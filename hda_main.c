#include <limits.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <nt/winerror.h>

#include <vmm.h>
#include <configmg.h>
#include <mmdevldr.h>
#include <pci.h>
#include <shell.h>
#include <vpicd.h>
#include <vtd.h>
#include <vwin32.h>

#include "tinyprintf.h"
#include "hdaudio.h"
#include "memory.h"
#include "hda_vxd_api.h"

#define BKPT __asm int 3

typedef uint16_t nodeid_t;

#define HDA_QUIRK_FORCE_STEREO (1 << 0)

DEVNODE hdaDevNode;
struct HDARegs *hdaRegs;
unsigned int hdaIRQNum;
HIRQ hIRQ;
uint32_t hdaQuirks = 0;

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

#define STREAM_CHUNK_SIZE 4096

// An entry in a stream's Buffer Descriptor List (BDL)
struct HDABufferDesc
{
	uint64_t address;  // physical address of the buffer
	uint32_t size;  // size of the buffer
	uint32_t ioc;  // interrupt on completion flag
};

struct AudioBlock
{
	WAVEHDR *wavHdr;
	DWORD wavHdrSegOff;  // the segment:offset address of wavHdr
	const void *data;
	struct AudioBlock *next;
	size_t bytesWritten;
};

#define OUTPUT_STREAM_TAG 1
#define INPUT_STREAM_TAG  2

typedef void (*ConverterFunc)(void *dest, const void *src, size_t *destSize, size_t *srcSize);

struct HDAStream
{
	uint8_t index;  // stream descriptor index
	uint8_t streamTag;  // value sent to codecs identifying this stream
	uint16_t format;
	void *waveBuf;
	physaddr_t waveBufPhys;
	size_t waveBufSize;
	struct HDABufferDesc *bdl;
	physaddr_t bdlPhys;
	int numBDLEntries;
	uint8_t sampleBits;
	uint8_t chanCount;
	struct AudioBlock *blockList;
	uint32_t currPos;  // current read/write position in waveBuf
	ConverterFunc converter;
};

struct HDAStream outStream;

#define MAX_CODECS 15
struct HDACodec codecs[MAX_CODECS];
unsigned int    codecsCount;

static void hda_codec_init(struct HDACodec *codec);

//------------------------------------------------------------------------------
// Misc. Functions
//------------------------------------------------------------------------------

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

// Reads a 16-bit value from the PCI configuration space
static BOOL pci_read_word(uint16_t *value, DEVNODE devnode, uint32_t offset)
{
	return (CM_Call_Enumerator_Function(devnode, PCI_ENUM_FUNC_GET_DEVICE_INFO, offset, value, sizeof(*value), 0) == CR_SUCCESS);
}

// Disables interrupts and returns the previous interrupt flag
static uint16_t __declspec(naked) disable_interrupts(void)
{
	__asm {
		pushf
		pop ax
		and ax, 0x200
		cli
		ret
	}
}

// Restores interrupt status to the previous state
// Parameters:
//   iflag - value from previous call to disable_interrupts
static void __declspec(naked) restore_interrupts(uint16_t iflag)
{
	__asm {
		pushf
		or WORD PTR [esp], ax
		popf
		ret
	}
}
#pragma aux restore_interrupts __parm [eax]

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

	hdaRegs->INTCTL |= INTCTL_SIE_MASK | INTCTL_GIE;  // enable stream interrupts

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
BOOL hda_run_commands(const uint32_t *commands, uint32_t *responses, unsigned int count)
{
	return hda_cmd_send(commands, count) && hda_cmd_recv(responses, count);
}

// Sends a single command to the controller and receives the response
BOOL hda_run_command(uint32_t command, uint32_t *response)
{
	return hda_run_commands(&command, response, 1);
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

// Finds the shortest path from the specified widget to an "Audio Output" widget and
// sets the connections appropriately
// Returns the length of the path, or INT_MAX if none was found
static int build_output_path(struct HDACodec *codec, struct HDAWidget *widget)
{
	uint32_t command, response;
	int pathLen = INT_MAX;
	widget->outPath = 0;

	for (int i = 0; i < widget->connectionsCount; i++)
	{
		uint16_t nodeID = widget->connections[i];
		if (nodeID < codec->afg.widgetsStart || nodeID >= codec->afg.widgetsStart + codec->afg.widgetsCount)
		{
			dprintf("warning: widget %i has invalid connection %i\n", widget->nodeID, nodeID);
			continue;
		}
		struct HDAWidget *input = get_widget_by_id(codec, nodeID);
		if (input->type == WIDGET_TYPE_AUDIO_OUTPUT)  // found it!
		{
			pathLen = 1;
			widget->outPath = nodeID;
			break;
		}
		int len = 1 + build_output_path(codec, input);
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
			// find index in the connection list
			for (i = 0; i < widget->connectionsCount; i++)
				if (widget->outPath == widget->connections[i])
					break;
			ASSERT(i < widget->connectionsCount);
			// set the connection
			command = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONNECTION_SELECT_CTRL, i);
			hda_run_commands(&command, &response, 1);
			dprintf("connected widget %i to input %i\n",
				widget->nodeID, widget->outPath);
		}
	}

	return pathLen;
}

