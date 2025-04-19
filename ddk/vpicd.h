#pragma once

#include <vmm.h>

#define VPICD_OPT_READ_HW_IRR          0x01
#define VPICD_OPT_READ_HW_IRR_BIT         0
#define VPICD_OPT_CAN_SHARE            0x02
#define VPICD_OPT_CAN_SHARE_BIT           1
#define VPICD_OPT_REF_DATA             0x04  // new for 4.0
#define VPICD_OPT_REF_DATA_BIT            2
#define VPICD_OPT_VIRT_INT_REJECT      0x10  // new for 4.0
#define VPICD_OPT_VIRT_INT_REJECT_BIT     4
#define VPICD_OPT_SHARE_PMODE_ONLY     0x20  // new for 4.0
#define VPICD_OPT_SHARE_PMODE_ONLY_BIT    5
#define VPICD_OPT_ALL                  0x3F  // Internal use

typedef struct VPICD_IRQ_Descriptor
{
	USHORT VID_IRQ_Number;
	USHORT VID_Options;          // INIT<0>
	ULONG VID_Hw_Int_Proc;
	ULONG VID_Virt_Int_Proc;     // INIT<0>
	ULONG VID_EOI_Proc;          // INIT<0>
	ULONG VID_Mask_Change_Proc;  // INIT<0>
	ULONG VID_IRET_Proc;         // INIT<0>
	ULONG VID_IRET_Time_Out;     // INIT<500>
	ULONG VID_Hw_Int_Ref;        // new for 4.0
} VID, *PVID;

typedef ULONG HIRQ;  // IRQ Handle

//------------------------------------------------------------------------------
// VxD services
//------------------------------------------------------------------------------

#define SVC_VPICD_Virtualize_IRQ    VXD_SERVICE(VPICD_DEVICE_ID, 1)
#define SVC_VPICD_Phys_EOI          VXD_SERVICE(VPICD_DEVICE_ID, 4)
#define SVC_VPICD_Physically_Mask   VXD_SERVICE(VPICD_DEVICE_ID, 8)
#define SVC_VPICD_Physically_Unmask VXD_SERVICE(VPICD_DEVICE_ID, 9)

static HIRQ __declspec(naked)
VPICD_Virtualize_IRQ(PVID pvid)
{
	VxDCall(SVC_VPICD_Virtualize_IRQ)
	__asm {
		jnc no_error
		xor eax, eax
	no_error:
		ret
	}
}
#pragma aux VPICD_Virtualize_IRQ \
	__parm [edi] \
	__value [eax]

static void __declspec(naked)
VPICD_Phys_EOI(HIRQ hirq)
{
	VxDJmp(SVC_VPICD_Phys_EOI)
}
#pragma aux VPICD_Phys_EOI \
	__parm [eax]

static void __declspec(naked)
VPICD_Physically_Mask(HIRQ hirq)
{
	VxDJmp(SVC_VPICD_Physically_Mask)
}
#pragma aux VPICD_Physically_Mask \
	__parm [eax]

static void __declspec(naked)
VPICD_Physically_Unmask(HIRQ hirq)
{
	VxDJmp(SVC_VPICD_Physically_Unmask)
}
#pragma aux VPICD_Physically_Unmask \
	__parm [eax]
