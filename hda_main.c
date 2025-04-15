#include <string.h>
#include <windows.h>
#include <vmm.h>
#include <configmg.h>
#include <mmdevldr.h>
#include <vtd.h>

#include "hdaudio.h"
#include "memory.h"

struct HDARegs *hdaRegs;
unsigned int hdaIRQNum;

// Communication with the HDA controller is done through two ring buffers (queues):
// the Command Outbound Ring Buffer (CORB) and the Response Input Ring Buffer (RIRB).
// We send commands to the controller by placing them in the CORB. The controller
// reads these commands and places its responses into the RIRB.

uint32_t    *corb;
physaddr_t   corbPhys;
unsigned int corbLength;

struct RIRBEntry
{
	uint32_t response;
	uint32_t resp_ex;
};

struct RIRBEntry *rirb;
physaddr_t        rirbPhys;
unsigned int      rirbLength;
unsigned int      rirbRP;

struct HDACodec
{
	uint8_t addr;
	uint8_t childStart;
	uint8_t childCount;
};

#define MAX_CODECS 15
struct HDACodec codecs[MAX_CODECS];
unsigned int    codecsCount;

static void hda_codec_init(struct HDACodec *codec);

#define TIMER_CLOCK_RATE 1193182  // Frequency of the Programmable Interrupt Timer
#define MICROSECS_TO_TICKS(microsecs) ((unsigned long long)(microsecs) * TIMER_CLOCK_RATE / 1000000)

// Busy-waits for the specified number of clock ticks
static void wait_ticks(unsigned long ticks)
{
	unsigned long long end = VTD_Get_Real_Time() + ticks;
	while (VTD_Get_Real_Time() < end)
		;
}

// Waits a maximum of maxWait ticks for cond to be true.
// runs the ontimeout code if the time expires
#define WAIT_FOR(cond, maxWait, ontimeout) \
do { \
	unsigned long long timeout = VTD_Get_Real_Time() + (maxWait); \
	while (!(cond)) \
	{ \
		if (VTD_Get_Real_Time() > timeout) \
		{ \
			ontimeout; \
			break; \
		} \
	} \
} while (0)

//------------------------------------------------------------------------------
// Controller hardware
//------------------------------------------------------------------------------

// Resets the entire controller to a known state
static BOOL hda_controller_reset(void)
{
	dprintf("resetting HDA controller\n");

	hdaRegs->CORBCTL &= ~CORBCTL_CORBRUN;  // Stop CORB DMA
	hdaRegs->RIRBCTL &= ~RIRBCTL_RIRBDMAEN;  // Stop RIRB DMA

	// Stop all streams
	int totalStreams = GCAP_ISS(hdaRegs->GCAP) + GCAP_OSS(hdaRegs->GCAP) + GCAP_BSS(hdaRegs->GCAP);
	for (int i = 0; i < totalStreams; i++)
	{
		hdaRegs->SDESC[i].SDCTLb0 = 0;
		hdaRegs->SDESC[i].SDCTLb2 = 0;
	}

	hdaRegs->WAKEEN = 0;  // Disable wake events
	hdaRegs->INTCTL = 0;  // Disable interrupts
	hdaRegs->STATESTS = hdaRegs->STATESTS;  // Clear state change status register
	hdaRegs->RIRBSTS = hdaRegs->RIRBSTS;  // Clear RIRB status register

	// Put the controller in reset (see HDA spec, section 4.2.2)
	hdaRegs->GCTL &= ~GCTL_CRST;
	// And make sure it actually went into reset (CRST bit should be 0)
	WAIT_FOR(
		(hdaRegs->GCTL & GCTL_CRST) == 0,
		MICROSECS_TO_TICKS(1000000),
		dprintf("ERROR: unable to reset HDA controller\n"); return FALSE;
	);
	// Take the controller out of reset
	hdaRegs->GCTL |= GCTL_CRST;
	// And make sure it actually came out of reset (CRST should be 1)
	WAIT_FOR(
		(hdaRegs->GCTL & GCTL_CRST) != 0,
		MICROSECS_TO_TICKS(1000000),
		dprintf("ERROR: HDA controller is stuck in reset\n"); return FALSE;
	);

	// Wait at least 512 microseconds for all codecs to register themselves
	wait_ticks(MICROSECS_TO_TICKS(521));

	return TRUE;
}

