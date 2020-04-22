#include "drbd_windows.h"
#include "windrbd_threads.h"
#include "drbd_int.h"

/* #define WINDRBD_RUN_TESTS 1 */

#ifdef RELEASE
#ifdef WINDRBD_RUN_TESTS
#undef WINDRBD_RUN_TESTS
#endif
#endif

/* #define WINDRBD_RUN_RCU_LOCK_RECURSION_TEST 1 */
/* #define WINDRBD_RUN_RCU_LOCK_SYNCHRONIZE_RECURSION_TEST 1 */
#define WINDRBD_PRINTK_PING 1
/* #define WINDRBD_WAIT_EVENT_TEST 1 */
/* #define WINDRBD_SCHEDULE_UNINTERRUPTIBLE_TEST 1 */

#ifdef WINDRBD_RUN_TESTS

/* This currently succeeds on Windows 7 and Windows 10. Test it also
 * with Windows Server 2016. It should stall the machine but it
 * doesn't.
 */

static int rcu_lock_recursion(void *unused)
{
	KIRQL flags1, flags2, flags3;

printk("1\n");
	flags1 = rcu_read_lock();
printk("2 flags1 is %d IRQL is %d\n", flags1, KeGetCurrentIrql());
	flags2 = rcu_read_lock();
printk("3 flags2 is %d IRQL is %d\n", flags2, KeGetCurrentIrql());
	flags3 = rcu_read_lock();
printk("4 flags3 is %d IRQL is %d\n", flags3, KeGetCurrentIrql());

	rcu_read_unlock(flags3);
printk("5 IRQL is %d\n", KeGetCurrentIrql());
	rcu_read_unlock(flags2);
printk("6 IRQL is %d\n", KeGetCurrentIrql());
	rcu_read_unlock(flags1);

printk("7 IRQL is %d\n", KeGetCurrentIrql());
	return 0;
}

/* As expected this test stalls the machine (does not dispatch
 * new threads any more)
 */
static int rcu_lock_synchronize_recursion(void *unused)
{
	KIRQL flags;

printk("1\n");
	flags = rcu_read_lock();
printk("2 flags is %d IRQL is %d\n", flags, KeGetCurrentIrql());
	synchronize_rcu();
printk("3 IRQL is %d\n", KeGetCurrentIrql());
	rcu_read_unlock(flags);
printk("4 IRQL is %d\n", KeGetCurrentIrql());

	return 0;
}

static int run_printk_ping = 1;
extern int num_pnp_requests;
extern int num_pnp_bus_requests;
extern int threads_sleeping;
struct drbd_connection *root_connection;
extern int duplicate_completions;
extern int raised_irql_waits;

static int printk_ping(void *unused)
{
	int i;
#if 0
	struct drbd_connection *connection;
#endif

	i=0;
	while (run_printk_ping) {
		printk("ping %d (drbd bus is %p) num_pnp_requests %d num_pnp_bus_requests %d currently %d threads sleeping interruptible duplicate_completions %d raised_irql_waits: %d\n", i, drbd_bus_device, num_pnp_requests, num_pnp_bus_requests, threads_sleeping, duplicate_completions, raised_irql_waits);
#if 0
		if (root_connection) {
			connection = root_connection; /* polymorph magic */
			drbd_info(connection, "root_connection %p", root_connection);
		}
#endif
		i++;
		msleep(1000);
	}
	return 0;
}

#endif

#ifdef WINDRBD_WAIT_EVENT_TEST

static int cond = 0;

static int wakeup_wait_queue(void *_w)
{
	struct wait_queue_head *w = _w;

printk("waiting a bit ...\n");
	msleep(1000);
printk("wake up queue ...\n");
	wake_up(w);

printk("waiting a bit ...\n");
	msleep(1000);
printk("setting condition to true (non-zero) ...\n");
	cond = 1;
printk("wake up queue ...\n");
	wake_up(w);
printk("wait_event_interruptible should have terminated now.\n");
	return 0;
}

static int wait_event_test(void *unused)
{
	struct wait_queue_head w;
	int ret;

printk("Waiting 3 minutes so you can uninstall windrbd\n");

	msleep(3*60*1000);

printk("starting wait_event test\n");
	init_waitqueue_head(&w);

printk("starting waker\n");
	kthread_run(wakeup_wait_queue, &w, "waker");

printk("waiting for waker\n");
	wait_event_interruptible(ret, w, cond);
printk("wait_event_interruptible returned %d\n", ret);

printk("ending test\n");
	return 0;
}

#endif

#ifdef WINDRBD_SCHEDULE_UNINTERRUPTIBLE_TEST

/* This test just calls schedule_timeout_uninterruptible() - this
 * function BSOD'ed immediately before 5fc2788ef91.
 */

