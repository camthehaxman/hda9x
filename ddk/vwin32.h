#pragma once

#include <vmm.h>

// for DeviceIOControl support
// On a DeviceIOControl call vWin32 will pass following parameters to
// the Vxd that is specified by hDevice. hDevice is obtained thru an
// earlier call to hDevice = CreateFile("\\.\vxdname", ...);
// ESI = DIOCParams STRUCT (defined below)
typedef struct DIOCParams
{
	DWORD Internal1;        // ptr to client regs
	DWORD VMHandle;         // VM handle
	DWORD Internal2;        // DDB
	DWORD dwIoControlCode;
	DWORD lpvInBuffer;
	DWORD cbInBuffer;
	DWORD lpvOutBuffer;
	DWORD cbOutBuffer;
	DWORD lpcbBytesReturned;
	DWORD lpoOverlapped;
	DWORD hDevice;
	DWORD tagProcess;
} DIOCPARAMETERS;