static BOOL hda_controller_setup_corb_rirb(void)
{
	uint8_t reg;

	// Determine CORB SIZE
	reg = hdaRegs->CORBSIZE;
	reg &= ~CORBSIZE_CORBSIZE_MASK;
	if (reg & CORBSIZE_CORBSZCAP_256ENT)
	{
		corbLength = 256;
		reg |= CORBSIZE_CORBSIZE_256ENT;
	}
	else if (reg & CORBSIZE_CORBSZCAP_16ENT)
	{
		corbLength = 16;
		reg |= CORBSIZE_CORBSIZE_16ENT;
	}
	else if (reg & CORBSIZE_CORBSZCAP_2ENT)
	{
		corbLength = 2;
		reg |= CORBSIZE_CORBSIZE_2ENT;
	}
	else
	{
		dprintf("cannot determine CORB size\n");
		return FALSE;
	}
	dprintf("CORB size: %i entries\n", corbLength);
	hdaRegs->CORBSIZE = reg;

	// Determine RIRB SIZE
	reg = hdaRegs->RIRBSIZE;
	reg &= ~RIRBSIZE_RIRBSIZE_MASK;
	if (reg & RIRBSIZE_RIRBSZCAP_256ENT)
	{
		rirbLength = 256;
		reg |= RIRBSIZE_RIRBSIZE_256ENT;
	}
	else if (reg & RIRBSIZE_RIRBSZCAP_16ENT)
	{
		rirbLength = 16;
		reg |= RIRBSIZE_RIRBSIZE_16ENT;
	}
	else if (reg & RIRBSIZE_RIRBSZCAP_2ENT)
	{
		rirbLength = 2;
		reg |= RIRBSIZE_RIRBSIZE_2ENT;
	}
	else
	{
		dprintf("cannot determine RIRB size\n");
		return FALSE;
	}
	dprintf("RIRB size: %i entries\n", rirbLength);
	hdaRegs->RIRBSIZE = reg;

	corb = memory_alloc_phys(corbLength * sizeof(*corb), &corbPhys);
	if (corb == NULL)
	{
		dprintf("failed to allocate CORB\n");
		return FALSE;
	}
	rirb = memory_alloc_phys(rirbLength * sizeof(*rirb), &rirbPhys);
	if (rirb == NULL)
	{
		dprintf("failed to allocate RIRB\n");
		return FALSE;
	}

	dprintf("CORB: phys=0x%08X, virt=0x%08X, %u entries\n", corbPhys, corb, corbLength);
	dprintf("RIRB: phys=0x%08X, virt=0x%08X, %u entries\n", rirbPhys, rirb, rirbLength);

	hdaRegs->CORBLBASE = corbPhys;
	hdaRegs->CORBUBASE = 0;
	hdaRegs->RIRBLBASE = rirbPhys;
	hdaRegs->RIRBUBASE = 0;

	// Reset CORB read pointer to 0 (see HDA spec, section 3.3.21)
	hdaRegs->CORBRP |= CORBRP_CORBRPRST;
	WAIT_FOR(
		hdaRegs->CORBRP & CORBRP_CORBRPRST,
		MICROSECS_TO_TICKS(1000000),
		dprintf("failed to reset CORB\n"); return FALSE;
	);
	hdaRegs->CORBRP &= ~CORBRP_CORBRPRST;
	WAIT_FOR(
		!(hdaRegs->CORBRP & CORBRP_CORBRPRST),
		MICROSECS_TO_TICKS(1000000),
		dprintf("failed to reset CORB\n"); return FALSE;
	);
	// Reset CORB write pointer to 0 (see HDA spec, section 3.3.20)
	hdaRegs->CORBWP &= ~CORBWP_CORBWP_MASK;

	// Reset RIRB write pointer to 0 (HDA spec section 3.3.27)
	hdaRegs->RIRBWP |= RIRBWP_RIRBWPRST;

	hdaRegs->RINTCNT = 1;  // seems to be needed for QEMU's emulated card but not real hardware?

	// Start CORB/RIRB DMA
	hdaRegs->CORBCTL |= CORBCTL_CORBRUN | CORBCTL_CMEIE;
	hdaRegs->RIRBCTL |= RIRBCTL_RIRBDMAEN;

	return TRUE;
}

// Sends commands through the HDA controller using the CORB
static BOOL hda_cmd_send(const uint32_t *commands, unsigned int count)
{
	unsigned int corbRP = hdaRegs->CORBRP;
	unsigned int corbWP = hdaRegs->CORBWP;

	// CORB should be empty, as this is the only code that writes to it
	ASSERT(corbRP == corbWP);
	// Should check before writing too many commands
	ASSERT(count <= corbLength);
	// Write entries to CORB
	for (unsigned int numWritten = 0; numWritten < count; numWritten++)
	{
		corbWP = (corbWP + 1) % corbLength;
		corb[corbWP] = commands[numWritten];
	}
	hdaRegs->CORBWP = corbWP;  // Tell the controller that there are new commands
	// Wait for the controller to receive all commands
	WAIT_FOR(
		hdaRegs->CORBRP == corbWP,
		MICROSECS_TO_TICKS(1000000),
		dprintf("CORB send timed out\n"); return FALSE;
	);
	return TRUE;
}

