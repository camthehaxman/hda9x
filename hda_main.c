#include <windows.h>
#include <vmm.h>
#include <configmg.h>
#include <mmdevldr.h>

#include "hdaudio.h"

// Callback for MMDEVLDR and CONFIGMG
static CONFIGRET __cdecl driver_config_handler(CONFIGFUNC func, SUBCONFIGFUNC subfunc, DEVNODE devnode, DWORD dwRefData, ULONG ulFlags)
{
	dprintf("driver_config_handler(func=0x%X, subfunc=0x%X, devnode=0x%X, dwRefData=0x%X, ulFlags=0x%X\n",
		func, subfunc, devnode, dwRefData, ulFlags);

	CMCONFIG conf;
	CONFIGRET result;

	switch (func)
	{
	case CONFIG_FILTER:
		return CR_SUCCESS;
	case CONFIG_START:
		result = CM_Get_Alloc_Log_Conf(&conf, devnode, CM_GET_ALLOC_LOG_CONF_ALLOC);
		if (result != CR_SUCCESS)
		{
			dprintf("failed to get device configuration: result %u\n", result);
			return result;
		}

		dprintf("Config information:\n");
		dprintf("  memory windows:\n");
		for (int i = 0; i < conf.wNumMemWindows; i++)
			dprintf("    base=0x%08X, len=0x%08X, attrib=0x%X\n", conf.dMemBase[i], conf.dMemLength[i], conf.wMemAttrib[i]);
		dprintf("  I/O ports:\n");
		for (int i = 0; i < conf.wNumIOPorts; i++)
			dprintf("    base=0x%04X, len=0x%04X\n", conf.wIOPortBase[i], conf.wIOPortLength[i]);
		dprintf("  IRQs:\n");
		for (int i = 0; i < conf.wNumIRQs; i++)
			dprintf("    reg=0x%02X, attrib=0x%X\n", conf.bIRQRegisters[i], conf.bIRQAttrib[i]);
		dprintf("  DMAs:\n");
		for (int i = 0; i < conf.wNumDMAs; i++)
			dprintf("    chan=%i, attrib=0x%X\n", conf.bDMALst[i], conf.wDMAAttrib[i]);

		return CR_SUCCESS;
	}
	return CR_DEFAULT;
}

// Handler for system control messages
// Returns 0 on error
DWORD __cdecl hda_vxd_control_proc(DWORD msg, DWORD paramEBX, DWORD paramEDX)
{
	hda_debug_init();

	char *name = "(unknown)";
	switch(msg)
	{
#define GET_NAME(x) case x: name = #x; break;
	GET_NAME(SYS_CRITICAL_INIT)
	GET_NAME(DEVICE_INIT)
	GET_NAME(INIT_COMPLETE)
	GET_NAME(SYS_VM_INIT)
	GET_NAME(SET_DEVICE_FOCUS)
	GET_NAME(BEGIN_PM_APP)
	GET_NAME(DESTROY_THREAD)
	GET_NAME(POWER_EVENT)
	GET_NAME(SYS_DYNAMIC_DEVICE_INIT)
	GET_NAME(SYS_DYNAMIC_DEVICE_EXIT)
	GET_NAME(THREAD_Not_Executeable)
	GET_NAME(CREATE_THREAD)
	GET_NAME(THREAD_INIT)
	GET_NAME(TERMINATE_THREAD)
	GET_NAME(PNP_NEW_DEVNODE)
	GET_NAME(KERNEL32_INITIALIZED)
#undef GET_NAME
	}
	dprintf("hda_vxd_control_proc(msg=%s (0x%X), paramEBX=0x%X, paramEDX=0x%X)\n",
		name, msg, paramEBX, paramEDX);

	switch (msg)
	{
	case PNP_NEW_DEVNODE:
		if (paramEDX == DLVXD_LOAD_DRIVER)
		{
			// Register ourselves as the driver
			dprintf("registering driver\n");
			MMDEVLDR_Register_Device_Driver(
				paramEBX,  // dnDevNode
				(DWORD)driver_config_handler,  // fnConfigHandler
				0);  // dwUserData
		}
		break;
	}

	return 1;
}

void __cdecl hda_vxd_pm16_api_proc(HVM hVM, CLIENT_STRUCT *clientRegs)
{

}
