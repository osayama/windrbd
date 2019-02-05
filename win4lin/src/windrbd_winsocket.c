﻿#include "drbd_windows.h"
#include "windrbd_threads.h"
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/tcp.h>
#include "windrbd_winsocket.h" 

/* Protects from API functions being called before the WSK provider is
 * initialized (see SocketsInit).
 */

#define WSK_DEINITIALIZED	0
#define WSK_DEINITIALIZING	1
#define WSK_INITIALIZING	2
#define WSK_INITIALIZED		3

struct net init_net;

static LONG wsk_state = WSK_DEINITIALIZED;

static WSK_REGISTRATION		g_WskRegistration;
static WSK_PROVIDER_NPI		g_WskProvider;
static WSK_CLIENT_DISPATCH	g_WskDispatch = { MAKE_WSK_VERSION(1, 0), 0, NULL };

static int winsock_to_linux_error(NTSTATUS status)
{
	switch (status) {
	case STATUS_SUCCESS:
		return 0;
	case STATUS_CONNECTION_RESET:
		return -ECONNRESET;
	case STATUS_CONNECTION_DISCONNECTED:
		return -ENOTCONN;
	case STATUS_CONNECTION_ABORTED:
		return -ECONNABORTED;
	case STATUS_IO_TIMEOUT:
	case STATUS_TIMEOUT:
		return -EAGAIN;
	case STATUS_INVALID_DEVICE_STATE:
		return -EINVAL;	/* -ENOTCONN? */
	case STATUS_NETWORK_UNREACHABLE:
		return -ENETUNREACH;
	case STATUS_HOST_UNREACHABLE:
		return -EHOSTUNREACH;
	case STATUS_CONNECTION_REFUSED:
		return -ECONNREFUSED;
	default:
		/* no printk's */
		// printk("Unknown status %x, returning -EIO.\n", status);
		return -EIO;
	}
}

