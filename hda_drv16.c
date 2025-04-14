// 16-bit userspace driver DLL (HDAUDIO.DRV) for High Definition Audio

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>

#include "tinyprintf.h"

// Attributes needed for a DLL export function.
// These are FAR calls, and the DS segment register must be loaded upon
// every call into this DLL.
#define DLL_EXPORT FAR PASCAL _loadds

// Debug print functions
#if DEBUG
#define dprintf(...) printf("hdaudio.drv: " __VA_ARGS__)
#else
#define dprintf(...)
#endif

static BOOL tprintfInitialized = FALSE;

// Prints a single character to the COM serial port
// Used by tinyprintf
static void putc(void *unused, char c)
{
	__asm {
		mov dx, 0x3F8
		mov al, c
		out dx, al
	}
}

// Called whenever this DLL is loaded
// Must return non-zero to continue loading
int DLL_EXPORT LibMain(HINSTANCE hInstance, WORD wDataSegment, WORD wHeapSize, LPSTR lpszCmdLine)
{
	if (!tprintfInitialized)
	{
		init_printf(NULL, putc);
		tprintfInitialized = TRUE;
	}
	dprintf("LibMain\n");
	return 1;
}

// "Windows Exit Procedure", called whenever this DLL is unloaded
int DLL_EXPORT WEP(int nParameter)
{
	return 1;
}

static const char *driver_proc_msg_name(WORD msg)
{
	static char buf[32];
	switch(msg)
	{
#define X(x) case x: return #x;
	X(DRV_LOAD)
	X(DRV_FREE)
	X(DRV_OPEN)
	X(DRV_CLOSE)
	X(DRV_ENABLE)
	X(DRV_DISABLE)
	X(DRV_QUERYCONFIGURE)
	X(DRV_CONFIGURE)
#undef X
	default:
		sprintf(buf, "(unknown 0x%X)", msg);
		return buf;
	}
}

// Main driver entry point
// Processes messages sent to by the system to the driver
// Parameters:
//   dwDriverID - Value to return in response to a DRV_OPEN message
//   hDriver    - Handle returned to the application by the driver interface
//   wMessage   - Requested action to be performed
//   dwParam1   - Message-specific parameter
//   dwParam2   - Message-specific parameter
// Return:
//   Defined separately for each message
DWORD DLL_EXPORT DriverProc(DWORD dwDriverID, HANDLE hDriver,
	WORD wMessage, DWORD dwParam1, DWORD dwParam2)
{
	dprintf("DriverProc(dwDriverID=0x%08lX, hDriver=0x%04X, wMessage=%s, dwParam1=0x%08lX, dwParam2=0x%08lX)\n",
		dwDriverID, hDriver, driver_proc_msg_name(wMessage), dwParam1, dwParam2);

	switch (wMessage)
	{
	case DRV_LOAD:
		// Sent to the driver when it is loaded. This will always be the first
		// message received.
		// Return non-zero for success.
		return 1;
	case DRV_FREE:
		// Sent to the driver when it is about to be discarded. This will always
		// be the last message received before being feed.
		// Return value is ignored.
		return 1;
	case DRV_OPEN:
		// Sent to the driver when it is opened.
		// Return???
		return 1;
	case DRV_CLOSE:
		// Sent to the driver when it is closed. Drivers are unloaded when the
		// close count reaches zero.
		// dwDriverID - driver identifier returned from corresponding DRV_OPEN
		// lParam1    - passed through from the drvOpen call
		// lParam2    - passed through from the drvOpen call
		// Return 0 to fail
		return 1;
	case DRV_ENABLE:
		// Sent to the driver when it is loaded or reloaded and whenever Windows
		// is enabled. Drivers should only hook interrupts or expect any part of
		// of the driver to be in memory between enable and disable messages
		// Return value is ignored.
		return 1;
	case DRV_DISABLE:
		// Sent to the driver before the driver is freed and whenever Windows is
		// disabled
		// Return value is ignored.
		return 1;
	case DRV_QUERYCONFIGURE:
		// Sent to the driver to determine whether it supports custom
		// configuration
		// Return non-zero if custom configuration is supported.
		return 0;  // we do not support this
	case DRV_CONFIGURE:
		// Sent to the driver to display a custom configuration dialog
		// lParam1 - parent window handle in lower word
		// lParam2 - passed from app, undefined
		// Return value is REBOOT, OK, or RESTART
		return 0;  // This shouldn't be called anyway
	}
	// let DefDriverProc handle the rest
	return DefDriverProc(dwDriverID, hDriver, wMessage, dwParam1, dwParam2);
}