// If the widget has an output amplifier, unmutes it and sets its gain to max
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

	dprintf(" Audio Function Group #%i, %i widgets, start %i\n", afg->nodeID, afg->widgetsCount, afg->widgetsStart);
	afg->widgets = memory_alloc(sizeof(*afg->widgets) * afg->widgetsCount);
	if (afg->widgets == NULL)
	{
		dprintf("memory allocation failed\n");
		return FALSE;
	}
	memset(afg->widgets, 0, sizeof(*afg->widgets) * afg->widgetsCount);

	// Collect information about widgets and initialize them
	widget = afg->widgets;
	for (nodeid_t nodeID = afg->widgetsStart; nodeID < afg->widgetsStart + afg->widgetsCount; nodeID++, widget++)
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

	dprintf("building paths\n");

	// Find widget paths, from Pin Complex to Audio Output
	widget = afg->widgets;
	for (nodeid_t nodeID = afg->widgetsStart; nodeID < afg->widgetsStart + afg->widgetsCount; nodeID++, widget++)
	{
		if (widget->type == WIDGET_TYPE_PIN_COMPLEX && (widget->pinCaps & PINCAP_OUTPUT))
		{
			build_output_path(codec, widget);
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
				// set stream tag
				w->streamTag = OUTPUT_STREAM_TAG;
				commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONVERTER_STREAM_CHANNEL, (widget->streamTag << 4) | 0);
				hda_run_commands(commands, responses, 1);
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
	dprintf("end codec\n");
}

//------------------------------------------------------------------------------
// Stream management
//------------------------------------------------------------------------------

static void hda_stream_reset(struct HDAStream *stream)
{
	struct HDAStreamDesc *sdesc = &hdaRegs->SDESC[stream->index];

	sdesc->SDCTLb0 &= ~SDCTLb0_RUN;  // Stop stream

	// Reset stream
	sdesc->SDCTLb0 |= SDCTLb0_SRST;
	WAIT_FOR(
		sdesc->SDCTLb0 & SDCTLb0_SRST,
		MICROSECS_TO_TICKS(1000000),
		dprintf("stream not resetting\n");
	);
	sdesc->SDCTLb0 &= ~SDCTLb0_SRST;
	WAIT_FOR(
		!(sdesc->SDCTLb0 & SDCTLb0_SRST),
		MICROSECS_TO_TICKS(1000000),
		dprintf("stream not coming out of reset\n");
	);

	sdesc->SDCTLb0 |= SDCTLb0_DEIE | SDCTLb0_FEIE | SDCTLb0_IOCE;
	stream->currPos = 0;

	//hda_debug_dump_regs();
}

static BOOL hda_output_stream_create(struct HDAStream *stream)
{
	memset(stream, 0, sizeof(*stream));

	int numInputStreams = GCAP_ISS(hdaRegs->GCAP);
	stream->index = numInputStreams;  // Output streams immediately follow input streams

	stream->streamTag = OUTPUT_STREAM_TAG;

	// Create buffer
	stream->numBDLEntries = 64;
	stream->waveBufSize = stream->numBDLEntries * STREAM_CHUNK_SIZE;
	stream->waveBuf = memory_alloc_phys(stream->waveBufSize, &stream->waveBufPhys);
	if (stream->waveBuf == NULL)
		goto alloc_fail;
	memset(stream->waveBuf, 0, stream->waveBufSize);

	// Create Buffer Descriptor List (BDL)
	stream->bdl = memory_alloc_phys(stream->numBDLEntries * sizeof(*stream->bdl), &stream->bdlPhys);
	if (stream->bdl == NULL)
		goto alloc_fail;
	for (int i = 0; i < stream->numBDLEntries; i++)
	{
		stream->bdl[i].address = stream->bdlPhys + i * STREAM_CHUNK_SIZE;
		stream->bdl[i].size = STREAM_CHUNK_SIZE;
		stream->bdl[i].ioc = 1;
	}

	dprintf("waveBuf: phys=0x%08X, virt=0x%08X\n"
	        "BDL:     phys=0x%08X, virt=0x%08X\n",
		stream->waveBufPhys, stream->waveBuf,
		stream->bdlPhys, stream->bdl);
	// Must be aligned to a multiple of 128 bytes. memory_alloc_phys should take
	// care of that, since it allocates whole 4096-byte pages
	ASSERT((stream->waveBufPhys & 0x7F) == 0);
	ASSERT((stream->bdlPhys & 0x7F) == 0);

	hda_stream_reset(stream);
	return TRUE;

alloc_fail:
	dprintf("memory allocation failure\n");
	return FALSE;
}