static NTSTATUS NTAPI CompletionRoutine(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PIRP			Irp,
	__in PKEVENT		CompletionEvent
)
{
	/* Must not printk in here, will loop forever. Hence also no
	 * ASSERT.
	 */

	KeSetEvent(CompletionEvent, IO_NO_INCREMENT, FALSE);
	
	return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS InitWskData(
	__out PIRP*		pIrp,
	__out PKEVENT	CompletionEvent,
	__in  BOOLEAN	bRawIrp
)
{
	// DW-1316 use raw irp.
	/* TODO: is this still needed? CloseWskSocket uses it, but why? */
	if (bRawIrp) {
		*pIrp = ExAllocatePoolWithTag(NonPagedPool, IoSizeOfIrp(1), 'FFDW');
		IoInitializeIrp(*pIrp, IoSizeOfIrp(1), 1);
	}
	else {
		*pIrp = IoAllocateIrp(1, FALSE);
	}

	if (!*pIrp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	KeInitializeEvent(CompletionEvent, SynchronizationEvent, FALSE);
	IoSetCompletionRoutine(*pIrp, CompletionRoutine, CompletionEvent, TRUE, TRUE, TRUE);

	return STATUS_SUCCESS;
}

static NTSTATUS InitWskBuffer(
	__in  PVOID		Buffer,
	__in  ULONG		BufferSize,
	__out PWSK_BUF	WskBuffer,
	__in  BOOLEAN	bWriteAccess,
	__in  BOOLEAN	may_printk
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    WskBuffer->Offset = 0;
    WskBuffer->Length = BufferSize;

    WskBuffer->Mdl = IoAllocateMdl(Buffer, BufferSize, FALSE, FALSE, NULL);
    if (!WskBuffer->Mdl) {
	return STATUS_INSUFFICIENT_RESOURCES;
    }

    try {
	// DW-1223: Locking with 'IoWriteAccess' affects buffer, which causes infinite I/O from ntfs when the buffer is from mdl of write IRP.
	// we need write access for receiver, since buffer will be filled.
	MmProbeAndLockPages(WskBuffer->Mdl, KernelMode, bWriteAccess?IoWriteAccess:IoReadAccess);
    } except(EXCEPTION_EXECUTE_HANDLER) {
	if (WskBuffer->Mdl != NULL) {
	    IoFreeMdl(WskBuffer->Mdl);
	}
	if (may_printk)
		printk(KERN_ERR "MmProbeAndLockPages failed. exception code=0x%x\n", GetExceptionCode());
	return STATUS_INSUFFICIENT_RESOURCES;
    }
    return Status;
}

static VOID FreeWskBuffer(
__in PWSK_BUF WskBuffer,
int may_printk
)
{
	if (WskBuffer->Mdl->MdlFlags & MDL_PAGES_LOCKED) {
		MmUnlockPages(WskBuffer->Mdl);
	} else {
		if (may_printk)
			printk("Page not locked in FreeWskBuffer\n");
	}
	IoFreeMdl(WskBuffer->Mdl);
}

struct send_page_completion_info {
	struct page *page;
	char *data_buffer;
	struct _WSK_BUF *wsk_buffer;
	struct socket *socket;
};

static void have_sent(struct socket *socket, size_t length)
{
	ULONG_PTR flags;

	spin_lock_irqsave(&socket->send_buf_counters_lock, flags);
	socket->sk->sk_wmem_queued -= length;
	spin_unlock_irqrestore(&socket->send_buf_counters_lock, flags);

	KeSetEvent(&socket->data_sent, IO_NO_INCREMENT, FALSE);
}

static NTSTATUS NTAPI SendPageCompletionRoutine(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PIRP		Irp,
	__in struct send_page_completion_info *completion

)
{ 
	int may_printk = completion->page != NULL; /* called from SendPage */
	size_t length;

	if (Irp->IoStatus.Status != STATUS_SUCCESS) {
		if (may_printk && completion->socket->error_status != STATUS_SUCCESS &&
		    completion->socket->error_status != Irp->IoStatus.Status)
			printk(KERN_WARNING "Last error status of socket was %x, now got %x\n", completion->socket->error_status, Irp->IoStatus.Status);

		completion->socket->error_status = Irp->IoStatus.Status;
	}	/* TODO: if success, clear error status? This happens
		 * on boot when there are no configured network interfaces
		 * and works later once they are.
		 */
	length = completion->wsk_buffer->Length;
		/* Also unmaps the pages of the containg Mdl */

	FreeWskBuffer(completion->wsk_buffer, may_printk);
	kfree(completion->wsk_buffer);

	have_sent(completion->socket, length);

	if (completion->page)
		put_page(completion->page); /* Might free the page if connection is already down */
	if (completion->data_buffer)
		kfree(completion->data_buffer);
	kfree(completion);
	
	IoFreeIrp(Irp);

	return STATUS_MORE_PROCESSING_REQUIRED;
}

static int wait_for_sendbuf(struct socket *socket, size_t want_to_send)
{
	ULONG_PTR flags;
	LARGE_INTEGER timeout;
	NTSTATUS status;

	while (1) {
		spin_lock_irqsave(&socket->send_buf_counters_lock, flags);

		if (socket->sk->sk_wmem_queued > socket->sk->sk_sndbuf) {
			spin_unlock_irqrestore(&socket->send_buf_counters_lock, flags);
			timeout.QuadPart = -1 * socket->sk->sk_sndtimeo * 10 * 1000 * 1000 / HZ;

				/* TODO: no signals? */
			status = KeWaitForSingleObject(&socket->data_sent, Executive, KernelMode, FALSE, &timeout);

			if (status == STATUS_TIMEOUT)
				return -ETIMEDOUT;
		} else {
			socket->sk->sk_wmem_queued += want_to_send;
			spin_unlock_irqrestore(&socket->send_buf_counters_lock, flags);
			return 0;
		}
			/* TODO: if socket closed meanwhile return an error */
			/* TODO: need socket refcount for doing so */
	}
}

/* Library initialization routine: registers us and waits for
 * provider NPI to become ready (which may take some time on boot,
 * so do not call from DriverEntry, call it in a separate thread)
 */

static NTSTATUS SocketsInit(void)
{
	static WSK_CLIENT_NPI	WskClient = { 0 };
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;

	if (InterlockedCompareExchange(&wsk_state, WSK_INITIALIZING, WSK_DEINITIALIZED) != WSK_DEINITIALIZED)
		return STATUS_ALREADY_REGISTERED;

	WskClient.ClientContext = NULL;
	WskClient.Dispatch = &g_WskDispatch;

	Status = WskRegister(&WskClient, &g_WskRegistration);
	if (!NT_SUCCESS(Status)) {
		InterlockedExchange(&wsk_state, WSK_DEINITIALIZED);
		return Status;
	}

	printk("WskCaptureProviderNPI start.\n");
	Status = WskCaptureProviderNPI(&g_WskRegistration, WSK_INFINITE_WAIT, &g_WskProvider);
	printk("WskCaptureProviderNPI done.\n"); // takes long time! msg out after MVL loaded.

	if (!NT_SUCCESS(Status)) {
		printk(KERN_ERR "WskCaptureProviderNPI() failed with status 0x%08X\n", Status);
		WskDeregister(&g_WskRegistration);
		InterlockedExchange(&wsk_state, WSK_DEINITIALIZED);
		return Status;
	}

	InterlockedExchange(&wsk_state, WSK_INITIALIZED);
	return STATUS_SUCCESS;
}

/* Deregister network programming interface (NPI) and wsk. Reverse of
 * SocketsInit()
 */

void SocketsDeinit(void)
{
	if (InterlockedCompareExchange(&wsk_state, WSK_INITIALIZED, WSK_DEINITIALIZING) != WSK_INITIALIZED)
		return;
	WskReleaseProviderNPI(&g_WskRegistration);
	WskDeregister(&g_WskRegistration);

	InterlockedExchange(&wsk_state, WSK_DEINITIALIZED);
}

static int CreateSocket(
	__in ADDRESS_FAMILY		AddressFamily,
	__in USHORT			SocketType,
	__in ULONG			Protocol,
	__in PVOID			SocketContext,
	__in struct _WSK_CLIENT_LISTEN_DISPATCH *Dispatch,
	__in ULONG			Flags,
	struct _WSK_SOCKET		**out
)
{
	KEVENT			CompletionEvent = { 0 };
	PIRP			Irp = NULL;
	PWSK_SOCKET		WskSocket = NULL;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;

	/* NO _printk HERE, WOULD LOOP */
	if (wsk_state != WSK_INITIALIZED || out == NULL)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status))
		return -ENOMEM;

	Status = g_WskProvider.Dispatch->WskSocket(
				g_WskProvider.Client,
				AddressFamily,
				SocketType,
				Protocol,
				Flags,
				SocketContext,
				Dispatch,
				NULL,
				NULL,
				NULL,
				Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	if (NT_SUCCESS(Status))
		*out = (struct _WSK_SOCKET*) Irp->IoStatus.Information;

	IoFreeIrp(Irp);
	return winsock_to_linux_error(Status);
}

NTSTATUS CloseWskSocket(struct _WSK_SOCKET *WskSocket)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	LARGE_INTEGER	nWaitTime;
	nWaitTime.QuadPart = (-1 * 1000 * 10000);   // wait 1000ms relative 

	if (wsk_state != WSK_INITIALIZED || !WskSocket)
		return STATUS_INVALID_PARAMETER;

	Status = InitWskData(&Irp, &CompletionEvent, TRUE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}
	Status = ((PWSK_PROVIDER_BASIC_DISPATCH) WskSocket->Dispatch)->WskCloseSocket(WskSocket, Irp);
	if (Status == STATUS_PENDING) {
		Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime);
		if (Status == STATUS_TIMEOUT) { // DW-1316 detour WskCloseSocket hang in Win7/x86.
			printk(KERN_WARNING "Timeout... Cancel WskCloseSocket:%p. maybe required to patch WSK Kernel\n", WskSocket);
			IoCancelIrp(Irp);
			// DW-1388: canceling must be completed before freeing the irp.
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		}
		Status = Irp->IoStatus.Status;
	}
	IoFreeIrp(Irp);

	return Status;
}