// Receives responses from the commands that were sent
static BOOL hda_cmd_recv(uint32_t *responses, unsigned int count)
{
	// get responses
	unsigned int i = 0;
	while (i < count)
	{
		// wait for the controller to write response(s)
		WAIT_FOR(
			hdaRegs->RIRBWP != rirbRP,
			MICROSECS_TO_TICKS(1000000),
			dprintf("RIRB recv timed out\n"); return FALSE;
		);

		// read a response
		rirbRP = (rirbRP + 1) % rirbLength;
		struct RIRBEntry response = rirb[rirbRP];
		if (!(response.resp_ex & (1 << 4)))
			responses[i++] = response.response;
	}

	// skip over any unsolicited responses
	while (rirbRP != hdaRegs->RIRBWP)
	{
		rirbRP = (rirbRP + 1) % rirbLength;
		if (!(rirb[rirbRP].resp_ex & (1 << 4)))
		{
			dprintf("excess solicited responses\n");
			return FALSE;
		}
	}

	return TRUE;
}

// Sends commands to the controller and receives the responses
static BOOL hda_run_commands(const uint32_t *commands, uint32_t *responses, unsigned int count)
{
	return hda_cmd_send(commands, count) && hda_cmd_recv(responses, count);
}

// Discovers and initializes all codecs attached to the controller
static BOOL hda_controller_enum_codecs(void)
{
	uint16_t statests = hdaRegs->STATESTS;

	codecsCount = 0;
	for (int i = 0; i < MAX_CODECS; i++)
	{
		if (statests & (1 << i))
		{
			struct HDACodec *codec = &codecs[codecsCount++];
			memset(codec, 0, sizeof(*codec));
			codec->addr = i;
			hda_codec_init(codec);
		}
	}
	if (codecsCount == 0)
	{
		dprintf("no codecs found\n");
		return FALSE;
	}
	return TRUE;
}

//------------------------------------------------------------------------------
// HDA codecs
//------------------------------------------------------------------------------

static void hda_codec_init(struct HDACodec *codec)
{
	uint32_t commands[1], responses[1];

	commands[0] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_VENDOR_ID);
	hda_run_commands(commands, responses, 1);
	printf("Codec %i, 0x%08X\n", codec->addr, responses[0]);
}

//------------------------------------------------------------------------------
// VxD procedures
//------------------------------------------------------------------------------

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
		// Get the hardware resource configuration from Configuration Manager
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

		// Verify that the PnP-configured resources are suitable
		if (conf.wNumMemWindows < 1)
		{
			dprintf("no MMIO available!\n");
			return CR_FAILURE;
		}
		if (conf.dMemLength[0] < 0x2000)
		{
			dprintf("MMIO space too small!\n");
			return CR_FAILURE;
		}
		if (conf.wNumIRQs < 1)
		{
			dprintf("no IRQ available!\n");
			return CR_FAILURE;
		}

		hdaIRQNum = conf.bIRQRegisters[0];
		hdaRegs = memory_map_phys_to_virt(conf.dMemBase[0], conf.dMemLength[0]);
		if (hdaRegs == NULL)
		{
			dprintf("failed to map HDA MMIO!\n");
			return CR_OUT_OF_MEMORY;
		}

		dprintf("HDA MMIO: physical=0x%08X, virtual=0x%08X, length=0x%X\n", conf.dMemBase[0], hdaRegs, conf.dMemLength[0]);
		dprintf("HDA controller version: %i.%i\n", hdaRegs->VMAJ, hdaRegs->VMIN);

		// Now, initialize the hardware
		if (!hda_controller_reset())
			return CR_FAILURE;
		if (!hda_controller_setup_corb_rirb())
			return CR_FAILURE;
		if (!hda_controller_enum_codecs())
			return CR_FAILURE;
		return CR_SUCCESS;
	}
	return CR_DEFAULT;
}

static const char *control_msg_name(DWORD msg)
{
	static char buf[32];
	switch(msg)
	{
#define X(x) case x: return #x;
	X(SYS_CRITICAL_INIT)
	X(DEVICE_INIT)
	X(INIT_COMPLETE)
	X(SYS_VM_INIT)
	X(SET_DEVICE_FOCUS)
	X(BEGIN_PM_APP)
	X(DESTROY_THREAD)
	X(POWER_EVENT)
	X(SYS_DYNAMIC_DEVICE_INIT)
	X(SYS_DYNAMIC_DEVICE_EXIT)
	X(THREAD_Not_Executeable)
	X(CREATE_THREAD)
	X(THREAD_INIT)
	X(TERMINATE_THREAD)
	X(PNP_NEW_DEVNODE)
	X(KERNEL32_INITIALIZED)
	X(CREATE_PROCESS)
	X(DESTROY_PROCESS)
#undef X
	default:
		sprintf(buf, "(unknown 0x%X)", msg);
		return buf;
	}
}

// Handler for system control messages
// Returns 0 on error
DWORD __cdecl hda_vxd_control_proc(DWORD msg, DWORD paramEBX, DWORD paramEDX)
{
	hda_debug_init();

	dprintf("hda_vxd_control_proc(msg=%s, paramEBX=0x%X, paramEDX=0x%X)\n",
		control_msg_name(msg), paramEBX, paramEDX);

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
