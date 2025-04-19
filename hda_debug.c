// Debugging routines for the VxD

#include <string.h>
#include <windows.h>
#include <vmm.h>

#include "tinyprintf.h"
#include "hdaudio.h"

static BOOL initialized = FALSE;

// Prints a single character to the debug console
// Used by tinyprintf
static void putc(void *unused, char c)
{
	Out_Debug_Chr(c);
}

// Initializes dprintf support
void hda_debug_init(void)
{
	if (!initialized)
	{
		init_printf(NULL, putc);
		initialized = TRUE;
	}
}

void hda_debug_dump_regs(void)
{
	dprintf("=== HDA Register Dump ===\n");
	dprintf("GCAP:       %04X\n"
	       "VMIN:       %02X       VMAJ:      %02X\n"
	       "OUTPAY:     %04X     INPAY:     %04X\n"
	       "GCTL:       %08X\n"
	       "WAKEEN:     %04X\n"
	       "STATESTS:   %04X\n"
	       "GSTS:       %04X\n"
	       "OUTSTRMPAY: %04X     INSTRMPAY: %04X\n"
	       "INTCTL:     %08X INTSTS:    %08X\n"
	       "WALCLK:     %08X\n"
	       "SSYNC:      %08X\n",
	       hdaRegs->GCAP,
	       hdaRegs->VMIN, hdaRegs->VMAJ,
	       hdaRegs->OUTPAY, hdaRegs->INPAY,
	       hdaRegs->GCTL,
	       hdaRegs->WAKEEN,
	       hdaRegs->STATESTS,
	       hdaRegs->GSTS,
	       hdaRegs->OUTSTRMPAY, hdaRegs->INSTRMPAY,
	       hdaRegs->INTCTL, hdaRegs->INTSTS,
	       hdaRegs->WALCLK,
	       hdaRegs->SSYNC);
	dprintf("CORBLBASE:  %08X CORBUBASE: %08X\n"
	       "CORBWP:     %04X     CORBRP:    %04X\n"
	       "CORBCTL:    %02X       CORBSTS:   %02X       CORBSIZE: %02X\n",
	       hdaRegs->CORBLBASE, hdaRegs->CORBUBASE,
	       hdaRegs->CORBWP, hdaRegs->CORBRP,
	       hdaRegs->CORBCTL, hdaRegs->CORBSTS, hdaRegs->CORBSIZE);
	dprintf("RIRBLBASE:  %08X RIRBUBASE: %08X\n"
	       "RIRBWP:     %04X     RINTCNT:   %04X\n"
	       "RIRBCTL:    %02X       RIRBSTS:   %02X       RIRBSIZE: %02X\n",
	       hdaRegs->RIRBLBASE, hdaRegs->RIRBUBASE,
	       hdaRegs->RIRBWP, hdaRegs->RINTCNT,
	       hdaRegs->RIRBCTL, hdaRegs->RIRBSTS, hdaRegs->RIRBSIZE);
	dprintf("DPLBASE:    %08X DPUBASE:   %08X\n",
	       hdaRegs->DPLBASE, hdaRegs->DPUBASE);
	uint16_t gcap = hdaRegs->GCAP;
	int iss = GCAP_ISS(gcap);
	int oss = GCAP_OSS(gcap);
	int bss = GCAP_BSS(gcap);
	int nStreams = iss + oss + bss;
	for (int i = 0; i < nStreams; i++)
	{
		struct HDAStreamDesc *sdesc = &hdaRegs->SDESC[i];
		const char *type = "input";
		if (i >= iss)
			type = "output";
		if (i >= iss + oss)
			type = "bidirectional";
		dprintf("Stream %i (%s)\n", i, type);
		dprintf("SDCTL:  %02X%02X%02X   SDSTS:   %02X\n"
		       "SDLPIB: %08X SDCBL:   %08X\n"
		       "SDLVI:  %04X     SDFIFOS: %04X     SDFMT: %04X\n"
		       "SDBDPL: %08X SDBDPU:  %08X\n",
		       sdesc->SDCTLb2, sdesc->SDCTLb1, sdesc->SDCTLb0, sdesc->SDSTS,
		       sdesc->SDLPIB, sdesc->SDCBL,
		       sdesc->SDLVI, sdesc->SDFIFOS, sdesc->SDFMT,
		       sdesc->SDBDPL, sdesc->SDBDPU);
	}
	dprintf("=== End HDA Register Dump ===\n");
}