static int wsk_getname(struct socket *socket, struct sockaddr *uaddr, int peer)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	status = STATUS_UNSUCCESSFUL;

	if (peer == 0)
		return -EOPNOTSUPP;

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL)
		return -EINVAL;

	status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(status))
		return winsock_to_linux_error(status);

	status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskGetRemoteAddress(socket->wsk_socket, uaddr, Irp);
	if (status != STATUS_SUCCESS)
	{
		if (status == STATUS_PENDING) {
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
			status = Irp->IoStatus.Status;
		}
	}
	IoFreeIrp(Irp);

	if (status == STATUS_SUCCESS)
		return sizeof(*uaddr);

	return winsock_to_linux_error(status);
}

static int wsk_connect(struct socket *socket, struct sockaddr *vaddr, int sockaddr_len, int flags)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

		/* TODO: check/implement those: */
	(void) sockaddr_len;
	(void) flags;

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL || vaddr == NULL)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status))
		return winsock_to_linux_error(Status);

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskConnect(
		socket->wsk_socket,
		vaddr,
		0,
		Irp);

	if (Status == STATUS_PENDING) {
		LARGE_INTEGER	nWaitTime;
		nWaitTime = RtlConvertLongToLargeInteger(-1 * socket->sk->sk_connecttimeo * 1000 * 10);

		if ((Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime)) == STATUS_TIMEOUT)
		{
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		}
	}

	if (Status == STATUS_SUCCESS)
	{
		Status = Irp->IoStatus.Status;
		if (Status == STATUS_SUCCESS)
			socket->sk->sk_state = TCP_ESTABLISHED;
	}

	IoFreeIrp(Irp);

	return winsock_to_linux_error(Status);
}

static int sock_create_linux_socket(struct socket **out);

int kernel_accept(struct socket *socket, struct socket **newsock, int io_flags)
{
	int err;
	struct _WSK_SOCKET *wsk_socket;
	struct socket *accept_socket;
	int flags;

	if ((io_flags | O_NONBLOCK) == 0)
		return -EOPNOTSUPP;

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL)
		return -EINVAL;

	spin_lock_irqsave(&socket->accept_socket_lock, flags);
	if (socket->accept_wsk_socket == NULL) {
		spin_unlock_irqrestore(&socket->accept_socket_lock, flags);
		return -EWOULDBLOCK;
	}
	wsk_socket = socket->accept_wsk_socket;
	socket->accept_wsk_socket = NULL;
	spin_unlock_irqrestore(&socket->accept_socket_lock, flags);

	err = sock_create_linux_socket(&accept_socket);
	if (err < 0)
		CloseWskSocket(wsk_socket);
	else {
		accept_socket->wsk_socket = wsk_socket;
		accept_socket->sk->sk_state = TCP_ESTABLISHED;
		accept_socket->sk->sk_state_change = socket->sk->sk_state_change;
		accept_socket->sk->sk_user_data = socket->sk->sk_user_data;

		*newsock = accept_socket;
	}

	return err;
}

	/* TODO: Or use the ControlSocket function */