// Converts unsigned 8-bit mono to signed 8-bit stereo
static void convert_1u8_2s8(void *dest, const void *src, size_t *destSize, size_t *srcSize)
{
	int nSamples = MIN(*srcSize, *destSize / 2);
	int8_t        *d = dest;
	const uint8_t *s = src;
	for (int i = 0; i < nSamples; i++)
	{
		int8_t sample = *s++ - 0x80;
		*d++ = sample;
		*d++ = sample;
		ASSERT((uint8_t *)s - (uint8_t *)src <= *srcSize);
		ASSERT((uint8_t *)d - (uint8_t *)dest <= *destSize);
	}
	*srcSize = nSamples;
	*destSize = nSamples * 2;
}

static void convert_1u8_2s16(void *dest, const void *src, size_t *destSize, size_t *srcSize)
{
	int nSamples = MIN(*srcSize, *destSize / 4);
	int16_t       *d = dest;
	const uint8_t *s = src;
	for (int i = 0; i < nSamples; i++)
	{
		int16_t sample = (*s++ - 0x80) << 8;
		*d++ = sample;
		*d++ = sample;
		ASSERT((uint8_t *)s - (uint8_t *)src <= *srcSize);
		ASSERT((uint8_t *)d - (uint8_t *)dest <= *destSize);
	}
	*srcSize = nSamples;
	*destSize = nSamples * 4;
}

// Simply copies stream data
static void convert_identity(void *dest, const void *src, size_t *destSize, size_t *srcSize)
{
	size_t size = MIN(*destSize, *srcSize);
	*destSize = *srcSize = size;
	memcpy(dest, src, size);
}

