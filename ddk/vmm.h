#pragma once

#include <windows.h>
#include <missing_typedefs.h>

typedef ULONG HVM;

// Client register representations for VxDs implementing a 16-bit protected mode
// API. The EBP register points to this structure upon entry to the API proc.

struct Client_Reg_Struc
{
	ULONG Client_EDI;
	ULONG Client_ESI;
	ULONG Client_EBP;
	ULONG Client_res0;
	ULONG Client_EBX;
	ULONG Client_EDX;
	ULONG Client_ECX;
	ULONG Client_EAX;
	ULONG Client_Error;
	ULONG Client_EIP;
	USHORT Client_CS;
	USHORT Client_res1;
	ULONG Client_EFlags;
	ULONG Client_ESP;
	USHORT Client_SS;
	USHORT Client_res2;
	USHORT Client_ES;
	USHORT Client_res3;
	USHORT Client_DS;
	USHORT Client_res4;
	USHORT Client_FS;
	USHORT Client_res5;
	USHORT Client_GS;
	USHORT Client_res6;
	ULONG Client_Alt_EIP;
	USHORT Client_Alt_CS;
	USHORT Client_res7;
	ULONG Client_Alt_EFlags;
	ULONG Client_Alt_ESP;
	USHORT Client_Alt_SS;
	USHORT Client_res8;
	USHORT Client_Alt_ES;
	USHORT Client_res9;
	USHORT Client_Alt_DS;
	USHORT Client_res10;
	USHORT Client_Alt_FS;
	USHORT Client_res11;
	USHORT Client_Alt_GS;
	USHORT Client_res12;
};

struct Client_Word_Reg_Struc
{
	USHORT Client_DI;
	USHORT Client_res13;
	USHORT Client_SI;
	USHORT Client_res14;
	USHORT Client_BP;
	USHORT Client_res15;
	ULONG Client_res16;
	USHORT Client_BX;
	USHORT Client_res17;
	USHORT Client_DX;
	USHORT Client_res18;
	USHORT Client_CX;
	USHORT Client_res19;
	USHORT Client_AX;
	USHORT Client_res20;
	ULONG Client_res21;
	USHORT Client_IP;
	USHORT Client_res22;
	ULONG Client_res23;
	USHORT Client_Flags;
	USHORT Client_res24;
	USHORT Client_SP;
	USHORT Client_res25;
	ULONG Client_res26[5];
	USHORT Client_Alt_IP;
	USHORT Client_res27;
	ULONG Client_res28;
	USHORT Client_Alt_Flags;
	USHORT Client_res29;
	USHORT Client_Alt_SP;
};

struct Client_Byte_Reg_Struc
{
	ULONG Client_res30[4];
	UCHAR Client_BL;
	UCHAR Client_BH;
	USHORT Client_res31;
	UCHAR Client_DL;
	UCHAR Client_DH;
	USHORT Client_res32;
	UCHAR Client_CL;
	UCHAR Client_CH;
	USHORT Client_res33;
	UCHAR Client_AL;
	UCHAR Client_AH;
};

typedef union tagCLIENT_STRUC
{
	struct Client_Reg_Struc      CRS;
	struct Client_Word_Reg_Struc CWRS;
	struct Client_Byte_Reg_Struc CBRS;
} CLIENT_STRUCT;

typedef struct Client_Reg_Struc CRS;
typedef CRS *PCRS;

//------------------------------------------------------------------------------
// VxD system control messages
//------------------------------------------------------------------------------

#define SYS_CRITICAL_INIT       0x0000
#define DEVICE_INIT             0x0001
#define INIT_COMPLETE           0x0002
#define SYS_VM_INIT             0x0003
#define SYS_VM_TERMINATE        0x0004
#define SYSTEM_EXIT             0x0005
#define SYS_CRITICAL_EXIT       0x0006
#define CREATE_VM               0x0007
#define VM_CRITICAL_INIT        0x0008
#define VM_INIT                 0x0009
#define VM_TERMINATE            0x000A
#define VM_NOT_EXECUTEABLE      0x000B
#define DESTROY_VM              0x000C
#define VM_SUSPEND              0x000D
#define VM_RESUME               0x000E
#define SET_DEVICE_FOCUS        0x000F
#define BEGIN_MESSAGE_MODE      0x0010
#define END_MESSAGE_MODE        0x0011
#define REBOOT_PROCESSOR        0x0012
#define QUERY_DESTROY           0x0013
#define DEBUG_QUERY             0x0014
#define BEGIN_PM_APP            0x0015
#define END_PM_APP              0x0016
#define DEVICE_REBOOT_NOTIFY    0x0017
#define CRIT_REBOOT_NOTIFY      0x0018
#define CLOSE_VM_NOTIFY         0x0019
#define POWER_EVENT             0x001A
#define SYS_DYNAMIC_DEVICE_INIT 0x001B
#define SYS_DYNAMIC_DEVICE_EXIT 0x001C
#define CREATE_THREAD           0x001D
#define THREAD_INIT             0x001E
#define TERMINATE_THREAD        0x001F
#define THREAD_Not_Executeable  0x0020
#define DESTROY_THREAD          0x0021
#define PNP_NEW_DEVNODE         0x0022
#define W32_DEVICEIOCONTROL     0x0023
#define SYS_VM_TERMINATE2       0x0024
#define SYSTEM_EXIT2            0x0025
#define SYS_CRITICAL_EXIT2      0x0026
#define VM_TERMINATE2           0x0027
#define VM_NOT_EXECUTEABLE2     0x0028
#define DESTROY_VM2             0x0029
#define VM_SUSPEND2             0x002A
#define END_MESSAGE_MODE2       0x002B
#define END_PM_APP2             0x002C
#define DEVICE_REBOOT_NOTIFY2   0x002D
#define CRIT_REBOOT_NOTIFY2     0x002E
#define CLOSE_VM_NOTIFY2        0x002F
#define GET_CONTENTION_HANDLER  0x0030
#define KERNEL32_INITIALIZED    0x0031
#define KERNEL32_SHUTDOWN       0x0032
#define MAX_SYSTEM_CONTROL      0x0032