static int wsk_set_event_callbacks(struct socket *socket, int mask)
{
	KEVENT CompletionEvent;
	PIRP Irp;
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	WSK_EVENT_CALLBACK_CONTROL callbackControl;

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent,FALSE);
	if (!NT_SUCCESS(Status))
		return -ENOMEM;

	callbackControl.NpiId = &NPI_WSK_INTERFACE_ID;
	callbackControl.EventMask = mask;

	Status = ((PWSK_PROVIDER_BASIC_DISPATCH)socket->wsk_socket->Dispatch)->WskControlSocket(socket->wsk_socket,
	        WskSetOption,
		SO_WSK_EVENT_CALLBACK,
		SOL_SOCKET,
		sizeof(WSK_EVENT_CALLBACK_CONTROL),
		&callbackControl,
		0,
		NULL,
		NULL,
		Irp
        );

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	IoFreeIrp(Irp);
	return winsock_to_linux_error(Status);
}

/* This just sets the callback event mask, socket->wsk_socket
 * must be a LISTEN socket (WSK_FLAG_LISTEN_SOCKET).
 */

static int wsk_listen(struct socket *socket, int len)
{
	NTSTATUS status;

	(void) len;

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL)
		return -EINVAL;

	return wsk_set_event_callbacks(socket, WSK_EVENT_ACCEPT);
}

int kernel_sock_shutdown(struct socket *sock, enum sock_shutdown_cmd how)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	LARGE_INTEGER	nWaitTime;
		/* TODO: this is also not a good idea ... */
	nWaitTime.QuadPart = (-1 * 1000 * 10000);   // wait 1000ms relative 
	
		/* TODO: one day ... */
	(void) how;

	if (wsk_state != WSK_INITIALIZED || sock == NULL || sock->wsk_socket == NULL)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return winsock_to_linux_error(Status);
	}
	
	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) sock->wsk_socket->Dispatch)->WskDisconnect(
		sock->wsk_socket,
		NULL,
		0,//WSK_FLAG_ABORTIVE,=> when disconnecting, ABORTIVE was going to standalone, and then we removed ABORTIVE
		Irp);

	if (Status == STATUS_PENDING) {
		Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime);
		if(STATUS_TIMEOUT == Status) { // DW-712 timeout process for fast closesocket in congestion mode, instead of WSK_FLAG_ABORTIVE
			printk("Timeout... Cancel Disconnect socket:%p\n", sock->wsk_socket);
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		} 

		Status = Irp->IoStatus.Status;
	}

	if (Status == STATUS_SUCCESS)
		sock->sk->sk_state = 0;

	IoFreeIrp(Irp);
	return winsock_to_linux_error(Status);
}


	/* TODO: maybe one day we also eliminate this function. It
	 * is currently only used for sending the first packet.
	 * Even more now when we do not have send buf implemented here..
	 */

	/* TODO: implement MSG_MORE? */

int kernel_sendmsg(struct socket *socket, struct msghdr *msg, struct kvec *vec,
                   size_t num, size_t len)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	ULONG Flags = 0;

	if (wsk_state != WSK_INITIALIZED || !socket || !socket->wsk_socket || !vec || vec[0].iov_base == NULL || ((int) vec[0].iov_len == 0))
		return -EINVAL;

	if (num != 1)
		return -EOPNOTSUPP;

	Status = InitWskBuffer(vec[0].iov_base, vec[0].iov_len, &WskBuffer, FALSE, TRUE);
	if (!NT_SUCCESS(Status)) {
		return winsock_to_linux_error(Status);
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer, 1);
		return winsock_to_linux_error(Status);
	}

	if (socket->no_delay)
		Flags |= WSK_FLAG_NODELAY;
	else
		Flags &= ~WSK_FLAG_NODELAY;

	mutex_lock(&socket->wsk_mutex);
	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskSend(
		socket->wsk_socket,
		&WskBuffer,
		Flags,
		Irp);
	mutex_unlock(&socket->wsk_mutex);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

		if (socket->sk->sk_sndtimeo <= 0 || socket->sk->sk_sndtimeo == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = NULL;
		}
		else
		{
			nWaitTime.QuadPart = -1 * socket->sk->sk_sndtimeo * 10 * 1000 * 1000 / HZ;
			pTime = &nWaitTime;
		}
		{
			struct      task_struct *thread = current;
			PVOID       waitObjects[2];
			int         wObjCount = 1;

			waitObjects[0] = (PVOID) &CompletionEvent;

			Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);
			switch (Status)
			{
			case STATUS_TIMEOUT:
				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				BytesSent = -EAGAIN;
				break;

			case STATUS_WAIT_0:
				if (NT_SUCCESS(Irp->IoStatus.Status))
				{
					BytesSent = (LONG)Irp->IoStatus.Information;
				}
				else
				{
					printk("tx error(%x) wsk(0x%p)\n",Irp->IoStatus.Status, socket->wsk_socket);
					switch (Irp->IoStatus.Status)
					{
						case STATUS_IO_TIMEOUT:
							BytesSent = -EAGAIN;
							break;
						case STATUS_INVALID_DEVICE_STATE:
							BytesSent = -EAGAIN;
							break;
						default:
							BytesSent = -ECONNRESET;
							break;
					}
				}
				break;

			//case STATUS_WAIT_1: // common: sender or send_bufferinf thread's kill signal
			//	IoCancelIrp(Irp);
			//	KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
			//	BytesSent = -EINTR;
			//	break;

			default:
				printk(KERN_ERR "Wait failed. status 0x%x\n", Status);
				BytesSent = winsock_to_linux_error(Status);
			}
		}
	}
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesSent = (LONG) Irp->IoStatus.Information;
			printk("WskSend No pending: but sent(%d)!\n", BytesSent);
		}
		else
		{
			printk("WskSend error(0x%x)\n", Status);
			BytesSent = winsock_to_linux_error(Status);
		}
	}


	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer, 1);

	return BytesSent;
}