static BOOL hda_stream_set_format(struct HDAStream *stream, const PCMWAVEFORMAT *wavFmt)
{
	uint16_t fmt = 0;

	// TODO: query from hardware
	uint32_t hwSupp = PCM_SUPP_16BIT;
	uint32_t hwMinChannels = 1;
	uint32_t hwMaxChannels = 16;

	if (hdaQuirks & HDA_QUIRK_FORCE_STEREO)
		hwMinChannels = 2;

	dprintf("hda_stream_set_format: channels=%u, sampleRate=%u, bits=%i\n",
		wavFmt->wf.nChannels, wavFmt->wf.nSamplesPerSec, wavFmt->wBitsPerSample);

	if (wavFmt->wf.wFormatTag != WAVE_FORMAT_PCM)
	{
		dprintf("unsupported non-PCM format\n");
		return FALSE;
	}

	// TODO: check what the hardware actually supports
	switch (wavFmt->wf.nSamplesPerSec)
	{
	case 8000:   fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X1 | SDFMT_DIV_6; break;
	case 11025:  fmt |= SDFMT_BASE_44KHZ | SDFMT_MULT_X1 | SDFMT_DIV_4; break;
	case 16000:  fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X1 | SDFMT_DIV_3; break;
	case 22050:  fmt |= SDFMT_BASE_44KHZ | SDFMT_MULT_X1 | SDFMT_DIV_2; break;
	case 32000:  fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X2 | SDFMT_DIV_3; break;
	case 44100:  fmt |= SDFMT_BASE_44KHZ | SDFMT_MULT_X1 | SDFMT_DIV_1; break;
	case 48000:  fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X1 | SDFMT_DIV_1; break;
	case 88200:  fmt |= SDFMT_BASE_44KHZ | SDFMT_MULT_X2 | SDFMT_DIV_1; break;
	case 96000:  fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X2 | SDFMT_DIV_1; break;
	case 176400: fmt |= SDFMT_BASE_44KHZ | SDFMT_MULT_X4 | SDFMT_DIV_1; break;
	case 192000: fmt |= SDFMT_BASE_48KHZ | SDFMT_MULT_X4 | SDFMT_DIV_1; break;
	// PARAM_SUPP_PCM_SIZE_RATE has a bit for 384000Hz. However, stream
	// descriptors seem to have a max multiplier of 4 (anything higher is
	// reserved, according to the spec), so we're not going to support it.
	case 384000: 
	default:
		dprintf("unsupported sample rate %u\n", wavFmt->wf.nSamplesPerSec);
		return FALSE;
	}

	switch (wavFmt->wBitsPerSample)
	{
	case 8:  fmt |= SDFMT_BITS_8;  break;
	case 16: fmt |= SDFMT_BITS_16; break;
	case 20: fmt |= SDFMT_BITS_20; break;
	case 24: fmt |= SDFMT_BITS_24; break;
	case 32: fmt |= SDFMT_BITS_32; break;
	default:
		dprintf("unsupported bit depth %u\n", wavFmt->wBitsPerSample);
		return FALSE;
	}
	stream->sampleBits = wavFmt->wBitsPerSample;
	//stream->sampleBits = 16;

	int chanCount = wavFmt->wf.nChannels;
	if (chanCount < hwMinChannels)
		chanCount = hwMinChannels;
	if (chanCount > hwMaxChannels)
	{
		dprintf("cannot support %u channels\n", wavFmt->wf.nChannels);
		return FALSE;
	}
	stream->chanCount = chanCount;
	fmt |= chanCount - 1;

	stream->format = fmt;

	/*
	if (wavFmt->wBitsPerSample == 8 && stream->chanCount == 2 && wavFmt->wf.nChannels == 1)
		stream->converter = convert_1u8_2s8;
	else
		stream->converter = convert_identity;
	*/
	if (stream->sampleBits == wavFmt->wBitsPerSample && stream->chanCount == wavFmt->wf.nChannels)
		stream->converter = convert_identity;
	else
	{
		if (stream->chanCount == 2 && stream->sampleBits == 16
		 && wavFmt->wf.nChannels == 1 && wavFmt->wBitsPerSample == 8)
			stream->converter = convert_1u8_2s16;
		else if (stream->chanCount == 2 && stream->sampleBits == 8
		 && wavFmt->wf.nChannels == 1 && wavFmt->wBitsPerSample == 8)
			stream->converter = convert_1u8_2s8;
		else
			ASSERT(0);  // TODO
	}

	return TRUE;
}

// Starts the stream
static void hda_stream_open(struct HDAStream *stream)
{
	struct HDAStreamDesc *sdesc = &hdaRegs->SDESC[stream->index];

	ASSERT(stream->waveBuf != NULL);
	ASSERT(stream->bdl != NULL);

	// Write stream descriptor
	sdesc->SDBDPL = stream->bdlPhys;
	sdesc->SDBDPU = 0;
	sdesc->SDLVI = stream->numBDLEntries - 1;
	sdesc->SDCBL = stream->waveBufSize;
	sdesc->SDCTLb2 = SDCTLb2_STRM(stream->streamTag);
	sdesc->SDFMT = stream->format;
	sdesc->SDCTLb0 |= SDCTLb0_IOCE;  // enable interrupt on completion

	dprintf("hda_stream_start: SDSTS=0x%02X\n", sdesc->SDSTS);

	// set format for all Audio Output widgets
	struct HDACodec *codec = codecs;
	for (int i = 0; i < codecsCount; i++, codec++)
	{
		struct HDAWidget *widget = codec->afg.widgets;
		for (int j = 0; j < codec->afg.widgetsCount; j++, widget++)
		{
			if (widget->streamTag == stream->streamTag)
			{
				dprintf("enabling widget #%i for stream %i\n", widget->nodeID, stream->streamTag);
				uint32_t commands[3], responses[3];
				commands[0] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONVERTER_FORMAT, stream->format);
				commands[1] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONVERTER_STREAM_CHANNEL, (stream->streamTag << 4) | 0);
				commands[2] = MAKE_COMMAND(codec->addr, widget->nodeID, VERB_SET_CONV_CHAN_COUNT, stream->chanCount - 1);
				hda_run_commands(commands, responses, 3);
			}
		}
	}

	__asm wbinvd  // flush cache

	sdesc->SDCTLb0 |= SDCTLb0_RUN;  // start the stream

	if (sdesc->SDSTS & SDSTS_FIFOE)
	{
		dprintf("FIFO error!\n");
		sdesc->SDSTS |= SDSTS_FIFOE;  // clear any pending FIFO error
		if (sdesc->SDSTS & SDSTS_FIFOE)
			dprintf("still FIFO error, WTF?\n");
		BKPT
	}
}