static const int numDevs = 5;
static const int maxClients = 1;
static const int maxChannels = 2;

struct ClientInfo
{
	WAVEOPENDESC wavOpen;
	DWORD dwFlags;
};
typedef struct ClientInfo NEAR *NPClientInfo;
typedef struct ClientInfo FAR *FPClientInfo;

static void do_driver_callback(struct ClientInfo *client, WORD msg, DWORD dwParam1)
{
	WAVEOPENDESC *wavOpen = &client->wavOpen;

	DWORD  dwCallback = wavOpen->dwCallback;
	UINT   uFlags     = HIWORD(client->dwFlags);
	HANDLE hDevice    = wavOpen->hWave;
	UINT   uMessage   = msg;
	DWORD  dwUser     = wavOpen->dwInstance;
	//DWORD  dwParam1   = 0;
	DWORD  dwParam2   = 0;

	dprintf("DriverCallback(0x%08lX, %u, 0x%X, %u, 0x%08lX, 0x%08lX, 0x%08lX)\n",
		dwCallback, uFlags, hDevice, uMessage, dwUser, dwParam1, dwParam2);

	// DriverCallback is documented in the DDK to return a BOOL indicating
	// whether the call succeeded. However, due to a bug in Windows itself
	// (Q169578, see https://helparchive.huntertur.net/document/37417), it
	// returns garbage instead. I'm not going to say how long I spent in
	// frustration trying to figure out what was going on. So, we will *not*
	// check the return value, but just hope that it actually worked!
	DriverCallback(
		dwCallback,
		uFlags,
		hDevice,
		uMessage,
		dwUser,
		dwParam1,
		dwParam2);
}

void DLL_EXPORT wave_block_finished(WAVEHDR FAR *wavHdr)
{
	wavHdr->dwFlags |= WHDR_DONE;
	wavHdr->dwFlags &= ~WHDR_INQUEUE;

	// TODO: should this be a FAR pointer?
	struct ClientInfo *client = (struct ClientInfo *)wavHdr->reserved;
	dprintf("wave_block_finished: client 0x%lX\n", client);
	do_driver_callback(client, WOM_DONE, (DWORD)wavHdr);
}

static void write_wave_data(WAVEHDR *wavHdr)
{
	dprintf("write_wave_data\n");

	// TODO: send this to the VxD. The VxD will then call wave_block_finished
	// when it is done with the wave block
	wave_block_finished(wavHdr);
}

static const char *wod_message_name(WORD msg)
{
	static char buf[32];
	switch(msg)
	{
#define X(x) case x: return #x;
	X(WODM_INIT)
	X(DRVM_EXIT)
	X(DRVM_ENABLE)
	X(DRVM_DISABLE)
	X(WODM_GETNUMDEVS)
	X(WODM_OPEN)
	X(WODM_CLOSE)
	X(WODM_WRITE)
	X(WODM_PREPARE)
	X(WODM_UNPREPARE)
	X(WODM_GETDEVCAPS)
	X(WODM_RESET)
	X(WODM_RESTART)
	X(WODM_SETPITCH)
	X(WODM_GETPLAYBACKRATE)
	X(WODM_SETPLAYBACKRATE)
	X(WODM_GETVOLUME)
	X(WODM_SETVOLUME)
#undef X
	default:
		sprintf(buf, "(unknown 0x%X)", msg);
		return buf;
	}
}