ssize_t wsk_sendpage(struct socket *socket, struct page *page, int offset, size_t len, int flags)
{
	struct _IRP *Irp;
	struct _WSK_BUF *WskBuffer;
	struct send_page_completion_info *completion;
	NTSTATUS status;
	int err;

	if (wsk_state != WSK_INITIALIZED || !socket || !socket->wsk_socket || !page || ((int) len <= 0))
		return -EINVAL;

	if (socket->error_status != STATUS_SUCCESS)
		return winsock_to_linux_error(socket->error_status);

	get_page(page);		/* we might sleep soon, do this before */

	err = wait_for_sendbuf(socket, len);
	if (err < 0) {
		put_page(page);
		return err;
	}

	WskBuffer = kzalloc(sizeof(*WskBuffer), 0, 'DRBD');
	if (WskBuffer == NULL) {
		have_sent(socket, len);
		put_page(page);
		return -ENOMEM;
	}

	completion = kzalloc(sizeof(*completion), 0, 'DRBD');
	if (completion == NULL) {
		have_sent(socket, len);
		put_page(page);
		kfree(WskBuffer);
		return -ENOMEM;
	}

// printk("page: %p page->addr: %p page->size: %d offset: %d len: %d page->kref.refcount: %d\n", page, page->addr, page->size, offset, len, page->kref.refcount);

	status = InitWskBuffer((void*) (((unsigned char *) page->addr)+offset), len, WskBuffer, FALSE, TRUE);
	if (!NT_SUCCESS(status)) {
		have_sent(socket, len);
		put_page(page);
		kfree(completion);
		kfree(WskBuffer);
		return -ENOMEM;
	}

	completion->page = page;
	completion->wsk_buffer = WskBuffer;
	completion->socket = socket;

	Irp = IoAllocateIrp(1, FALSE);
	if (Irp == NULL) {
		have_sent(socket, len);
		put_page(page);
		kfree(completion);
		kfree(WskBuffer);
		FreeWskBuffer(WskBuffer, 1);
		return -ENOMEM;
	}
	IoSetCompletionRoutine(Irp, SendPageCompletionRoutine, completion, TRUE, TRUE, TRUE);

	if (socket->no_delay)
		flags |= WSK_FLAG_NODELAY;
	else
		flags &= ~WSK_FLAG_NODELAY;


	mutex_lock(&socket->wsk_mutex);
	status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskSend(
		socket->wsk_socket,
		WskBuffer,
		flags,
		Irp);
	mutex_unlock(&socket->wsk_mutex);

	switch (status) {
	case STATUS_PENDING:
			/* This now behaves just like Linux kernel socket
			 * sending functions do for TCP/IP: on return,
			 * the data is queued, if there is an error later
			 * we cannot know now, but a follow-up sending
			 * function will fail. To know about it, we
			 * have a error_status field in our socket struct
			 * which is set by the completion routine on
			 * error.
			 */

		return len;

	case STATUS_SUCCESS:
		return (LONG) Irp->IoStatus.Information;

	default:
		return winsock_to_linux_error(status);
	}
}

/* Do not use printk's in here, will loop forever... */

