﻿/*
        Copyright(C) 2017-2018, Johannes Thoma <johannes@johannesthoma.com>
        Copyright(C) 2017-2018, LINBIT HA-Solutions GmbH  <office@linbit.com>
	Copyright(C) 2007-2016, ManTechnology Co., LTD.
	Copyright(C) 2007-2016, wdrbd@mantech.co.kr

	Windows DRBD is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	Windows DRBD is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Windows DRBD; see the file COPYING. If not, write to
	the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// #include <Ntifs.h>
#include <ntddk.h>
#include <stdlib.h>
#include <Mountmgr.h>
#include <ntddvol.h>
#include <Ntstrsafe.h>

#include "drbd_windows.h"
#include "drbd_wingenl.h"
#include "drbd_int.h"

static NTSTATUS GetRegistryValue(PCWSTR pwcsValueName, ULONG *pReturnLength, UCHAR *pucReturnBuffer, size_t buflen, PUNICODE_STRING pRegistryPath)
{
    HANDLE hKey;
    ULONG ulLength;
    NTSTATUS status;
    OBJECT_ATTRIBUTES stObjAttr;
    UNICODE_STRING valueName;
    KEY_VALUE_PARTIAL_INFORMATION stKeyInfo;
    PKEY_VALUE_PARTIAL_INFORMATION pstKeyInfo;

    RtlInitUnicodeString(&valueName, pwcsValueName);

    InitializeObjectAttributes(&stObjAttr, pRegistryPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    status = ZwOpenKey(&hKey, KEY_ALL_ACCESS, &stObjAttr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    ulLength = 0;
    status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation, &stKeyInfo, sizeof(KEY_VALUE_PARTIAL_INFORMATION), &ulLength);
    if (!NT_SUCCESS(status) && (status != STATUS_BUFFER_OVERFLOW) && (status != STATUS_BUFFER_TOO_SMALL))
    {
        ZwClose(hKey);
        return status;
    }

    pstKeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, ulLength, '36DW');
    if (pstKeyInfo == NULL)
    {
        ZwClose(hKey);
        return status;
    }

    status = ZwQueryValueKey(hKey, &valueName, KeyValuePartialInformation, pstKeyInfo, ulLength, &ulLength);
    if (NT_SUCCESS(status))
    {
        if (ulLength >= buflen)
            status = STATUS_BUFFER_TOO_SMALL;
        else
            RtlCopyMemory(pucReturnBuffer, pstKeyInfo->Data, pstKeyInfo->DataLength);

        *pReturnLength = pstKeyInfo->DataLength;
    }
    ExFreePool(pstKeyInfo);
    ZwClose(hKey);
    return status;
}

int initRegistry(__in PUNICODE_STRING RegPath_unicode)
{
	ULONG ulLength;
	ULONG ip_length;
	UCHAR aucTemp[255] = { 0 };
	NTSTATUS status;
	char syslog_ip[255];

	ip_length = 0;

	status = GetRegistryValue(L"syslog_ip", &ulLength, (UCHAR*)&aucTemp, sizeof(aucTemp), RegPath_unicode);
	if (status == STATUS_SUCCESS) {
		status = RtlUnicodeToUTF8N(syslog_ip, ARRAY_SIZE(syslog_ip)-1, &ip_length, (WCHAR*) aucTemp, ulLength);
	}
	if (status != STATUS_SUCCESS) {
		strcpy(syslog_ip, "127.0.0.1");
	} else {
		syslog_ip[ip_length] = '\0';
	}
	set_syslog_ip(syslog_ip);
	printk("syslog_ip from registry is %s", syslog_ip);

	return 0;
}

/* TODO: move somewhere else */

/* TODO: argh... */
char *kvasprintf(int flags, const char *fmt, va_list args)
{
	char *buffer;
	const int size = 4096; /* TODO: no. */
	NTSTATUS status;

	buffer = kzalloc(size, flags, 'AVDW');
	if (buffer) {
			/* TODO: RtlStringCbVPrintfA */
		status = RtlStringCchVPrintfA(buffer, size, fmt, args);
		if (status == STATUS_SUCCESS)
			return buffer;

		kfree(buffer);
	}

	return NULL;
}

size_t windrbd_vsnprintf(char *buf, size_t bufsize, const char *fmt, va_list args)
{
	NTSTATUS status;

	status = RtlStringCbVPrintfA(buf, bufsize, fmt, args);
	if (status == STATUS_SUCCESS)
		return strlen(buf);

	return 0;
}


