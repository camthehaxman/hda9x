// 16-bit userspace driver DLL (HDAUDIO.DRV) for High Definition Audio

#include <windows.h>
#include <mmsystem.h>

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
	dprintf("DriverProc(dwDriverID=0x%08lX, hDriver=0x%04X, wMessage=0x%04X, dwParam1=0x%08lX, dwParam2=0x%08lX)\n",
		dwDriverID, hDriver, wMessage, dwParam1, dwParam2);

	switch (wMessage)
	{
	case DRV_LOAD:
		// Sent to the driver when it is loaded. This will always be the first
		// message received.
		// Return non-zero for success.
		dprintf("DRV_LOAD\n");
		return 1;
	case DRV_FREE:
		// Sent to the driver when it is about to be discarded. This will always
		// be the last message received before being feed.
		// Return value is ignored.
		dprintf("DRV_FREE\n");
		return 1;
	case DRV_OPEN:
		// Sent to the driver when it is opened.
		// Return???
		dprintf("DRV_OPEN\n");
		return 1;
	case DRV_CLOSE:
		// Sent to the driver when it is closed. Drivers are unloaded when the
		// close count reaches zero.
		// dwDriverID - driver identifier returned from corresponding DRV_OPEN
		// lParam1    - passed through from the drvOpen call
		// lParam2    - passed through from the drvOpen call
		// Return 0 to fail
		dprintf("DRV_CLOSE\n");
		return 1;
	case DRV_ENABLE:
		// Sent to the driver when it is loaded or reloaded and whenever Windows
		// is enabled. Drivers should only hook interrupts or expect any part of
		// of the driver to be in memory between enable and disable messages
		// Return value is ignored.
		dprintf("DRV_ENABLE\n");
		return 1;
	case DRV_DISABLE:
		// Sent to the driver before the driver is freed and whenever Windows is
		// disabled
		// Return value is ignored.
		dprintf("DRV_DISABLE\n");
		return 1;
	case DRV_QUERYCONFIGURE:
		// Sent to the driver to determine whether it supports custom
		// configuration
		// Return non-zero if custom configuration is supported.
		dprintf("DRV_QUERYCONFIGURE\n");
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
	dprintf("wodMessage(%u, %u, 0x%08lX, 0x%08lX, 0x%08lX)\n",
		uDeviceID, uMsg, dwUser, dwParam1, dwParam2);
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