int SendTo(struct socket *socket, void *Buffer, size_t BufferSize, PSOCKADDR RemoteAddress)
{
	struct _IRP *irp;
	struct _WSK_BUF *WskBuffer;
	struct send_page_completion_info *completion;

		/* We copy what we send to a tmp buffer, so
		 * caller may free or use otherwise what we
		 * have got in Buffer.
		 */

	char *tmp_buffer;
	NTSTATUS status;
	int err;

	if (wsk_state != WSK_INITIALIZED || !socket || !socket->wsk_socket || !Buffer || !BufferSize)
		return -EINVAL;

	if (socket->error_status != STATUS_SUCCESS)
		return winsock_to_linux_error(socket->error_status);

	err = wait_for_sendbuf(socket, BufferSize);
	if (err < 0)
		return err;

	WskBuffer = kzalloc(sizeof(*WskBuffer), 0, 'DRBD');
	if (WskBuffer == NULL) {
		have_sent(socket, BufferSize);
		return -ENOMEM;
	}

	completion = kzalloc(sizeof(*completion), 0, 'DRBD');
	if (completion == NULL) {
		have_sent(socket, BufferSize);
		kfree(WskBuffer);
		return -ENOMEM;
	}

	tmp_buffer = kmalloc(BufferSize, 0, 'TMPB');
	if (tmp_buffer == NULL) {
		have_sent(socket, BufferSize);
		kfree(completion);
		kfree(WskBuffer);
		return -ENOMEM;
	}
	memcpy(tmp_buffer, Buffer, BufferSize);

	status = InitWskBuffer(tmp_buffer, BufferSize, WskBuffer, FALSE, FALSE);
	if (!NT_SUCCESS(status)) {
		have_sent(socket, BufferSize);
		kfree(completion);
		kfree(WskBuffer);
		kfree(tmp_buffer);
		return -ENOMEM;
	}

	completion->data_buffer = tmp_buffer;
	completion->wsk_buffer = WskBuffer;
	completion->socket = socket;

	irp = IoAllocateIrp(1, FALSE);
	if (irp == NULL) {
		have_sent(socket, BufferSize);
		kfree(completion);
		kfree(WskBuffer);
		kfree(tmp_buffer);
		FreeWskBuffer(WskBuffer, 0);
		return -ENOMEM;
	}
	IoSetCompletionRoutine(irp, SendPageCompletionRoutine, completion, TRUE, TRUE, TRUE);

	status = ((PWSK_PROVIDER_DATAGRAM_DISPATCH) socket->wsk_socket->Dispatch)->WskSendTo(
		socket->wsk_socket,
		WskBuffer,
		0,
		RemoteAddress,
		0,
		NULL,
		irp);

		/* Again if not yet sent, pretend that it has been sent,
		 * followup calls to SendTo() on that socket will report
		 * errors. This is how Linux behaves.
		 */

	if (status == STATUS_PENDING)
		status = STATUS_SUCCESS;

	return status == STATUS_SUCCESS ? BufferSize : winsock_to_linux_error(status);
}

int kernel_recvmsg(struct socket *socket, struct msghdr *msg, struct kvec *vec,
                   size_t num, size_t len, int flags)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesReceived;
	NTSTATUS	Status;
	ULONG		wsk_flags;

	struct      task_struct *thread = current;
	PVOID       waitObjects[2];
	int         wObjCount = 1;

	if (wsk_state != WSK_INITIALIZED || !socket || !socket->wsk_socket || !vec || vec[0].iov_base == NULL || ((int) vec[0].iov_len == 0))
		return -EINVAL;

	if (num != 1)
		return -EOPNOTSUPP;

	Status = InitWskBuffer(vec[0].iov_base, vec[0].iov_len, &WskBuffer, TRUE, TRUE);
	if (!NT_SUCCESS(Status)) {
		return winsock_to_linux_error(Status);
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);

	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer, 1);
		return winsock_to_linux_error(Status);
	}

	wsk_flags = 0;
	if (flags | MSG_WAITALL)
		wsk_flags |= WSK_FLAG_WAITALL;

	mutex_lock(&socket->wsk_mutex);
	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskReceive(
				socket->wsk_socket,
				&WskBuffer,
				wsk_flags,
				Irp);
	mutex_unlock(&socket->wsk_mutex);

    if (Status == STATUS_PENDING)
    {
        LARGE_INTEGER	nWaitTime;
        LARGE_INTEGER	*pTime;

        if (socket->sk->sk_rcvtimeo <= 0 || socket->sk->sk_rcvtimeo == MAX_SCHEDULE_TIMEOUT)
        {
            pTime = 0;
        }
        else
        {
            nWaitTime.QuadPart = -1LL * socket->sk->sk_rcvtimeo * 1000 * 10 * 1000 / HZ;
            pTime = &nWaitTime;
        }

        waitObjects[0] = (PVOID) &CompletionEvent;
        if (thread->has_sig_event)
        {
            waitObjects[1] = (PVOID) &thread->sig_event;
            wObjCount = 2;
        } 

        Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);
        switch (Status)
        {
        case STATUS_WAIT_0: // waitObjects[0] CompletionEvent
            if (Irp->IoStatus.Status == STATUS_SUCCESS)
            {
                BytesReceived = (LONG) Irp->IoStatus.Information;
            }
            else
            {
		printk("RECV wsk(0x%p) multiWait err(0x%x)\n", socket->wsk_socket, Irp->IoStatus.Status);
		BytesReceived = winsock_to_linux_error(Irp->IoStatus.Status);
            }
            break;

        case STATUS_WAIT_1:
            BytesReceived = -EINTR;
            break;

        case STATUS_TIMEOUT:
            BytesReceived = -EAGAIN;
            break;

        default:
            BytesReceived = winsock_to_linux_error(Status);
            break;
        }
    }
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesReceived = (LONG) Irp->IoStatus.Information;
			printk("(%s) Rx No pending and data(%d) is avail\n", current->comm, BytesReceived);
		}
		else
		{
			printk("WskReceive Error Status=0x%x\n", Status); // EVENT_LOG!
			BytesReceived = winsock_to_linux_error(Status);
		}
	}

	if (BytesReceived == -EINTR || BytesReceived == -EAGAIN)
	{
		// cancel irp in wsk subsystem
		IoCancelIrp(Irp);
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		if (Irp->IoStatus.Information > 0)
		{
				/* When network is interrupted while we
				 * are serving a Primary Diskless server
				 * we want DRBD to know that the network
				 * is down. Do not deliver the data to
				 * DRBD, it should cancel the receiver
				 * instead (else it would get stuck in
				 * NetworkFailure). This is probably a
				 * DRBD bug, since Linux (userland) recv
				 * would deliver EINTR only if no data
				 * is available.
				 */

		/* Deliver what we have in case we timed out. */

			if (BytesReceived == -EAGAIN) {
				printk("Timed out, but there is data (%d bytes) returning it.\n", Irp->IoStatus.Information);
				BytesReceived = Irp->IoStatus.Information;
			} else {
				printk("Receiving canceled (errno is %d) but data available (%d bytes, will be discarded).\n", BytesReceived, Irp->IoStatus.Information);
			}
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer, 1);

	return BytesReceived;
}