static int schedule_uninterruptible_test(void *unused)
{
	struct wait_queue_head w;
	int ret;

printk("Waiting 4 minutes so you can uninstall windrbd\n");

	msleep(4*60*1000);

printk("starting schedule uninterruptible test\n");
printk("waiting a second ...\n");
	schedule_timeout_uninterruptible(HZ);
printk("finished\n");

	return 0;
}

#endif

void windrbd_run_tests(void)
{
#ifdef WINDRBD_RUN_TESTS
#ifdef WINDRBD_RUN_RCU_LOCK_RECURSION_TEST
	kthread_run(rcu_lock_recursion, NULL, "rcu-lock-rec");
#endif
#ifdef WINDRBD_RUN_RCU_LOCK_SYNCHRONIZE_RECURSION_TEST
	kthread_run(rcu_lock_synchronize_recursion, NULL, "rcu-lock-sync");
#endif
#ifdef WINDRBD_PRINTK_PING
	kthread_run(printk_ping, NULL, "printk-ping");
#endif
#ifdef WINDRBD_WAIT_EVENT_TEST
	kthread_run(wait_event_test, NULL, "wait-event");
#endif
#ifdef WINDRBD_SCHEDULE_UNINTERRUPTIBLE_TEST
	kthread_run(schedule_uninterruptible_test, NULL, "schedule-unintr");
#endif
#else
	printk("WinDRBD self-tests disabled, see windrbd_test.c for how to enabled them.\n");
#endif
}


void windrbd_shutdown_tests(void)
{
#ifdef WINDRBD_RUN_TESTS
#ifdef WINDRBD_PRINTK_PING
	run_printk_ping = 0;
#endif
#endif
}

static long long non_atomic_int = 0;
static spinlock_t test_lock;
static struct mutex test_mutex;

static unsigned long long my_strtoull(const char *nptr, const char ** endptr, int base)
{
        unsigned long long val = 0;

        while (isdigit(*nptr)) {
                val *= 10;
                val += (*nptr)-'0';
                nptr++;
        }
        if (endptr)
                *endptr = nptr;

        return val;
}

static unsigned long long n;
static unsigned long long num_threads;
static int test_debug;

enum lock_methods { LM_NONE, LM_SPIN_LOCK, LM_SPIN_LOCK_IRQ, LM_SPIN_LOCK_IRQSAVE, LM_MUTEX, LM_LAST };
static char *lock_methods[LM_LAST] = {
	"none", "spin_lock", "spin_lock_irq", "spin_lock_irqsave", "mutex"
};
static enum lock_methods lock_method;

