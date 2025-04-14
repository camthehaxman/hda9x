#pragma once

#include <windows.h>
#include <mmsystem.h>

#define WINMMAPI

// All structures here are byte-packed
#pragma pack(push)
#pragma pack(1)

#define DRV_LOAD                0x0001
#define DRV_ENABLE              0x0002
#define DRV_OPEN                0x0003
#define DRV_CLOSE               0x0004
#define DRV_DISABLE             0x0005
#define DRV_FREE                0x0006
#define DRV_CONFIGURE           0x0007
#define DRV_QUERYCONFIGURE      0x0008
#define DRV_INSTALL             0x0009
#define DRV_REMOVE              0x000A
#define DRV_RESERVED            0x0800
#define DRV_USER                0x4000

WINMMAPI BOOL WINAPI DriverCallback(DWORD dwCallback, UINT uFlags,
    HANDLE hDevice, UINT uMessage, DWORD dwUser, DWORD dwParam1, DWORD dwParam2);

#define DRVM_INIT    0x64
#define DRVM_EXIT    0x65
#define	DRVM_DISABLE 0x66
#define	DRVM_ENABLE  0x67

#define DRVM_MAPPER 0x2000
#define DRVM_USER   0x4000

typedef struct {
	DWORD  cbSize;
	LPVOID pCaps;
} MDEVICECAPSEX;

//------------------------------------------------------------------------------
// Waveform device driver support
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// from mmreg.h

// wFormatTag values
#define WAVE_FORMAT_UNKNOWN 0
#define WAVE_FORMAT_PCM     1
#define WAVE_FORMAT_ADPCM   2

// Extended waveform format structure (for all non-PCM formats)
typedef struct tWAVEFORMATEX
{
	WORD    wFormatTag;        /* format type */
	WORD    nChannels;         /* number of channels (i.e. mono, stereo...) */
	DWORD   nSamplesPerSec;    /* sample rate */
	DWORD   nAvgBytesPerSec;   /* for buffer estimation */
	WORD    nBlockAlign;       /* block size of data */
	WORD    wBitsPerSample;    /* Number of bits per sample of mono data */
	WORD    cbSize;            /* The count in bytes of the size of
                                  extra information (after cbSize) */
} WAVEFORMATEX;
typedef WAVEFORMATEX      *PWAVEFORMATEX;
typedef WAVEFORMATEX NEAR *NPWAVEFORMATEX;
typedef WAVEFORMATEX FAR  *LPWAVEFORMATEX;

// end mmreg.h
//------------------------------------------------------------------------------

// waveform input and output device open information structure
typedef struct waveopendesc_tag
{
	HWAVE                   hWave;             /* handle */
	const WAVEFORMATEX FAR* lpFormat;          /* format of wave data */
	DWORD                   dwCallback;        /* callback */
	DWORD                   dwInstance;        /* app's private instance information */
	UINT                    uMappedDeviceID;   /* device to map to if WAVE_MAPPED set */
	DWORD                   dnDevNode;         /* if device is PnP */
} WAVEOPENDESC;
typedef WAVEOPENDESC FAR *LPWAVEOPENDESC;

#define WODM_INIT DRVM_INIT
#define WIDM_INIT DRVM_INIT

#define WODM_GETNUMDEVS         3
#define WODM_GETDEVCAPS         4
#define WODM_OPEN               5
#define WODM_CLOSE              6
#define WODM_PREPARE            7
#define WODM_UNPREPARE          8
#define WODM_WRITE              9
#define WODM_PAUSE              10
#define WODM_RESTART            11
#define WODM_RESET              12 
#define WODM_GETPOS             13
#define WODM_GETPITCH           14
#define WODM_SETPITCH           15
#define WODM_GETVOLUME          16
#define WODM_SETVOLUME          17
#define WODM_GETPLAYBACKRATE    18
#define WODM_SETPLAYBACKRATE    19
#define WODM_BREAKLOOP          20

#pragma pack(pop)