static void hda_stream_close(struct HDAStream *stream)
{
	dprintf("hda_stream_close\n");
	hdaRegs->SDESC[stream->index].SDCTLb0 &= ~SDCTLb0_RUN;
	hda_stream_reset(stream);
}

static void hda_stream_add_block(struct HDAStream *stream, WAVEHDR *wavHdr, DWORD wavHdrSegOff, void *data)
{
	dprintf("hda_stream_add_block(wavHdr=0x%08X, data=0x%08X), size=0x%X\n",
		wavHdr, data, wavHdr->dwBufferLength);
	// TODO: This allocated memory is used at interrupt time. Make sure it can't be paged out!
	struct AudioBlock *block = memory_alloc(sizeof(*block));
	block->wavHdr = wavHdr;
	block->wavHdrSegOff = wavHdrSegOff;
	block->data = data;
	block->bytesWritten = 0;
	block->next = NULL;
	uint16_t iflag = disable_interrupts();  // don't let the interrupt handler mess with us
	// Append to block list
	if (stream->blockList == NULL)
		stream->blockList = block;
	else
	{
		struct AudioBlock *last = stream->blockList;
		while (last->next != NULL)
			last = last->next;
		last->next = block;
	}
	restore_interrupts(iflag);
}

static void __cdecl release_audio_block_appy_time(DWORD waveHdrSegOff)
{
	DWORD result = _SHELL_CallDll("HDAUDIO", "wave_block_finished", sizeof(waveHdrSegOff), &waveHdrSegOff);
	if (result == 0)
	{
		dprintf("failed to call ring-3 driver\n");
		BKPT
	}
	else
		dprintf("dll result: %u\n", result);
}

static void release_block(struct HDAStream *stream, struct AudioBlock *block)
{
	dprintf("release_block 0x%08X\n", block);
	// TODO: schedule appy time
	// Ring-3 code can only be called at "appy-time", and certainly not in an
	// interrupt handler, so we schedule an appy-time event to notify the ring-3
	// driver that we are finished with the block.
	_SHELL_CallAtAppyTime(release_audio_block_appy_time, block->wavHdrSegOff, CAAFL_RING0, 0);

	ASSERT(block == stream->blockList);
	stream->blockList = block->next;
	memory_free(block);
}

static void stream_interrupt(int streamIndex)
{
	struct HDAStreamDesc *sdesc = &hdaRegs->SDESC[streamIndex];
	uint8_t sdsts = sdesc->SDSTS;

	if (sdsts & SDSTS_FIFOE)
	{
		// TODO: Find out what's causing these damn FIFO errors and prevent them
		// from happening!
		dprintf("stream %i FIFO error\n", streamIndex);
		BKPT
	}
	if (sdsts & SDSTS_DESE)
	{
		dprintf("stream %i descriptor error\n", streamIndex);
		BKPT
	}
	if (sdsts & SDSTS_BCIS)
	{
		dprintf("stream %i buffer complete\n", streamIndex);
		if (streamIndex == outStream.index)
		{
			struct HDAStream *stream = &outStream;
			struct AudioBlock *block = stream->blockList;
			size_t destBytesLeft = STREAM_CHUNK_SIZE;

			ASSERT(stream->currPos % STREAM_CHUNK_SIZE == 0);
			// Write a whole chunk's worth of data
			while (block != NULL && destBytesLeft > 0)
			{
				size_t destSize = destBytesLeft;
				size_t srcSize = block->wavHdr->dwBufferLength - block->bytesWritten;
				stream->converter(
					(uint8_t *)stream->waveBuf + stream->currPos,  // dest
					(uint8_t *)block->data + block->bytesWritten,  // src
					&destSize,  // destSize
					&srcSize);  // srcSize
				//dprintf("wrote %i bytes\n", destSize);
				block->bytesWritten += srcSize;
				stream->currPos += destSize;
				destBytesLeft -= destSize;
				ASSERT(block->bytesWritten <= block->wavHdr->dwBufferLength);
				if (block->bytesWritten == block->wavHdr->dwBufferLength)
				{
					// done with that block, move on to next
					struct AudioBlock *next = block->next;
					release_block(stream, block);
					block = next;
				}
			}
			if (destBytesLeft > 0)  // pad with zeros
			{
				memset((uint8_t *)stream->waveBuf + stream->currPos, 0, destBytesLeft);
				stream->currPos += destBytesLeft;
			}
			__asm wbinvd  // flush cache
			stream->currPos %= stream->waveBufSize;
			ASSERT(stream->currPos % STREAM_CHUNK_SIZE == 0);
		}
	}

	sdesc->SDSTS = sdsts;
}