//------------------------------------------------------------------------------
// VMM services
//------------------------------------------------------------------------------

// Device IDs for standard VxDs

#define UNDEFINED_DEVICE_ID 0x00000
#define VMM_DEVICE_ID       0x00001 /* Used for dynalink table */
#define DEBUG_DEVICE_ID     0x00002
#define VPICD_DEVICE_ID     0x00003
#define VDMAD_DEVICE_ID     0x00004
#define VTD_DEVICE_ID       0x00005
#define V86MMGR_DEVICE_ID   0x00006
#define PAGESWAP_DEVICE_ID  0x00007
#define PARITY_DEVICE_ID    0x00008
#define REBOOT_DEVICE_ID    0x00009
#define VDD_DEVICE_ID       0x0000A
#define VSD_DEVICE_ID       0x0000B
#define VMD_DEVICE_ID       0x0000C
#define VKD_DEVICE_ID       0x0000D
#define VCD_DEVICE_ID       0x0000E
#define VPD_DEVICE_ID       0x0000F
#define BLOCKDEV_DEVICE_ID  0x00010
#define VMCPD_DEVICE_ID     0x00011
#define EBIOS_DEVICE_ID     0x00012
#define BIOSXLAT_DEVICE_ID  0x00013
#define VNETBIOS_DEVICE_ID  0x00014
#define DOSMGR_DEVICE_ID    0x00015
#define WINLOAD_DEVICE_ID   0x00016
#define SHELL_DEVICE_ID     0x00017
#define VMPOLL_DEVICE_ID    0x00018
#define VPROD_DEVICE_ID     0x00019
#define DOSNET_DEVICE_ID    0x0001A
#define VFD_DEVICE_ID       0x0001B
#define VDD2_DEVICE_ID      0x0001C /* Secondary display adapter */
#define WINDEBUG_DEVICE_ID  0x0001D
#define TSRLOAD_DEVICE_ID   0x0001E /* TSR instance utility ID */
#define BIOSHOOK_DEVICE_ID  0x0001F /* Bios interrupt hooker VxD */
#define INT13_DEVICE_ID     0x00020
#define PAGEFILE_DEVICE_ID  0x00021 /* Paging File device */
#define SCSI_DEVICE_ID      0x00022 /* SCSI device */
#define MCA_POS_DEVICE_ID   0x00023 /* MCA_POS device */
#define SCSIFD_DEVICE_ID    0x00024 /* SCSI FastDisk device */
#define VPEND_DEVICE_ID     0x00025 /* Pen device */
#define APM_DEVICE_ID       0x00026 /* Power Management device */
#define VPOWERD_DEVICE_ID   APM_DEVICE_ID   /* We overload APM since we replace it */
#define VXDLDR_DEVICE_ID    0x00027 /* VxD Loader device */
#define NDIS_DEVICE_ID      0x00028 /* NDIS wrapper */
#define BIOS_EXT_DEVICE_ID  0x00029 /* Fix Broken BIOS device */
#define VWIN32_DEVICE_ID    0x0002A /* for new WIN32-VxD */
#define VCOMM_DEVICE_ID     0x0002B /* New COMM device driver */
#define SPOOLER_DEVICE_ID   0x0002C /* Local Spooler */
#define WIN32S_DEVICE_ID    0x0002D /* Win32S on Win 3.1 driver */
#define DEBUGCMD_DEVICE_ID  0x0002E /* Debug command extensions */
#define CONFIGMG_DEVICE_ID  0x00033 /* Configuration manager (Plug&Play) */
#define DWCFGMG_DEVICE_ID   0x00034 /* Configuration manager for win31 and DOS */
#define SCSIPORT_DEVICE_ID  0x00035 /* Dragon miniport loader/driver */
#define VFBACKUP_DEVICE_ID  0x00036 /* allows backup apps to work with NEC */
#define ENABLE_DEVICE_ID    0x00037 /* for access VxD */
#define VCOND_DEVICE_ID     0x00038 /* Virtual Console Device - check vcond.inc */
#define ISAPNP_DEVICE_ID    0x0003C /* ISA P&P Enumerator */
#define BIOS_DEVICE_ID      0x0003D /* BIOS P&P Enumerator */
#define IFSMgr_Device_ID    0x00040 /* Installable File System Manager */
#define VCDFSD_DEVICE_ID    0x00041 /* Static CDFS ID */
#define MRCI2_DEVICE_ID     0x00042 /* DrvSpace compression engine */
#define PCI_DEVICE_ID       0x00043 /* PCI P&P Enumerator */
#define PELOADER_DEVICE_ID  0x00044 /* PE Image Loader */
#define EISA_DEVICE_ID      0x00045 /* EISA P&P Enumerator */
#define DRAGCLI_DEVICE_ID   0x00046 /* Dragon network client */
#define DRAGSRV_DEVICE_ID   0x00047 /* Dragon network server */
#define PERF_DEVICE_ID	    0x00048 /* Config/stat info */
#define AWREDIR_DEVICE_ID   0x00049 /* AtWork Network FSD */

