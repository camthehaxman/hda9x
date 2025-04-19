#pragma once

// 16-bit protected mode API
// All of these function codes are passed in the AX register.
// On return, the AL register contains 1 if succeeded or 0 on failure.
// Parameters are passed in other registers depending on the function.

// Fills out a WAVEOUTCAPS structure
// Parameters:
//   ES:SI - pointer to WAVEOUTCAPS structure
#define HDA_VXD_GET_CAPABILITIES  1

// Opens an output stream
// Parameters:
//   ES:SI - pointer to PCMWAVEFORMAT structure
#define HDA_VXD_OPEN_STREAM       2

// Closes an output stream
#define HDA_VXD_CLOSE_STREAM      3

// Submits a sound block for playback
// Parameters:
//   ES:SI - pointer to WAVEHDR
#define HDA_VXD_SUBMIT_WAVE_BLOCK 4

// Win32 API
#define HDA_VXD_GET_PCI_CONFIG      5
#define HDA_VXD_EXEC_VERB           6
#define HDA_VXD_GET_BASE_REGS       7
#define HDA_VXD_GET_STREAM_DESC(n)  (8 + (n))

#ifndef __386__
typedef void (FAR *VxDAPIEntry)(void);

static VxDAPIEntry hda_vxd_get_entry_point(void)
{
	// The VxD name is always 8 characters, padded with spaces
	static const char *vxdName = "HDAUDIO ";
	VxDAPIEntry *entry;

	__asm {
		les di, vxdName
		mov ax, 0x1684
		int 0x2F
		mov WORD PTR entry+2, es
		mov WORD PTR entry,   di
	}
	return entry;
}

static BYTE hda_vxd_get_capabilities(VxDAPIEntry entry, WAVEOUTCAPS FAR *wc)
{
	__asm {
		les si, wc
		mov ax, HDA_VXD_GET_CAPABILITIES
		call DWORD PTR entry
	}
}

static BYTE hda_vxd_open_stream(VxDAPIEntry entry, const PCMWAVEFORMAT FAR *wavFmt)
{
	__asm {
		les si, wavFmt
		mov ax, HDA_VXD_OPEN_STREAM
		call DWORD PTR entry
	}
}

static BYTE hda_vxd_close_stream(VxDAPIEntry entry)
{
	__asm {
		mov ax, HDA_VXD_CLOSE_STREAM
		call DWORD PTR entry
	}
}

static BYTE hda_vxd_submit_wave_block(VxDAPIEntry entry, WAVEHDR FAR *wavHdr)
{
	__asm {
		les si, wavHdr
		mov ax, HDA_VXD_SUBMIT_WAVE_BLOCK
		call DWORD PTR entry
	}
}
#endif
