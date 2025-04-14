#pragma once

#include <vmm.h>

#pragma pack(push)
#pragma pack(1)

#define	DLVXD_LOAD_ENUMERATOR 0  // We loaded DLVxD as an enumerator.
#define	DLVXD_LOAD_DEVLOADER  1  // We loaded DLVxD as a devloader.
#define	DLVXD_LOAD_DRIVER     2  // We loaded DLVxD as a device driver.
#define	NUM_DLVXD_LOAD_TYPE   3  // Number of DLVxD load type.

typedef DWORD CONFIGRET;

// CONFIGRET values

#define CR_SUCCESS              0x00000000
#define CR_DEFAULT              0x00000001
#define CR_OUT_OF_MEMORY        0x00000002
#define CR_INVALID_POINTER      0x00000003
#define CR_INVALID_FLAG         0x00000004
#define CR_INVALID_DEVNODE      0x00000005
#define CR_INVALID_RES_DES      0x00000006
#define CR_INVALID_LOG_CONF     0x00000007
#define CR_INVALID_ARBITRATOR   0x00000008
#define CR_INVALID_NODELIST     0x00000009
#define CR_DEVNODE_HAS_REQS     0x0000000A
#define CR_INVALID_RESOURCEID   0x0000000B
#define CR_DLVXD_NOT_FOUND      0x0000000C
#define CR_NO_SUCH_DEVNODE      0x0000000D
#define CR_NO_MORE_LOG_CONF     0x0000000E
#define CR_NO_MORE_RES_DES      0x0000000F
#define CR_ALREADY_SUCH_DEVNODE 0x00000010
#define CR_INVALID_RANGE_LIST   0x00000011
#define CR_INVALID_RANGE        0x00000012
#define CR_FAILURE              0x00000013
#define CR_NO_SUCH_LOGICAL_DEV  0x00000014
#define CR_CREATE_BLOCKED       0x00000015
#define CR_NOT_SYSTEM_VM        0x00000016
#define CR_REMOVE_VETOED        0x00000017
#define CR_APM_VETOED           0x00000018
#define CR_INVALID_LOAD_TYPE    0x00000019
#define CR_BUFFER_SMALL         0x0000001A
#define CR_NO_ARBITRATOR        0x0000001B
#define CR_NO_REGISTRY_HANDLE   0x0000001C
#define CR_REGISTRY_ERROR       0x0000001D
#define CR_INVALID_DEVICE_ID    0x0000001E
#define CR_INVALID_DATA         0x0000001F
#define CR_INVALID_API          0x00000020
#define CR_DEVLOADER_NOT_READY  0x00000021
#define CR_NEED_RESTART         0x00000022
#define CR_NO_MORE_HW_PROFILES  0x00000023
#define CR_DEVICE_NOT_THERE     0x00000024
#define CR_NO_SUCH_VALUE        0x00000025
#define CR_WRONG_TYPE           0x00000026
#define CR_INVALID_PRIORITY     0x00000027
#define CR_NOT_DISABLEABLE      0x00000028
#define CR_FREE_RESOURCES       0x00000029
#define CR_QUERY_VETOED         0x0000002A
#define CR_CANT_SHARE_IRQ       0x0000002B
#define NUM_CR_RESULTS          0x0000002C

typedef	ULONG CONFIGFUNC;

// CONFIGFUNC values

#define CONFIG_FILTER         0x00000000  // Ancestors must filter requirements.
#define CONFIG_START          0x00000001  // Devnode dynamic initialization.
#define CONFIG_STOP           0x00000002  // Devnode must stop using config.
#define CONFIG_TEST           0x00000003  // Can devnode change state now.
#define CONFIG_REMOVE         0x00000004  // Devnode must stop using config.
#define CONFIG_ENUMERATE      0x00000005  // Devnode must enumerated.
#define CONFIG_SETUP          0x00000006  // Devnode should download driver.
#define CONFIG_CALLBACK       0x00000007  // Devnode is being called back.
#define CONFIG_APM            0x00000008  // APM functions.
#define CONFIG_TEST_FAILED    0x00000009  // Continue as before after a TEST.
#define CONFIG_TEST_SUCCEEDED 0x0000000A  // Prepare for the STOP/REMOVE.
#define CONFIG_VERIFY_DEVICE  0x0000000B  // Insure the legacy card is there.
#define CONFIG_PREREMOVE      0x0000000C  // Devnode must stop using config.
#define CONFIG_SHUTDOWN       0x0000000D  // We are shutting down.
#define CONFIG_PREREMOVE2     0x0000000E  // Devnode must stop using config.
#define CONFIG_READY          0x0000000F  // The devnode has been setup.
#define CONFIG_PROP_CHANGE    0x00000010  // The property page is exiting.
#define CONFIG_PRIVATE        0x00000011  // Someone called Call_Handler.
#define CONFIG_PRESHUTDOWN    0x00000012  // We are shutting down