static const char *funcgrp_type_name(int type)
{
	ASSERT(type >= 0 && type <= 255);
	if (type >= 0x80)
		return "Vendor Defined";
	switch (type)
	{
	case 1: return "Audio";
	case 2: return "Modem";
	}
	return "Reserved";
}

static const char *widget_type_name(int type)
{
	ASSERT(type >= 0 && type <= 15);
	switch (type)
	{
	case 0: return "Audio Output";
	case 1: return "Audio Input";
	case 2: return "Audio Mixer";
	case 3: return "Audio Selector";
	case 4: return "Pin Complex";
	case 5: return "Power Widget";
	case 6: return "Volume Knob";
	case 7: return "Beep Generator";
	case 15: return "Vendor Defined";
	}
	return "Reserved";
}

static const char *bool_str(int val)
{
	return val ? "YES" : "NO";
}

struct Flag
{
	int bit;
	const char *name;
};

static char *flag_list(uint32_t value, const struct Flag *flags, int flagsCount)
{
	static char buf[1024];
	char *dest = buf;

	for (int i = 0; i < flagsCount; i++)
	{
		if (value & (1 << flags[i].bit))
		{
			if (dest > buf)
			{
				*dest++ = ',';
				*dest++ = ' ';
			}
			strcpy(dest, flags[i].name);
			dest += strlen(flags[i].name);
		}
	}
	*dest = 0;
	return buf;
}

