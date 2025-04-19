#pragma once

#include <vmm.h>

typedef DWORD SHELL_HINSTANCE;
typedef DWORD APPY_HANDLE;
typedef VOID (__cdecl *APPY_CALLBACK)(DWORD dwRefData);

//------------------------------------------------------------------------------
// VxD services
//------------------------------------------------------------------------------

#define SVC_SHELL_Get_Version            VXD_SERVICE(SHELL_DEVICE_ID,  0)
#define SVC_SHELL_QueryAppyTimeAvailable VXD_SERVICE(SHELL_DEVICE_ID, 13)
#define SVC_SHELL_CallAtAppyTime         VXD_SERVICE(SHELL_DEVICE_ID, 14)
#define SVC_SHELL_LoadLibrary            VXD_SERVICE(SHELL_DEVICE_ID, 21)
#define SVC_SHELL_FreeLibrary            VXD_SERVICE(SHELL_DEVICE_ID, 22)
#define SVC_SHELL_GetProcAddress         VXD_SERVICE(SHELL_DEVICE_ID, 23)
#define SVC_SHELL_CallDll                VXD_SERVICE(SHELL_DEVICE_ID, 24)

// Flags for SHELL_CallAtAppyTime
#define CAAFL_RING0     0x00000001  // Does not require GUI services
#define CAAFL_TIMEOUT   0x00000002  // Time out the event

static APPY_HANDLE __declspec(naked) __cdecl
_SHELL_CallAtAppyTime(APPY_CALLBACK pfnAppyCallBack, DWORD dwRefData, DWORD dwFlags, DWORD dwTimeout)
{
	VxDJmp(SVC_SHELL_CallAtAppyTime)
}

static DWORD __declspec(naked) __cdecl
_SHELL_CallDll(PCHAR lpszDll, PCHAR lpszProcName, DWORD cbArgs, PVOID lpvArgs)
{
	VxDJmp(SVC_SHELL_CallDll)
}
