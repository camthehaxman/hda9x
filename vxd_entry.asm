; Defines the VxD Device Descriptor Block

.386p  ; Allow use of 386 and privileged instructions

DDK_VERSION EQU 0400h
UNDEFINED_DEVICE_ID  EQU 00000H
UNDEFINED_INIT_ORDER EQU 080000000H

VxD_Desc_Block STRUC
	DDB_Next                DD ?
	DDB_SDK_Version         DW DDK_VERSION
	DDB_Req_Device_Number   DW UNDEFINED_DEVICE_ID
	DDB_Dev_Major_Version   DB 0
	DDB_Dev_Minor_Version   DB 0
	DDB_Flags               DW 0
	DDB_Name                DB "        "
	DDB_Init_Order          DD UNDEFINED_INIT_ORDER
	DDB_Control_Proc        DD ?
	DDB_V86_API_Proc        DD 0
	DDB_PM_API_Proc         DD 0
	DDB_V86_API_CSIP        DD 0
	DDB_PM_API_CSIP         DD     0
	DDB_Reference_Data      DD ?
	DDB_Service_Table_Ptr   DD 0
	DDB_Service_Table_Size  DD 0
	DDB_Win32_Service_Table DD 0
	DDB_Prev                DD 'Prev'
	DDB_Size                DD SIZE(VxD_Desc_Block)
	DDB_Reserved1           DD 'Rsv1'
	DDB_Reserved2           DD 'Rsv2'
	DDB_Reserved3           DD 'Rsv3'
VxD_Desc_Block ENDS


EXTERN hda_vxd_control_proc_:NEAR
EXTERN _hda_vxd_pm16_api_proc:NEAR


_LDATA SEGMENT DWORD PUBLIC USE32 'LCODE'
ALIGN 4

PUBLIC HDAUDIO_DDB
HDAUDIO_DDB VxD_Desc_Block <,,
	UNDEFINED_DEVICE_ID,           ; Device ID
	DRV_VER_MAJOR, DRV_VER_MINOR,  ; Version
	,
	"HDAUDIO ",                    ; Name
	UNDEFINED_INIT_ORDER,          ; Initialization order
	OFFSET hda_vxd_control_proc_,  ; Control procedure
	,                              ; V86 API procedure
	OFFSET MyVxD_PM_Api,           ; 16-bit protected mode API procedure
	,,,,>

_LDATA ENDS


_LTEXT SEGMENT DWORD PUBLIC USE32 'LCODE'

MyVxD_PM_Api PROC
	pushad

	; ebx = VM handle, ebp = client regs struct
	push ebp
	push ebx
	call _hda_vxd_pm16_api_proc
	add esp, 8

	popad
	ret
MyVxD_PM_Api ENDP

_LTEXT ENDS

END