static void interrupt_handler(HIRQ hIRQ, HVM hVM)
{
	//dprintf("interrupt_handler(0x%08X, 0x%08X)\n", hIRQ, hVM);
	uint32_t intsts = hdaRegs->INTSTS;
	//dprintf("intsts = 0x%08X\n", intsts);
	BOOL handled = FALSE;

	if (intsts & INTSTS_GIS)  // Is the interrupt for us?
	{
		handled = TRUE;
		if (intsts & INTSTS_CIS)  // controller interrupt
		{
			dprintf("controller interrupt\n");
			BKPT
		}
		if (intsts & INTSTS_SIS_MASK)  // stream interrupt
			for (int i = 0; i <= 29; i++)
				if (intsts & (1 << i))
					stream_interrupt(i);
	}
	VPICD_Phys_EOI(hIRQ);
	if (handled)
	{
		__asm clc
	}
	else  // probably for some other device sharing the same IRQ
	{
		__asm stc
	}
}
#pragma aux interrupt_handler \
	__parm [eax] [ebx]

static BOOL install_interrupt_handler(void)
{
	VID vid = { 0 };

	vid.VID_Options = VPICD_OPT_CAN_SHARE;
	vid.VID_IRQ_Number = hdaIRQNum;
	vid.VID_Hw_Int_Proc = (ULONG)interrupt_handler;
	hIRQ = VPICD_Virtualize_IRQ(&vid);
	if (hIRQ == 0)
	{
		dprintf("failed to install interrupt handler\n");
		BKPT
		return FALSE;
	}
	VPICD_Physically_Unmask(hIRQ);
	return TRUE;
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
		hdaQuirks |= HDA_QUIRK_FORCE_STEREO;
		// Get the hardware resource configuration from Configuration Manager
		result = CM_Get_Alloc_Log_Conf(&conf, devnode, CM_GET_ALLOC_LOG_CONF_ALLOC);
		if (result != CR_SUCCESS)
		{
			dprintf("failed to get device configuration: result %u\n", result);
			BKPT
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
			BKPT
			return CR_FAILURE;
		}
		if (conf.dMemLength[0] < 0x2000)
		{
			dprintf("MMIO space too small!\n");
			BKPT
			return CR_FAILURE;
		}
		if (conf.wNumIRQs < 1)
		{
			dprintf("no IRQ available!\n");
			BKPT
			return CR_FAILURE;
		}

		hdaIRQNum = conf.bIRQRegisters[0];
		hdaRegs = memory_map_phys_to_virt(conf.dMemBase[0], conf.dMemLength[0]);
		if (hdaRegs == NULL)
		{
			dprintf("failed to map HDA MMIO!\n");
			BKPT
			return CR_OUT_OF_MEMORY;
		}

		dprintf("HDA MMIO: physical=0x%08X, virtual=0x%08X, length=0x%X\n", conf.dMemBase[0], hdaRegs, conf.dMemLength[0]);
		dprintf("HDA controller version: %i.%i\n", hdaRegs->VMAJ, hdaRegs->VMIN);

		// Now, initialize the hardware
		if (!install_interrupt_handler())
			return CR_FAILURE;
		if (!hda_controller_reset())
			return CR_FAILURE;
		if (!hda_controller_setup_corb_rirb())
			return CR_FAILURE;
		if (!hda_controller_enum_codecs())
			return CR_FAILURE;
		if (!hda_output_stream_create(&outStream))
			return CR_FAILURE;
		return CR_SUCCESS;
	}
	return CR_DEFAULT;
}

