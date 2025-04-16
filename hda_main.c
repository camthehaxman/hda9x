#include <limits.h>
#include <string.h>
#include <windows.h>
#include <vmm.h>
#include <configmg.h>
#include <mmdevldr.h>
#include <vtd.h>

#include "hdaudio.h"
#include "memory.h"

typedef uint16_t nodeid_t;

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

#define MAX_CONNECTIONS 16

struct HDAWidget
{
	nodeid_t nodeID;
	uint8_t type;
	uint8_t connectionsCount;
	nodeid_t connections[MAX_CONNECTIONS];  // list of possible inputs to this widget
	nodeid_t outPath;  // next node in path to "Audio Output" widget, or 0 if none
	uint32_t caps;
	// specific to Pin Complex
	uint32_t pinCaps;
};

struct HDAAudioFuncGroup
{
	nodeid_t nodeID;
	uint8_t widgetsStart;  // starting node ID of child widgets
	uint8_t widgetsCount;  // number of child widgets
	struct HDAWidget *widgets;
};

struct HDACodec
{
	uint8_t addr;
	uint8_t childStart;
	uint8_t childCount;
	// We only support a single audio function group. While the standard doesn't
	// forbid there being multiple, this would be unusual.
	struct HDAAudioFuncGroup afg;
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

	hdaRegs->RINTCNT = 255;  // seems to be needed for QEMU's emulated card but not real hardware?

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

// Returns the widget in the codec with the given node ID
static struct HDAWidget *get_widget_by_id(struct HDACodec *codec, nodeid_t nodeID)
{
	ASSERT(nodeID >= codec->afg.widgetsStart && nodeID < codec->afg.widgetsStart + codec->afg.widgetsCount);
	struct HDAWidget *widget = &codec->afg.widgets[nodeID - codec->afg.widgetsStart];
	ASSERT(widget->nodeID == nodeID);
	return widget;
}

// Recursively finds the shortest path from the widget to an Audio Output widget
// and selects the connection
// Returns the length of the path
static int find_output_path(struct HDACodec *codec, struct HDAWidget *widget)
{
	uint32_t command, response;
	int pathLen = INT_MAX;
	widget->outPath = 0;

	for (int i = 0; i < widget->connectionsCount; i++)
	{
		uint16_t nodeID = widget->connections[i];
		if (nodeID == 0)  // Some codecs have widgets with zeros as the connections for some reason. Ignore those.
			continue;
		struct HDAWidget *input = get_widget_by_id(codec, nodeID);
		if (input->type == WIDGET_TYPE_AUDIO_OUTPUT)
		{
			// found it!
			pathLen = 1;
			widget->outPath = nodeID;
			break;
		}
		int len = 1 + find_output_path(codec, input);
		if (len < pathLen)
		{
			pathLen = len;
			widget->outPath = nodeID;
		}
	}

	// Select the connection
	if (pathLen < INT_MAX)
	{
		if (widget->type != WIDGET_TYPE_AUDIO_MIXER)  // Mixers have a hard-wired list of inputs and grab audio from all of them, therefore not selectable
		{
			int i;
			for (i = 0; i < widget->connectionsCount; i++)
				if (widget->outPath == widget->connections[i])
					break;
			ASSERT(i < widget->connectionsCount);
			command = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONNECTION_SELECT_CTRL, i);
			hda_run_commands(&command, &response, 1);
		}
	}

	return pathLen;
}

static void unmute_widget(struct HDACodec *codec, struct HDAWidget *widget)
{
	uint32_t commands[1], responses[1];
	if (widget->caps & WIDGET_CAP_OUTPUT_AMP)
	{
		commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_GET_PARAMETER, PARAM_OUTPUT_AMP_CAP);
		hda_run_commands(commands, responses, 1);
		int maxGain = GET_BITS(responses[0], 8, 7);  // NumSteps field
		uint32_t ampGainMute = maxGain | (1 << 15) | (1 << 13) | (1 << 12);
		commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_AMP_GAIN_MUTE, ampGainMute);
		hda_run_commands(commands, responses, 1);
	}
}