int concurrency_thread(void *c)
{
	long long j;
	volatile long long val;
	struct completion *completion = c;
	KIRQL flags;

	flags = 0;
	for (j=0;j<n;j++) {
		switch (lock_method) {
		case LM_MUTEX:
			mutex_lock(&test_mutex);
			break;

		case LM_SPIN_LOCK:
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_lock\n", KeGetCurrentIrql());
			spin_lock(&test_lock);
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_lock\n", KeGetCurrentIrql());
			break;

		case LM_SPIN_LOCK_IRQ:
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_lock\n", KeGetCurrentIrql());
			spin_lock_irq(&test_lock);
			if (KeGetCurrentIrql() != DISPATCH_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_lock\n", KeGetCurrentIrql());
			break;

		case LM_SPIN_LOCK_IRQSAVE:
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_lock\n", KeGetCurrentIrql());
			spin_lock_irqsave(&test_lock, flags); /* is a macro */
			if (KeGetCurrentIrql() != DISPATCH_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_lock\n", KeGetCurrentIrql());
			if (flags != PASSIVE_LEVEL)
				printk("Warning: flags is %d\n", flags);
			break;
		case LM_NONE:
			break;
		default:
			printk("lock method %d not supported.\n", lock_method);
			return -1;
		}

		val = non_atomic_int;
		val++;
		non_atomic_int = val;

		switch (lock_method) {
		case LM_MUTEX:
			mutex_unlock(&test_mutex);
			break;

		case LM_SPIN_LOCK:
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_unlock\n", KeGetCurrentIrql());
			spin_unlock(&test_lock);
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_unlock\n", KeGetCurrentIrql());
			break;
		case LM_SPIN_LOCK_IRQ:
			if (KeGetCurrentIrql() != DISPATCH_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_lock\n", KeGetCurrentIrql());
			spin_unlock_irq(&test_lock);
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_lock\n", KeGetCurrentIrql());
			break;
		case LM_SPIN_LOCK_IRQSAVE:
			if (KeGetCurrentIrql() != DISPATCH_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d before spin_lock\n", KeGetCurrentIrql());
			spin_unlock_irqrestore(&test_lock, flags);
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
				printk("Warning: KeGetCurrentIrql() is %d after spin_lock\n", KeGetCurrentIrql());
			break;
		case LM_NONE:
			break;
		default:
			printk("lock method %d not supported.\n", lock_method);
			return -1;
		}
	}

	if (test_debug)
		printk("thread finished\n");

	complete(c);
	return 0;
}

/* windrbd run-test 'concurrency_test 100 10000000' */

void concurrency_test(int argc, const char **argv)
{
	int i;
	const char *s;
	size_t len;
	struct completion **completions;
	enum lock_methods m;

	test_debug = 0;

	if (argc < 4)
		goto usage;

	i=1;
	if (argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'd': test_debug = 1; break;
		default: goto usage;
		}
		i++;
	}
	if (argc != i+3)
		goto usage;

	num_threads = my_strtoull(argv[i], &s, 10);
	if (*s != '\0')
		goto usage;

	i++;
	n = my_strtoull(argv[i], &s, 10);
	if (*s != '\0')
		goto usage;

	i++;
	for (m=LM_NONE;m<LM_LAST;m++) {
		len = strlen(lock_methods[m]);
		if (strncmp(lock_methods[m], argv[i], len) == 0 &&
		   (argv[i][len] == '\0'))
			break;
	}
	if (m == LM_LAST) 
		goto usage;
	lock_method = m;

	if (test_debug) {
		printk("n is %llu num_threads is %llu\n", n, num_threads);
		printk("sizeof(non_atomic_int) is %d\n", sizeof(non_atomic_int));
	}

	non_atomic_int = 0;
	spin_lock_init(&test_lock);
	mutex_init(&test_mutex);
	completions = kmalloc(sizeof(*completions)*num_threads, 0, '1234');
	if (completions == NULL) {
		printk("Not enough memory\n");
		return;
	}

	for (i=0;i<num_threads;i++) {
		completions[i] = kmalloc(sizeof(struct completion), 0, '1234');
		if (completions[i] == NULL) {
			printk("Not enough memory\n");
			return;
		}

		init_completion(completions[i]);

		if (test_debug)
			printk("about to start thread %i\n", i);

		kthread_run(concurrency_thread, completions[i], "concurrency_test");
	}
	for (i=0;i<num_threads;i++) {
		wait_for_completion(completions[i]);
		if (test_debug)
			printk("thread %i completed\n", i);
		kfree(completions[i]);
	}
	kfree(completions);

	if (non_atomic_int != n*num_threads) {
		printk("Test failed\n");
	}
	printk("non_atomic_int is %lld (should be %lld)\n", non_atomic_int, n*num_threads);
	return;

usage:
	printk("Usage: concurrency_test [-d] <num_threads> <n> <lock-method>\n");
}

void mutex_trylock_test(void)
{
	static struct mutex m, m2;
	int i, i2;

	mutex_init(&m);
	mutex_init(&m2);

		/* TODO: muteces are recursive ... */
	i = mutex_trylock(&m);
	if (i) {
		i2 = mutex_trylock(&m);
		if (i2)
			printk("mutex_trylock succeeded twice\n");
	}
	while (mutex_is_locked(&m)) {
		printk("mutex_unlock ...\n");
		mutex_unlock(&m);
	}
		/* here all mutexes must be unlocked (=signalled state)
		 * else BSOD on returning to user space.
		 */
}

void argv_test(int argc, char ** argv)
{
	int i;

	for (i=0; i<argc; i++)
		printk("argv[%d] is %s\n", i, argv[i]);
}

void test_main(const char *arg)
{
	char *arg_mutable, *s;
	char **argv;
	int argc;
	int i;

	arg_mutable = kstrdup(arg, 0);
	if (arg_mutable == NULL) {
		printk("Sorry no memory.\n");
		return;
	}
	s = arg_mutable;
	argc = 0;

	while (1) {
		while (*s == ' ') s++;
		if (*s == '\0')
			break;
		argc++;
		while (*s != ' ' && *s != '\0') s++;
	}
	argv = kmalloc(sizeof(*argv)*(argc+1), 0, 'DRBD');
	if (argv == NULL) {
		printk("Sorry no memory.\n");
		goto kfree_arg_mutable;
	}

	i = 0;
	s = arg_mutable;
	while (1) {
		while (*s == ' ') s++;
		if (*s == '\0')
			break;
		argv[i] = s;
		i++;
		while (*s != ' ' && *s != '\0') s++;
		if (*s != '\0') {
			*s = '\0';
			s++;
		}
	}
	if (i!=argc) {
		printk("Bug: i (%d) != argc(%d)\n", i, argc);
		goto kfree_argv;
	}

	if (strcmp(argv[0], "mutex_trylock_test") == 0)
		mutex_trylock_test();
	if (strcmp(argv[0], "concurrency_test") == 0)
		concurrency_test(argc, argv);
	if (strcmp(argv[0], "argv_test") == 0)
		argv_test(argc, argv);

kfree_argv:
	kfree(argv);
kfree_arg_mutable:
	kfree(arg_mutable);
}