// Processes control codes sent by the DeviceIoControl Win32 function
// Returns ERROR_SUCCESS on success, or an error code on failure.
// An application can call GetLastError() to retrieve this error code.
DWORD handle_win32_io(DIOCPARAMETERS *diocParams)
{
	DWORD *pBytesReturned = (DWORD *)diocParams->lpcbBytesReturned;
	if (pBytesReturned != NULL)
		*pBytesReturned = 0;
	switch (diocParams->dwIoControlCode)
	{
	case DIOC_OPEN:
		dprintf("DIOC_OPEN\n");
		return ERROR_SUCCESS;
	case DIOC_CLOSEHANDLE:
		dprintf("DIOC_CLOSEHANDLE\n");
		return ERROR_SUCCESS;
	case HDA_VXD_EXEC_VERB:
		dprintf("HDA_VXD_EXEC_VERB\n");
		int count = diocParams->cbInBuffer / sizeof(uint32_t);
		size_t retSize = count * sizeof(uint32_t);
		if (diocParams->cbOutBuffer < retSize)
			return ERROR_INSUFFICIENT_BUFFER;
		if (!hda_run_commands((uint32_t *)diocParams->lpvInBuffer, (uint32_t *)diocParams->lpvOutBuffer, count))
			return ERROR_GEN_FAILURE; 
		if (pBytesReturned != NULL)
			*pBytesReturned = retSize;
		return ERROR_SUCCESS;
	case HDA_VXD_GET_PCI_CONFIG:
		dprintf("HDA_VXD_GET_PCI_CONFIG\n");
		if (diocParams->cbOutBuffer < 256)
			return ERROR_INSUFFICIENT_BUFFER;
		CONFIGRET result = CM_Call_Enumerator_Function(
			hdaDevNode,
			PCI_ENUM_FUNC_GET_DEVICE_INFO,
			0,
			(void *)diocParams->lpvOutBuffer,
			256,
			0);
		if (result != CR_SUCCESS)
			return ERROR_GEN_FAILURE;
		if (pBytesReturned != NULL)
			*pBytesReturned = 256;
		return ERROR_SUCCESS;
	case HDA_VXD_GET_BASE_REGS:
		dprintf("HDA_VXD_GET_BASE_REGS\n");
		if (diocParams->cbOutBuffer < sizeof(struct HDARegs))
			return ERROR_INSUFFICIENT_BUFFER;
		memcpy((void *)diocParams->lpvOutBuffer, hdaRegs, sizeof(struct HDARegs));
		if (pBytesReturned != NULL)
			*pBytesReturned = sizeof(struct HDARegs);
		return ERROR_SUCCESS;
	default:
		if (diocParams->dwIoControlCode >= HDA_VXD_GET_STREAM_DESC(0)
		 && diocParams->dwIoControlCode <  HDA_VXD_GET_STREAM_DESC(HDA_MAX_STREAMS))
		{
			int index = diocParams->dwIoControlCode - HDA_VXD_GET_STREAM_DESC(0);
			dprintf("HDA_VXD_GET_STREAM_DESC(%i)\n", index);
			if (diocParams->cbOutBuffer < sizeof(struct HDAStreamDesc))
				return ERROR_INSUFFICIENT_BUFFER;
			memcpy((void *)diocParams->lpvOutBuffer, &hdaRegs->SDESC[index], sizeof(struct HDAStreamDesc));
			if (pBytesReturned != NULL)
				*pBytesReturned = sizeof(struct HDAStreamDesc);
			return ERROR_SUCCESS;
		}
		else
			return ERROR_NOT_SUPPORTED;
	}

	// invalid control code
	dprintf("invalid Win32 control code 0x%X\n", diocParams->dwIoControlCode);
	BKPT
	return ERROR_NOT_SUPPORTED;
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
	X(W32_DEVICEIOCONTROL)
#undef X
	default:
		sprintf(buf, "(unknown 0x%X)", msg);
		return buf;
	}
}

// Handler for system control messages
// Return value is specific to the message
// Carry flag is cleared on success, or set on error
DWORD hda_vxd_control_proc(DWORD msg, DWORD paramEBX, DWORD paramEDX, DWORD paramESI)
{
	DWORD retVal = 0;

	hda_debug_init();

	dprintf("hda_vxd_control_proc(msg=%s, paramEBX=0x%X, paramEDX=0x%X)\n",
		control_msg_name(msg), paramEBX, paramEDX);

	switch (msg)
	{
	case PNP_NEW_DEVNODE:
		if (paramEDX == DLVXD_LOAD_DRIVER)
		{
			hdaDevNode = paramEBX;
			// Register ourselves as the driver
			dprintf("registering driver\n");
			MMDEVLDR_Register_Device_Driver(
				paramEBX,  // dnDevNode
				(DWORD)driver_config_handler,  // fnConfigHandler
				0);  // dwUserData
		}
		break;
	case W32_DEVICEIOCONTROL:
		retVal = handle_win32_io((DIOCPARAMETERS *)paramESI);
		break;
	}

	__asm clc  // clear carry flag
	return retVal;
}
#pragma aux hda_vxd_control_proc \
	__parm [eax] [ebx] [edx] [esi] \
	__value [eax]