typedef	ULONG SUBCONFIGFUNC;

// SUBCONFIGFUNC values

#define CONFIG_START_DYNAMIC_START        0x00000000
#define CONFIG_START_FIRST_START          0x00000001

#define CONFIG_STOP_DYNAMIC_STOP          0x00000000
#define CONFIG_STOP_HAS_PROBLEM           0x00000001

// For both CONFIG_REMOVE, CONFIG_PREREMOVE and CONFIG_POSTREMOVE
#define CONFIG_REMOVE_DYNAMIC             0x00000000
#define CONFIG_REMOVE_SHUTDOWN            0x00000001
#define CONFIG_REMOVE_REBOOT              0x00000002

#define CONFIG_TEST_CAN_STOP              0x00000000
#define CONFIG_TEST_CAN_REMOVE            0x00000001

#define CONFIG_APM_TEST_STANDBY           0x00000000
#define CONFIG_APM_TEST_SUSPEND           0x00000001
#define CONFIG_APM_TEST_STANDBY_FAILED    0x00000002
#define CONFIG_APM_TEST_SUSPEND_FAILED    0x00000003
#define CONFIG_APM_TEST_STANDBY_SUCCEEDED 0x00000004
#define CONFIG_APM_TEST_SUSPEND_SUCCEEDED 0x00000005
#define CONFIG_APM_RESUME_STANDBY         0x00000006
#define CONFIG_APM_RESUME_SUSPEND         0x00000007
#define CONFIG_APM_RESUME_CRITICAL        0x00000008
#define CONFIG_APM_UI_ALLOWED             0x80000000

typedef DWORD DEVNODE;

#define	MAX_MEM_REGISTERS  9
#define	MAX_IO_PORTS      20
#define	MAX_IRQS           7
#define	MAX_DMA_CHANNELS   7

// Config buffer info
typedef struct Config_Buff_s
{
	WORD  wNumMemWindows;                 // Num memory windows
	DWORD dMemBase[MAX_MEM_REGISTERS];    // Memory window base
	DWORD dMemLength[MAX_MEM_REGISTERS];  // Memory window length
	WORD  wMemAttrib[MAX_MEM_REGISTERS];  // Memory window Attrib
	WORD  wNumIOPorts;                    // Num IO ports
	WORD  wIOPortBase[MAX_IO_PORTS];      // I/O port base
	WORD  wIOPortLength[MAX_IO_PORTS];    // I/O port length
	WORD  wNumIRQs;                       // Num IRQ info
	BYTE  bIRQRegisters[MAX_IRQS];        // IRQ list
	BYTE  bIRQAttrib[MAX_IRQS];           // IRQ Attrib list
	WORD  wNumDMAs;                       // Num DMA channels
	BYTE  bDMALst[MAX_DMA_CHANNELS];      // DMA list
	WORD  wDMAAttrib[MAX_DMA_CHANNELS];   // DMA Attrib list
	BYTE  bReserved1[3];                  // Reserved
} CMCONFIG, *PCMCONFIG;

//------------------------------------------------------------------------------
// VxD services
//------------------------------------------------------------------------------

#define SVC_CM_Get_Version              VXD_SERVICE(CONFIGMG_DEVICE_ID,  0)
#define SVC_CM_Initialize               VXD_SERVICE(CONFIGMG_DEVICE_ID,  1)
#define SVC_CM_Locate_DevNode           VXD_SERVICE(CONFIGMG_DEVICE_ID,  2)
#define SVC_CM_Get_Parent               VXD_SERVICE(CONFIGMG_DEVICE_ID,  3)
#define SVC_CM_Get_Child                VXD_SERVICE(CONFIGMG_DEVICE_ID,  4)
#define SVC_CM_Get_Sibling              VXD_SERVICE(CONFIGMG_DEVICE_ID,  5)
#define SVC_CM_Register_Device_Driver   VXD_SERVICE(CONFIGMG_DEVICE_ID, 14)
#define SVC_CM_Get_Alloc_Log_Conf       VXD_SERVICE(CONFIGMG_DEVICE_ID, 59)
#define SVC_CM_Call_Enumerator_Function VXD_SERVICE(CONFIGMG_DEVICE_ID, 84)

#define	CM_GET_ALLOC_LOG_CONF_ALLOC	     0x00000000
#define	CM_GET_ALLOC_LOG_CONF_BOOT_ALLOC 0x00000001
#define	CM_GET_ALLOC_LOG_CONF_BITS       0x00000001

CONFIGRET __declspec(naked) __cdecl
CM_Get_Alloc_Log_Conf(PCMCONFIG pccBuffer, DEVNODE dnDevNode, ULONG ulFlags)
{
	VxDJmp(SVC_CM_Get_Alloc_Log_Conf)
}

#pragma pack(pop)