static BOOL hda_func_group_init(struct HDACodec *codec, struct HDAAudioFuncGroup *afg)
{
	uint32_t commands[2], responses[2];
	struct HDAWidget *widget;

	dprintf(" Audio Function Group #%i\n", afg->nodeID);
	afg->widgets = memory_alloc(sizeof(*afg->widgets) * afg->widgetsCount);
	if (afg->widgets == NULL)
	{
		dprintf("memory allocation failed\n");
		return FALSE;
	}
	memset(afg->widgets, 0, sizeof(*afg->widgets) * afg->widgetsCount);

	// Collect information about widgets and initialize them
	widget = afg->widgets;
	for (nodeid_t nodeID = afg->widgetsStart; nodeID < afg->widgetsCount; nodeID++, widget++)
	{
		commands[0] = MAKE_COMMAND(codec->addr, nodeID, VERB_GET_PARAMETER, PARAM_AUDIO_WIDGET_CAP);
		hda_run_commands(commands, responses, 1);
		widget->caps = responses[0];
		widget->nodeID = nodeID;
		widget->type = GET_BITS(widget->caps, 20, 4);

		dprintf("  Widget #%i, type = %i\n", nodeID, widget->type);

		if (widget->type == WIDGET_TYPE_PIN_COMPLEX)
		{
			commands[0] = MAKE_COMMAND(codec->addr, nodeID, VERB_GET_PARAMETER, PARAM_PIN_CAP);
			hda_run_commands(commands, responses, 1);
			widget->pinCaps = responses[0];
			// For some reason, EAPD needs to be enabled if the widget supports
			// it, or else we just get silence
			if (widget->pinCaps & PINCAP_EAPD)
			{
				commands[0] = MAKE_COMMAND(codec->addr, nodeID, VERB_SET_EAPD_ENABLE, (1 << 1));
				hda_run_commands(commands, responses, 1);
			}
		}

		widget->connectionsCount = 0;
		if (widget->caps & WIDGET_CAP_CONN_LIST)
		{
			commands[0] = MAKE_COMMAND(codec->addr, nodeID, VERB_GET_PARAMETER, PARAM_CONN_LIST_LENGTH);
			hda_run_commands(commands, responses, 1);
			BOOL longForm = (responses[0] & (1 << 7)) != 0;
			widget->connectionsCount = responses[0] & 0x3F;
			int entriesPerResp = longForm ? 2 : 4;

			if (widget->connectionsCount > MAX_CONNECTIONS)
			{
				widget->connectionsCount = MAX_CONNECTIONS;
				dprintf("Warning: widget #%i's connection list is too long!\n", nodeID);
			}
			dprintf("   Connections (%i):\n", widget->connectionsCount);
			dprintf("    ");
			for (int i = 0; i < widget->connectionsCount; i++)
			{
				int index = i % entriesPerResp;
				if (index == 0)
				{
					// fetch the next set of entries
					commands[0] = MAKE_COMMAND(codec->addr, nodeID, VERB_GET_CONNECTION_LIST_ENTRY, i - index);
					hda_run_commands(commands, responses, 1);
				}
				if (longForm)
					widget->connections[i] = (responses[0] >> (index * 16)) & 0xFFFF;
				else
					widget->connections[i] = (responses[0] >> (index * 8)) & 0xFF;
				dprintf("%i ", widget->connections[i]);
			}
			dprintf("\n");
		}
	}

	// Find widget paths, from Pin Complex to Audio Output
	widget = afg->widgets;
	for (nodeid_t nodeID = afg->widgetsStart; nodeID < afg->widgetsCount; nodeID++, widget++)
	{
		if (widget->type == WIDGET_TYPE_PIN_COMPLEX && (widget->pinCaps & PINCAP_OUTPUT))
		{
			find_output_path(codec, widget);
			if (widget->outPath != 0)
			{
				dprintf("enabling output pin %i\n", nodeID);
				// enable output
				uint32_t pinCtrl;
				commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_GET_PIN_CONTROL, 0);
				hda_run_commands(commands, &pinCtrl, 1);
				pinCtrl |= PIN_CONTROL_OUTPUT_ENABLE;
				commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_PIN_CONTROL, pinCtrl);
				hda_run_commands(commands, responses, 1);
				// unmute all nodes in path
				struct HDAWidget *w;
				for (w = widget; w->outPath != 0; w = get_widget_by_id(codec, w->outPath))
					unmute_widget(codec, w);
				ASSERT(w->type == WIDGET_TYPE_AUDIO_OUTPUT);
				unmute_widget(codec, w);
				if (w->caps & WIDGET_CAP_DIGITAL)
				{
					// There's an extra bit that we need to enable for digital Audio Outputs
					uint32_t digiconv;
					commands[0] = MAKE_COMMAND(codec->addr, w->nodeID, VERB_GET_DIGICONVERT, 0);
					hda_run_commands(commands, &digiconv, 1);
					digiconv |= 1;
					commands[0] = MAKE_COMMAND(codec->addr, w->nodeID, VERB_SET_DIGICONVERT0, (digiconv & 0xFF));
					hda_run_commands(commands, responses, 1);
				}
			}
		}
	}

	return TRUE;
}

static void hda_codec_init(struct HDACodec *codec)
{
	uint32_t commands[2], responses[2];
	int childStart, childCount;

	commands[0] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_VENDOR_ID);
	commands[1] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
	hda_run_commands(commands, responses, 2);
	childStart = GET_BITS(responses[1], 16, 8);
	childCount = GET_BITS(responses[1], 0, 8);
	codec->childStart = childStart;
	codec->childCount = childCount;
	dprintf("Codec %i, vendor/device=0x%08X, childStart=%i, childCount=%i\n", codec->addr, responses[0], childStart, childCount);

	BOOL gotAFG = FALSE;

	// Enumerate function groups
	for (nodeid_t fgID = childStart; fgID < childStart + childCount; fgID++)
	{
		commands[0] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_PARAMETER, PARAM_FUNC_GRP_TYPE);
		commands[1] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
		hda_run_commands(commands, responses, 2);
		if (GET_BITS(responses[0], 0, 8) == FUNC_GRP_AUDIO)
		{
			codec->afg.nodeID = fgID;
			codec->afg.widgetsStart = GET_BITS(responses[1], 16, 8);
			codec->afg.widgetsCount = GET_BITS(responses[1], 0, 8);
			hda_func_group_init(codec, &codec->afg);
			gotAFG = TRUE;
			break;
		}
	}
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