void hda_debug_dump_widget(struct HDACodec *codec, nodeid_t wID)
{
	static const char spaces[] = "        ";
	const char *indent = spaces + strlen(spaces) - 4;

	uint32_t cmds[8], resps[8];
	uint32_t caps;
	int type;

	cmds[0] = MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_AUDIO_WIDGET_CAP);
	hda_run_commands(cmds, resps, 1);
	caps = resps[0];
	type = GET_BITS(caps, 20, 4);

	printf("%sWidget #%i (%s)\n",
		indent, wID, widget_type_name(type));
	indent--;

	if (type == WIDGET_TYPE_VENDOR_DEFINED)
		return;

	printf("%sParameters:\n",
		indent);
	indent--;

	if (type == WIDGET_TYPE_AUDIO_INPUT
	 || type == WIDGET_TYPE_AUDIO_OUTPUT
	 || type == WIDGET_TYPE_AUDIO_SELECTOR
	 || type == WIDGET_TYPE_AUDIO_MIXER
	 || type == WIDGET_TYPE_PIN_COMPLEX)
	{
		printf("%sAUDIO_WIDGET_CAP: 0x%08X\n",
			indent, caps);
		indent--;
		static const struct Flag audWidgetCapFlags[] =
		{
			{ 1,  "Input Amp" },
			{ 2,  "Output Amp" },
			{ 3,  "Amp Param Override" },
			{ 4,  "Format Override" },
			{ 5,  "Stripe" },
			{ 6,  "Processor" },
			{ 7,  "Unsol Capable" },
			{ 8,  "Conn List" },
			{ 9,  "Digital" },
			{ 10, "Power Ctrl" },
			{ 11, "L-R Swap" },
			{ 12, "CP Caps" },
		};
		printf("%s%s\n",
			indent, flag_list(caps, audWidgetCapFlags, ARRAY_COUNT(audWidgetCapFlags)));
		printf("%sType=%s, Delay=%i, ChannelCount=%i\n",
			indent, widget_type_name(type), GET_BITS(caps, 16, 4), (GET_BITS(caps, 13, 3) << 1) + (caps & 1) + 1);
		indent++;
	}

	if (type == WIDGET_TYPE_AUDIO_INPUT || type == WIDGET_TYPE_AUDIO_OUTPUT)
	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_SUPP_PCM_SIZE_RATE), &resp);
		printf("%sSUPP_PCM_SIZE_RATE: 0x%08X\n",
			indent, resp);
		indent--;
		static const struct Flag rateFlags[] =
		{
			{  0, "8000Hz" },
			{  1, "11025Hz" },
			{  2, "16000Hz" },
			{  3, "22050Hz" },
			{  4, "32000Hz" },
			{  5, "44100Hz" },
			{  6, "48000Hz" },
			{  7, "88200Hz" },
			{  8, "96000Hz" },
			{  9, "176400Hz" },
			{ 10, "192000Hz" },
			{ 11, "384000Hz" },
		};
		printf("%sRates: %s\n",
			indent, flag_list(resp, rateFlags, ARRAY_COUNT(rateFlags)));
		static const struct Flag bitDepthFlags[] =
		{
			{ 16, "8-bit" },
			{ 17, "16-bit" },
			{ 18, "20-bit" },
			{ 19, "24-bit" },
			{ 20, "32-bit" },
		};
		printf("%sBit Depths: %s\n",
			indent, flag_list(resp, bitDepthFlags, ARRAY_COUNT(bitDepthFlags)));
		indent++;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_SUPP_STREAM_FORMATS), &resp);
		printf("%sSUPP_STREAM_FORMATS: 0x%08X\n",
			indent, resp);
		indent--;
		static const struct Flag formatFlags[] =
		{
			{ 0, "PCM" },
			{ 1, "Float32" },
			{ 2, "DolbyAC3" },
		};
		printf("%s%s\n",
			indent, flag_list(resp, formatFlags, ARRAY_COUNT(formatFlags)));
		indent++;
	}

	if (type == WIDGET_TYPE_PIN_COMPLEX)
	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_PIN_CAP), &resp);
		printf("%sPIN_CAP: 0x%08X\n",
			indent, resp);
		indent--;
		static const struct Flag pinCapFlags[] =
		{
			{  0, "Impedance Sense" },
			{  1, "Trigger Reqd" },
			{  2, "Pres Detect" },
			{  3, "HdPh Drive" },
			{  4, "Output" },
			{  5, "Input" },
			{  6, "Bal. I/O Pins" },
			{  7, "HDMI" },
			{  8, "VRef-Hi-Z" },
			{  9, "VRef-50%" },
			{ 10, "VRef-Ground" },
			{ 12, "VRef-80%" },
			{ 13, "VRef-100%" },
			{ 16, "EAPD" },
			{ 24, "DisplayPort" },
			{ 27, "High Bit Rate" },
		};
		printf("%s%s\n",
			indent, flag_list(resp, pinCapFlags, ARRAY_COUNT(pinCapFlags)));
		indent++;
	}

	if (caps & (WIDGET_CAP_INPUT_AMP))
	{
		uint32_t resp;
		
		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_INPUT_AMP_CAP), &resp);
		printf("%sINPUT_AMP_CAP: 0x%08X\n",
			indent, resp);
		indent--;
		printf("%sOffset=%i, NumSteps=%i, StepSize=%i, MuteCapable=%s\n",
			indent, GET_BITS(resp, 0, 7), GET_BITS(resp, 8, 7), GET_BITS(resp, 16, 7), bool_str(resp & (1 << 31)));
		indent++;
	}

	if (caps & (WIDGET_CAP_OUTPUT_AMP))
	{
		uint32_t resp;
		
		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_OUTPUT_AMP_CAP), &resp);
		printf("%sOUTPUT_AMP_CAP: 0x%08X\n",
			indent, resp);
		indent--;
		printf("%sOffset=%i, NumSteps=%i, StepSize=%i, MuteCapable=%s\n",
			indent, GET_BITS(resp, 0, 7), GET_BITS(resp, 8, 7), GET_BITS(resp, 16, 7), bool_str(resp & (1 << 31)));
		indent++;
	}

	if (caps & (WIDGET_CAP_CONN_LIST))
	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_CONN_LIST_LENGTH), &resp);
		printf("%sCONN_LIST_LENGTH: 0x%08X (%i, %s form)\n",
			indent, resp, GET_BITS(resp, 0, 7), (resp & (1 << 7)) ? "long" : "short");
	}

	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_SUPP_POWER_STATES), &resp);
		printf("%sSUPP_POWER_STATES: 0x%08X\n",
			indent, resp);
		indent--;
		static const struct Flag powerStateFlags[] =
		{
			{  0, "D0" },
			{  1, "D1" },
			{  2, "D2" },
			{  3, "D3" },
			{  4, "D3Cold" },
			{ 29, "S3D3Cold" },
			{ 30, "ClkStop" },
			{ 31, "EPSS" },
		};
		printf("%s%s\n",
			indent, flag_list(resp, powerStateFlags, ARRAY_COUNT(powerStateFlags)));
		indent++;
	}

	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_PROCESSING_CAP), &resp);
		printf("%sPROCESSING_CAP: 0x%08X (BenignCap=%s, NumCoeff=%i)\n",
			indent, resp, bool_str(resp & (1 << 0)), GET_BITS(resp, 8, 8));
	}

	if (0)  // only for function groups
	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_GPIO_COUNT), &resp);
		printf("%sGPIO_COUNT: 0x%08X (NumGPIOs=%i, NumGPOs=%i, NumGPIs=%i, GPIUnsol=%s, GPIWake=%s)\n",
			indent, resp, GET_BITS(resp, 0, 8), GET_BITS(resp, 8, 8), GET_BITS(resp, 16, 8), bool_str(resp & (1 << 30)), bool_str(resp & (1 << 31)));
	}

	if (type == WIDGET_TYPE_VOLUME_KNOB)
	{
		uint32_t resp;

		hda_run_command(MAKE_COMMAND(codec->addr, wID, VERB_GET_PARAMETER, PARAM_VOLUME_KNOB_CAP), &resp);
		printf("%sVOLUME_KNOB_CAP: 0x%08X (NumSteps=%i, Delta=%s)\n",
			indent, resp, GET_BITS(resp, 0, 7), bool_str(resp & (1 << 7)));
	}

	indent++;
}