void __cdecl hda_vxd_pm16_api_proc(HVM hVM, CLIENT_STRUCT *clientRegs)
{
	switch (clientRegs->CWRS.Client_AX)
	{
	case HDA_VXD_GET_CAPABILITIES:
		;
		dprintf("HDA_VXD_GET_CAPABILITIES\n");
		// WAVEOUTCAPS struct in es:si registers of client
		WAVEOUTCAPS *wc = Map_Flat(
			offsetof(struct Client_Reg_Struc, Client_ES),
			offsetof(struct Client_Word_Reg_Struc, Client_SI));
		pci_read_word(&wc->wMid, hdaDevNode, 0);
		pci_read_word(&wc->wPid, hdaDevNode, 2);
		wc->vDriverVersion = (DRV_VER_MAJOR << 8) | DRV_VER_MINOR;
		// TODO: get the actual device name (from CONFIGMG or the registry)
		strcpy(wc->szPname, "HD Audio");
		wc->dwFormats = WAVE_FORMAT_1M08 | WAVE_FORMAT_1M16 | WAVE_FORMAT_1S08 | WAVE_FORMAT_1S16
					  | WAVE_FORMAT_2M08 | WAVE_FORMAT_2M16 | WAVE_FORMAT_2S08 | WAVE_FORMAT_2S16
					  | WAVE_FORMAT_4M08 | WAVE_FORMAT_4M16 | WAVE_FORMAT_4S08 | WAVE_FORMAT_4S16
					  /*| WAVE_FORMAT_96M08 | WAVE_FORMAT_96M16 | WAVE_FORMAT_96S08 | WAVE_FORMAT_96S16*/;
		wc->wChannels = 2;
		wc->dwSupport = WAVECAPS_LRVOLUME|WAVECAPS_VOLUME/*|WAVECAPS_SAMPLEACCURATE*/;
		break;
	case HDA_VXD_OPEN_STREAM:
		dprintf("HDA_VXD_OPEN_STREAM\n");
		// PCMWAVEFORMAT struct in es:si registers of client
		const PCMWAVEFORMAT *wavFmt = Map_Flat(
			offsetof(struct Client_Reg_Struc, Client_ES),
			offsetof(struct Client_Word_Reg_Struc, Client_SI));
		if (!hda_stream_set_format(&outStream, wavFmt))
			goto failure;
		hda_stream_open(&outStream);
		break;
	case HDA_VXD_CLOSE_STREAM:
		dprintf("HDA_VXD_CLOSE_STREAM\n");
		hda_stream_close(&outStream);
		break;
	case HDA_VXD_SUBMIT_WAVE_BLOCK:
		dprintf("HDA_VXD_SUBMIT_WAVE_BLOCK\n");
		// WAVEHDR struct in es:si registers of client
		DWORD wavHdrSegOff = (clientRegs->CRS.Client_ES << 16) | clientRegs->CWRS.Client_SI;
		WAVEHDR *wavHdr = Map_Flat(
			offsetof(struct Client_Reg_Struc, Client_ES),
			offsetof(struct Client_Word_Reg_Struc, Client_SI));
		// Get the address of lpData
		WORD prevES = clientRegs->CRS.Client_ES;
		WORD prevSI = clientRegs->CWRS.Client_SI;
		clientRegs->CRS.Client_ES = HIWORD(wavHdr->lpData);
		clientRegs->CWRS.Client_SI = LOWORD(wavHdr->lpData);
		void *lpData = Map_Flat(
			offsetof(struct Client_Reg_Struc, Client_ES),
			offsetof(struct Client_Word_Reg_Struc, Client_SI));
		clientRegs->CRS.Client_ES = prevES;
		clientRegs->CWRS.Client_SI = prevSI;
		hda_stream_add_block(&outStream, wavHdr, wavHdrSegOff, lpData);
		break;
	default:
		dprintf("hda_vxd_pm16_api_proc: bad function code %u\n", clientRegs->CWRS.Client_AX);
		goto failure;
	}

	clientRegs->CBRS.Client_AL = 1;
	return;

failure:
	clientRegs->CBRS.Client_AL = 0;
	BKPT
}