// Trampoline functions for various VxD services that allow them to easily be
// called from C code

#define VXD_SERVICE(vxdDevID, serviceNum) ((vxdDevID) * 0x10000 + (serviceNum))

// Calling a function in another VxD is done by executing an "int 20h" followed
// by a 32-bit value consisting of the VxD device ID in the upper 16 bits and the
// service number in the lower 16 bits (see the VXD_SERVICE macro). At runtime when this interrupt is first
// executed, the VMM patches this whole thing with a direct call to the address
// of the target function so that further calls simply go to that.
#define VxDCall(service) \
	__asm int 0x20 \
	__asm dd (service)

// Jumping to a function in another VxD is just like calling one except that the
// value has bit 15 set.
#define VxDJmp(service) \
	__asm int 0x20 \
	__asm dd ((service) + 0x8000)

// There really is no standard calling convention for these VxD services. Some
// of them follow the standard C __cdecl calling convention, while others take
// arguments in registers and return values in registers besides eax.
// The __declspec(naked) attribute is used here to remove the usual function
// prologues and epilogues, since these trampoline functions are written purely in
// embedded assembly, and any compiler-generated prologues and epilogues would interfere.
//
// For services that use the __cdecl calling convention, they are easily implemented
// here as a simple VxDJmp. Calling the trampoline functions in this file pushes
// the return address onto the stack, so the trampoline jumps to the actual VxD
// service, and when it returns, it returns back to the original caller. By
// convention, most of the functions using __cdecl have names that begin with an
// underscore, though there are a few exceptions.
//
// For services that use registers instead, we make use of OpenWatcom's
// auxiliary pragma to specify which arguments are in registers, which registers
// are clobbered, and which registers contain the return value.

#define SVC_Get_VMM_Version   VXD_SERVICE(VMM_DEVICE_ID,   0)
#define SVC_Get_Cur_VM_Handle VXD_SERVICE(VMM_DEVICE_ID,   1)
#define SVC_Map_Flat          VXD_SERVICE(VMM_DEVICE_ID,  28)
#define SVC__HeapAllocate     VXD_SERVICE(VMM_DEVICE_ID,  79)
#define SVC__HeapReAllocate   VXD_SERVICE(VMM_DEVICE_ID,  80)
#define SVC__HeapFree         VXD_SERVICE(VMM_DEVICE_ID,  81)
#define SVC__PageAllocate     VXD_SERVICE(VMM_DEVICE_ID,  83)
#define SVC__MapPhysToLinear  VXD_SERVICE(VMM_DEVICE_ID, 108)
#define SVC_Out_Debug_String  VXD_SERVICE(VMM_DEVICE_ID, 194)
#define SVC_Out_Debug_Chr     VXD_SERVICE(VMM_DEVICE_ID, 195)

static VOID __declspec(naked)
Out_Debug_String(const char *s)
{
	__asm pushad
	VxDCall(SVC_Out_Debug_String)
	__asm popad
}
#pragma aux Out_Debug_String \
	__parm [esi]

static VOID __declspec(naked)
Out_Debug_Chr(char c)
{
	VxDJmp(SVC_Out_Debug_Chr)
}
#pragma aux Out_Debug_Chr \
	__parm [eax] \
	__modify [eax]