void hda_debug_dump_codec(struct HDACodec *codec)
{
	uint32_t cmds[8];
	uint32_t resps[8];
	int fgIDStart, fgIDCount;

	printf("Codec #%i\n", codec->addr);

	cmds[0] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_VENDOR_ID);
	cmds[1] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_REVISION_ID);
	cmds[2] = MAKE_COMMAND(codec->addr, 0, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
	hda_run_commands(cmds, resps, 3);
	fgIDCount = GET_BITS(resps[2], 0, 8);
	fgIDStart = GET_BITS(resps[2], 16, 8);
	printf(" Vendor=0x%04X, Device=0x%04X, Rev=%i.%i, RevID=%i, Stepping=%i\n",
		GET_BITS(resps[0], 16, 16), GET_BITS(resps[0], 0, 16),
		GET_BITS(resps[1], 20, 4), GET_BITS(resps[1], 16, 4), GET_BITS(resps[1], 8, 8), GET_BITS(resps[1], 0, 8));

	printf(" SubNodes: %i starting at %i\n", fgIDCount, fgIDStart);

	for (nodeid_t fgID = fgIDStart; fgID < fgIDStart + fgIDCount; fgID++)
	{
		int fgType;
		int wIDStart, wIDCount;

		printf("  Function Group #%i\n", fgID);
		cmds[0] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_PARAMETER, PARAM_FUNC_GRP_TYPE);
		cmds[1] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
		cmds[2] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_GPIO_DATA, 0);
		hda_run_commands(cmds, resps, 3);
		fgType = GET_BITS(resps[0], 0, 8);
		wIDCount = GET_BITS(resps[1], 0, 8);
		wIDStart = GET_BITS(resps[1], 16, 8);
		printf("   Type=%s (%i), UnSolCapable=%s\n", funcgrp_type_name(fgType), fgType, bool_str(resps[0] & (1 << 8)));
		printf("   GPIOData=0x%08X\n", resps[2]);

		if (fgType == 1)  // Audio Function Group
		{
			cmds[0] = MAKE_COMMAND(codec->addr, fgID, VERB_GET_PARAMETER, PARAM_AUDIO_FUNC_GRP_CAP);
			hda_run_commands(cmds, resps, 1);
			printf("   AFG Caps: OutDelay=%i, InDelay=%i, Beep=%s\n",
				GET_BITS(resps[0], 0, 4), GET_BITS(resps[0], 8, 4), bool_str(resps[0] & (1 << 16)));
		}

		printf("   SubNodes: %i starting at %i\n", wIDCount, wIDStart);
		for (nodeid_t wID = wIDStart; wID < wIDStart + wIDCount; wID++)
		{
			hda_debug_dump_widget(codec, wID);
		}
	}
}

void hda_debug_assert_fail(const char *expr, const char *file, int line)
{
	static char buffer[1024];
	__asm cli
	sprintf(
		buffer,
		"*** ASSERTION FAILED ***\r\n"
		"%s\r\n"
		"File: %s, Line: %i\r\n",
		expr, file, line);
	Out_Debug_String(buffer);
	Fatal_Error_Handler(buffer, 0);
}
