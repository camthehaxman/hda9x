#pragma once

#include <vmm.h>

//------------------------------------------------------------------------------
// VxD services
//------------------------------------------------------------------------------

#define SVC_VTD_Get_Real_Time VXD_SERVICE(VTD_DEVICE_ID, 7)

unsigned long long __declspec(naked) __cdecl
VTD_Get_Real_Time(void)
{
	VxDJmp(SVC_VTD_Get_Real_Time)
}
