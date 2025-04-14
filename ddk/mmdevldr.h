#pragma once

#include <vmm.h>

//------------------------------------------------------------------------------
// VxD services
//------------------------------------------------------------------------------

#define MMDEVLDR_DEVICE_ID 0x44A

#define SVC_MMDEVLDR_Register_Device_Driver VXD_SERVICE(MMDEVLDR_DEVICE_ID, 0)

static void __declspec(naked)
MMDEVLDR_Register_Device_Driver(DEVNODE dnDevNode, DWORD fnConfigHandler, DWORD dwUserData)
{
	VxDJmp(SVC_MMDEVLDR_Register_Device_Driver)
}
#pragma aux MMDEVLDR_Register_Device_Driver \
	__parm [eax] [ebx] [ecx] \
	__modify [ebx]