/* Must not printk() from in here, might loop forever */
static int wsk_bind(
	struct socket *socket,
	struct sockaddr *myaddr,
	int sockaddr_len
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	(void) sockaddr_len;	/* TODO: check this parameter */

	if (wsk_state != WSK_INITIALIZED || socket == NULL || socket->wsk_socket == NULL || myaddr == NULL)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return winsock_to_linux_error(Status);
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) socket->wsk_socket->Dispatch)->WskBind(
		socket->wsk_socket,
		myaddr,
		0,
		Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}
	IoFreeIrp(Irp);
	return winsock_to_linux_error(Status);
}

static NTSTATUS ControlSocket(
	__in PWSK_SOCKET	WskSocket,
	__in ULONG			RequestType,
	__in ULONG		    ControlCode,
	__in ULONG			Level,
	__in SIZE_T			InputSize,
	__in_opt PVOID		InputBuffer,
	__in SIZE_T			OutputSize,
	__out_opt PVOID		OutputBuffer,
	__out_opt SIZE_T	*OutputSizeReturned
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (wsk_state != WSK_INITIALIZED || !WskSocket)
		return -EINVAL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		printk(KERN_ERR "InitWskData() failed with status 0x%08X\n", Status);
		return winsock_to_linux_error(Status);
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskControlSocket(
				WskSocket,
				RequestType,		// WskSetOption, 
				ControlCode,		// SIO_WSK_QUERY_RECEIVE_BACKLOG, 
				Level,				// IPPROTO_IPV6,
				InputSize,			// sizeof(optionValue),
				InputBuffer,		// NULL, 
				OutputSize,			// sizeof(int), 
				OutputBuffer,		// &backlog, 
				OutputSizeReturned, // NULL,
				Irp);


	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	IoFreeIrp(Irp);
	return Status;
}