// Additional entry point for waveform output devices
// Parameters:
//   uDeviceID - ID of the target device. These are sequential, ranging from 0
//               to 1 less than the number of devices the driver supports.
//   uMsg      - Message sent to the driver
//   dwUser    - For the WODM_OPEN message, driver should fill this location
//               with instance data. For other messages, this contains the
//               instance data that was filled in WODM_OPEN.
//   dwParam1  - Message-specific parameter
//   dwParam2  - Message-specific parameter
// Return:
//   a MMSYSERR_... or WAVERR_... error code. MMSYSERR_NOTSUPPORTED should be
//   returned if we do not support the message.
DWORD DLL_EXPORT wodMessage(UINT uDeviceID, WORD uMsg, DWORD dwUser, DWORD dwParam1, DWORD dwParam2)
{
	struct ClientInfo FAR *client;

	dprintf("wodMessage(%u, %s, 0x%08lX, 0x%08lX, 0x%08lX)\n",
		uDeviceID, wod_message_name(uMsg), dwUser, dwParam1, dwParam2);

	switch (uMsg)
	{
	case WODM_INIT:
		// Sent when the device is first started. The driver should allocate an
		// instance structure or increment a usage reference count.
		return MMSYSERR_NOERROR;
	case DRVM_EXIT:
		// Sent when the device is disable and there are no open handles to the
		// driver. The driver should free instance structure when the usage
		// reference count is zero.
		return MMSYSERR_NOERROR;
	case DRVM_ENABLE:
		// Sent when the device node becomes active. The driver should
		// increment an enable count, perform hardware initialization, and
		// allocate structures.
		return MMSYSERR_NOERROR;
	case DRVM_DISABLE:
		// Sent when the device node becomes disabled. The driver should
		// decrement the enable count. When the enable count is zero, it should
		// release hardware resources and free structures allocated during
		// DRVM_ENABLE.
		return MMSYSERR_NOERROR;
	case WODM_GETNUMDEVS:
		return numDevs;
	case WODM_GETDEVCAPS:
		// Sent to request capabilities of a waveform output device
		// dwParam1 - pointer to a MDEVICECAPSEX structure that should be filled
		//            with capabilities of the device
		// dwParam2 - specifies a device node
		if (uDeviceID >= numDevs)
		{
			dprintf("bad device ID %u\n", uDeviceID);
			return MMSYSERR_BADDEVICEID;
		}
		MDEVICECAPSEX FAR *caps = (MDEVICECAPSEX FAR *)dwParam1;
		WAVEOUTCAPS FAR *wc = caps->pCaps;
		if (caps->cbSize < sizeof(*wc))
		{
			dprintf("struct size too small\n");
			return MMSYSERR_INVALPARAM;
		}
		// TODO: query this info from the VxD
		wc->wMid = 0x8086;
		wc->wPid = 0xA2F0;
		wc->vDriverVersion = (DRV_VER_MAJOR << 8) | DRV_VER_MINOR;
		sprintf(wc->szPname, "HD Audio Device %u", uDeviceID);
		wc->dwFormats = WAVE_FORMAT_1M08 | WAVE_FORMAT_1M16 | WAVE_FORMAT_1S08 | WAVE_FORMAT_1S16
					  | WAVE_FORMAT_2M08 | WAVE_FORMAT_2M16 | WAVE_FORMAT_2S08 | WAVE_FORMAT_2S16
					  | WAVE_FORMAT_4M08 | WAVE_FORMAT_4M16 | WAVE_FORMAT_4S08 | WAVE_FORMAT_4S16
					  /*| WAVE_FORMAT_96M08 | WAVE_FORMAT_96M16 | WAVE_FORMAT_96S08 | WAVE_FORMAT_96S16*/;
		wc->wChannels = maxChannels;
		wc->dwSupport = WAVECAPS_LRVOLUME|WAVECAPS_VOLUME/*|WAVECAPS_SAMPLEACCURATE*/;
		return MMSYSERR_NOERROR;
	case WODM_OPEN:
		// Sent to allocate a device for use by a client application
		// dwParam1 - pointer to a WAVEOPENDESC structure containing information
		//            such as format, instance data, and a callback
		// dwParam2 - flags for opening the device
		if (uDeviceID >= numDevs)
		{
			dprintf("bad device ID %u\n", uDeviceID);
			return MMSYSERR_BADDEVICEID;
		}
		const WAVEOPENDESC FAR *wavOpen = (WAVEOPENDESC FAR *)dwParam1;
		const PCMWAVEFORMAT FAR *lpFormat = (PCMWAVEFORMAT FAR *)wavOpen->lpFormat;
		if (lpFormat->wf.wFormatTag != WAVE_FORMAT_PCM)
		{
			dprintf("format %u not supported\n", lpFormat->wf.wFormatTag);
			return WAVERR_BADFORMAT;
		}
		if (lpFormat->wf.nChannels > maxChannels)
		{
			dprintf("too many channels %i\n", lpFormat->wf.nChannels);
			return WAVERR_BADFORMAT;
		}
		switch (lpFormat->wBitsPerSample)
		{
		case 4:
		case 8:
		case 16:
		case 20:
		case 24:
		case 32:
			break;
		default:
			dprintf("%u bits per sample not supported\n", lpFormat->wBitsPerSample);
			return WAVERR_BADFORMAT;
		}
		if (!(dwParam2 & WAVE_FORMAT_QUERY))
		{
			// Save the client info for when we need to call DriverCallback later
			client = malloc(sizeof(*client));
			if (client == NULL)
			{
				dprintf("failed to allocate memory\n");
				return MMSYSERR_NOMEM;
			}
			client->wavOpen = *wavOpen;
			client->dwFlags = dwParam2;
			// stuff it into dwUser so that we can retrieve it later during WODM_WRITE
			*(FPClientInfo FAR *)dwUser = client;
//			dprintf("stored userdata 0x%lX\n", *(FPClientInfo FAR *)dwUser);
			do_driver_callback(client, WOM_OPEN, 0);
			dprintf("device opened\n");
		}
		return MMSYSERR_NOERROR;
	case WODM_CLOSE:
		// Sent to deallocate a specified device
		// If there are buffers still playing, return WAVERR_STILLPLAYING
		client = (struct ClientInfo FAR *)dwUser;
		do_driver_callback(client, WOM_CLOSE, 0);
		free(client);
		dprintf("device closed\n");
		return MMSYSERR_NOERROR;
	case WODM_WRITE:
		// Sent to write a waveform data block to the device
		// dwParam1 - pointer to a WAVEHDR structure identifying the data block
		// dwParam2 - size of the WAVEHDR structure
		;
		WAVEHDR FAR *wavHdr = (WAVEHDR FAR *)dwParam1;
		if (dwParam2 < sizeof(*wavHdr))
		{
			dprintf("struct size too small\n");
			return MMSYSERR_INVALPARAM;
		}
		if (!(wavHdr->dwFlags & WHDR_PREPARED))
		{
			dprintf("wave header not prepared\n");
			return WAVERR_UNPREPARED;
		}
		wavHdr->dwFlags &= ~WHDR_DONE;
		wavHdr->dwFlags |= WHDR_INQUEUE;
		client = (struct ClientInfo FAR *)dwUser;
//		dprintf("retrieved userdata 0x%lX\n", client);
		// We can now store the client info in the "reserved" field of the WAVEHDR.
		// The MSSNDSYS DDK example does that, so it's okay.
		wavHdr->reserved = (DWORD)client;
		write_wave_data(wavHdr);
		return MMSYSERR_NOERROR;
	}

	dprintf("%s not handled\n", wod_message_name(uMsg));
	return MMSYSERR_NOTSUPPORTED;
}

// Symbols needed due to OpenWatcom linker bullshit. Should never get called.
// TODO: try to get rid of this
void __DLLstart(void)
{
	__asm int 3
}

void main(void)
{
	__asm int 3
}