int kernel_setsockopt(struct socket *sock, int level, int optname, char *optval,
		      unsigned int optlen)
{
	NTSTATUS status;
	ULONG flag;

	if (sock == NULL)
		return -EINVAL;

	switch (level) {
	case SOL_TCP:
		switch (optname) {
		case TCP_NODELAY:
			if (optlen < 1)
				return -EINVAL;

			sock->no_delay = *optval;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;

	case SOL_SOCKET:
		switch (optname) {
		case SO_REUSEADDR:
			if (optlen < 1)
				return -EINVAL;

			flag = *optval;	
			status = ControlSocket(sock->wsk_socket, WskSetOption, SO_REUSEADDR, SOL_SOCKET, sizeof(flag), &flag, 0, NULL, NULL);

			return winsock_to_linux_error(status);

		default:
			return -EOPNOTSUPP;
		}

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

struct proto_ops winsocket_ops = {
	.bind = wsk_bind,
	.listen = wsk_listen,
	.connect = wsk_connect,
	.sendpage = wsk_sendpage,
	.getname = wsk_getname
};

static void wsk_sock_statechange(struct sock *sk)
{
}

static int sock_create_linux_socket(struct socket **out)
{
	struct socket *socket;

	socket = kzalloc(sizeof(*socket), 0, '3WDW');
	if (!socket)
		return -ENOMEM;

	socket->sk = kzalloc(sizeof(*socket->sk), 0, 'KARI');
	if (!socket->sk) {
		kfree(socket);
		return -ENOMEM; 
	}

	socket->error_status = STATUS_SUCCESS;

	spin_lock_init(&socket->send_buf_counters_lock);
	spin_lock_init(&socket->accept_socket_lock);
	KeInitializeEvent(&socket->data_sent, SynchronizationEvent, FALSE);
	mutex_init(&socket->wsk_mutex);
	socket->ops = &winsocket_ops;

	socket->sk->sk_sndbuf = 4*1024*1024;
	socket->sk->sk_rcvbuf = 4*1024*1024;
	socket->sk->sk_wmem_queued = 0;
	socket->sk->sk_socket = socket;
	socket->sk->sk_sndtimeo = 10*HZ;
	socket->sk->sk_rcvtimeo = 10*HZ;
	socket->sk->sk_state_change = wsk_sock_statechange;
	rwlock_init(&socket->sk->sk_callback_lock);

	*out = socket;

	return 0;
}

static NTSTATUS WSKAPI wsk_incoming_connection (
    _In_  PVOID         SocketContext,
    _In_  ULONG         Flags,
    _In_  PSOCKADDR     LocalAddress,
    _In_  PSOCKADDR     RemoteAddress,
    _In_opt_  PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID *AcceptSocketContext,
    _Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch
)
{
	int flags;
	struct socket *socket = (struct socket*) SocketContext;

	spin_lock_irqsave(&socket->accept_socket_lock, flags);
	if (socket->accept_wsk_socket != NULL)
		socket->dropped_accept_sockets++;
	socket->accept_wsk_socket = AcceptSocket;
	spin_unlock_irqrestore(&socket->accept_socket_lock, flags);

	if (socket->sk->sk_state_change)
		socket->sk->sk_state_change(socket->sk);

	return STATUS_SUCCESS;
}

static struct _WSK_CLIENT_LISTEN_DISPATCH listen_dispatch = {
	wsk_incoming_connection,
	NULL,
	NULL
};

static int wsk_sock_create_kern(void *net_namespace,
	ADDRESS_FAMILY		family,
	USHORT			type,
	ULONG			protocol,
	ULONG			Flags,
	struct socket  		**out)
{
	struct _WSK_SOCKET *wsk_socket;
	struct socket *socket;
	int err;
	NTSTATUS status;

	if (net_namespace != &init_net)
		return -EINVAL;

	err = sock_create_linux_socket(&socket);
	if (err < 0)
		return err;

	if (Flags == WSK_FLAG_LISTEN_SOCKET)
		err = CreateSocket(family, type, protocol,
				socket, &listen_dispatch, Flags, &wsk_socket);
	else
		err = CreateSocket(family, type, protocol,
				NULL, NULL, Flags, &wsk_socket);

	if (err < 0)
		return err;

	socket->wsk_socket = wsk_socket;
	*out = socket;
	return 0;
}

int sock_create_kern(struct net *net, int family, int type, int proto, struct socket **res)
{
	ULONG Flags;

	switch (type) {
	case SOCK_DGRAM:
		Flags = WSK_FLAG_DATAGRAM_SOCKET;
		break;

	case SOCK_STREAM:
		Flags = WSK_FLAG_CONNECTION_SOCKET;
		break;

	case SOCK_LISTEN:	/* windrbd specific */
		Flags = WSK_FLAG_LISTEN_SOCKET;
		type = SOCK_STREAM;
		break;

	default:
		return -EINVAL;
	}

	return wsk_sock_create_kern(net, family, type, proto, Flags, res);
}

void sock_release(struct socket *sock)
{
	NTSTATUS status;

	if (sock == NULL)
		return;

	status = CloseWskSocket(sock->wsk_socket);
	if (!NT_SUCCESS(status))
	{
		WDRBD_ERROR("error=0x%x\n", status);
		return;
	}

	kfree(sock->sk);
	kfree(sock);
}

void platform_update_socket_buffer_sizes(struct socket *socket)
{
	NTSTATUS status;

	if (socket == NULL)
		return;

	if (socket->sk->sk_userlocks & SOCK_SNDBUF_LOCK) {
                KeSetEvent(&socket->data_sent, IO_NO_INCREMENT, FALSE);
		socket->sk->sk_userlocks &= ~SOCK_SNDBUF_LOCK;
	}
	if (socket->sk->sk_userlocks & SOCK_RCVBUF_LOCK) {
                status = ControlSocket(socket->wsk_socket, WskSetOption, SO_RCVBUF, SOL_SOCKET, sizeof(socket->sk->sk_rcvbuf), &socket->sk->sk_rcvbuf, 0, NULL, NULL);
                if (status != STATUS_SUCCESS)
                        printk(KERN_WARNING "Could not set receive buffer size to %d, status is %x\n", socket->sk->sk_rcvbuf, status);
		socket->sk->sk_userlocks &= ~SOCK_RCVBUF_LOCK;
	}
}


static void *init_wsk_thread;

/* This is a separate thread, since it blocks until Windows has finished
 * booting. It initializes everything we need and then exits. You can
 * ignore the return value.
 */

static NTSTATUS windrbd_init_wsk_thread(void *unused)
{
	NTSTATUS status;
	int err;

        /* We have to do that here in a separate thread, else Windows
	 * will deadlock on booting.
         */
        status = SocketsInit();

        if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "Failed to initialize socket layer, status is %x.\n", status);
			/* and what now? */
	} else {
		printk("WSK initialized.\n");
	}

#if 0
	err = windrbd_create_boot_device();
	printk("windrbd_create_boot_device returned %d\n", err);
#endif

	return status;
}

NTSTATUS windrbd_init_wsk(void)
{
	HANDLE h;
	NTSTATUS status;

	status = windrbd_create_windows_thread(windrbd_init_wsk_thread, NULL, &init_wsk_thread);

	if (!NT_SUCCESS(status))
		printk("Couldn't create thread for initializing socket layer: windrbd_create_windows_thread failed with status 0x%x\n", status);

	return status;
}

	/* Under normal conditions, the thread already terminated long ago.
	 * Wait for its termination in case it is still running.
	 */

void windrbd_shutdown_wsk(void)
{
        NTSTATUS status;

        status = windrbd_cleanup_windows_thread(init_wsk_thread);

        if (!NT_SUCCESS(status))
                printk("windrbd_cleanup_windows_thread failed with status %x\n", status);

/* TODO: this BSODs or stalls? */
/*	SocketsDeinit(); */
}
