// Simple tool to debug and manipulate the HDA controller from userspace

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "hdaudio.h"
#include "hda_vxd_api.h"

struct EnumItem
{
	int value;
	const char *name;
};

static const struct EnumItem verbs[] =
{
#define X(name) { VERB_##name, #name }
	X(GET_CONVERTER_FORMAT),
	X(SET_CONVERTER_FORMAT),
	X(GET_AMP_GAIN_MUTE),
	X(SET_AMP_GAIN_MUTE),
	X(GET_PARAMETER),
	X(GET_CONNECTION_SELECT_CTRL),
	X(SET_CONNECTION_SELECT_CTRL),
	X(GET_CONNECTION_LIST_ENTRY),
	X(GET_PROCESSING_STATE),
	X(SET_PROCESSING_STATE),
	X(GET_IN_CONVERTER_SDI_SELECT),
	X(SET_IN_CONVERTER_SDI_SELECT),
	X(GET_POWER_STATE),
	X(SET_POWER_STATE),
	X(GET_CONVERTER_STREAM_CHANNEL),
	X(SET_CONVERTER_STREAM_CHANNEL),
	X(GET_PIN_CONTROL),
	X(SET_PIN_CONTROL),
	X(GET_UNSOLRESP),
	X(SET_UNSOLRESP),
	X(GET_PIN_SENSE),
	X(EXEC_PIN_SENSE),
	X(GET_EAPD_ENABLE),
	X(SET_EAPD_ENABLE),
	X(GET_DIGICONVERT),
	X(SET_DIGICONVERT0),
	X(SET_DIGICONVERT1),
	X(SET_DIGICONVERT2),
	X(SET_DIGICONVERT3),
	X(GET_GPI_DATA),
	X(SET_GPI_DATA),
	X(GET_GPIO_DATA),
	X(SET_GPIO_DATA),
	X(GET_CONFIG_DEFAULT),
	X(SET_CONFIG_DEFAULT0),
	X(SET_CONFIG_DEFAULT1),
	X(SET_CONFIG_DEFAULT2),
	X(SET_CONFIG_DEFAULT3),
	X(GET_CONV_CHAN_COUNT),
	X(SET_CONV_CHAN_COUNT),
	X(GET_ASP_CHAN_MAP),
	X(SET_ASP_CHAN_MAP),
#undef X
	{ 0 },
};

static const struct EnumItem params[] =
{
#define X(name) { PARAM_##name, #name }
	X(VENDOR_ID),
	X(REVISION_ID),
	X(SUB_NODE_COUNT),
	X(FUNC_GRP_TYPE),
	X(AUDIO_FUNC_GRP_CAP),
	X(AUDIO_WIDGET_CAP),
	X(SUPP_PCM_SIZE_RATE),
	X(SUPP_STREAM_FORMATS),
	X(PIN_CAP),
	X(INPUT_AMP_CAP),
	X(CONN_LIST_LENGTH),
	X(SUPP_POWER_STATES),
	X(PROCESSING_CAP),
	X(GPIO_COUNT),
	X(OUTPUT_AMP_CAP),
	X(VOLUME_KNOB_CAP),
#undef X
	{ 0 },
};

static void usage(const char *exec)
{
	printf("usage: %s [options]\n"
	       "High Definition Audio driver utility\n"
	       "options:\n"
	       "  -c                              List codecs\n"
	       "  -w                              Dump all widgets\n"
	       "  -r                              Dump HDA controller registers\n"
	       "  -v codec_id node_id verb param  Execute the specified verb\n"
	       "                                  verb may either be the the name of a verb or\n"
	       "                                  its value (see the -lv option)\n"
	       "  -p                              Print the PCI configuration space\n"
	       "  -lv                             List available verbs\n"
	       "  -lp                             List available parameters for the\n"
	       "                                  GET_PARAMETER verb\n"
	       "  -h                              Display this help message\n",
	       exec);
}

static const char *get_errmsg(void)
{
	static char buffer[512];

	DWORD error = GetLastError();
	FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		buffer,
		sizeof(buffer),
		NULL);
	return buffer;
}

static HANDLE open_device(void)
{
	HANDLE h = CreateFileA(
		"\\\\.\\HDAUDIO.VXD",
		0,
		0,
		NULL,
		CREATE_NEW,
		FILE_FLAG_DELETE_ON_CLOSE,
		NULL);
	if (h == INVALID_HANDLE_VALUE)
		printf("Could not open HD audio device: %s\n", get_errmsg());
	return h;
}

static void close_device(HANDLE h)
{
	CloseHandle(h);
}

static BOOL run_verbs(HANDLE hDevice, const uint32_t *commands, uint32_t *responses, unsigned int count)
{
	return DeviceIoControl(
		hDevice,
		HDA_VXD_EXEC_VERB,
		(void *)commands, count * sizeof(*commands),
		responses, count * sizeof(*responses),
		NULL,
		NULL);
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

static const char *bool_str(int n)
{
	return n ? "Yes" : "No";
}

static const char *vendor_name(int n)
{
	switch (n)
	{
	case 0x0456: return "Analog Devices";
	case 0x066F: return "SigmaTel";
	case 0x0D8C: return "C-Media Electronics";
	case 0x1013: return "Cirrus Logic";
	case 0x10DE: return "Nvidia";
	case 0x10EC: return "Realtek";
	case 0x1102: return "Creative";
	case 0x14F1: return "Conexant";
	case 0x1AF4: return "Red Hat";
	case 0x2109: return "VIA Labs";
	case 0x8086: return "Intel";
	default: return "Unknown";
	}
}

static const char *funcgrp_type_name(int n)
{
	if (n >= 0x80)
		return "Vendor Defined";
	switch (n)
	{
	case 1: return "Audio";
	case 2: return "Modem";
	}
	return "Reserved";
}

static const char *widget_type_name(int n)
{
	switch (n)
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

static const char *port_connectivity_name(int n)
{
	switch (n)
	{
	case 0: return "Jack";
	case 1: return "None";
	case 2: return "Fixed";
	case 3: return "Jack & Fixed";
	default: return "Unknown";
	}
}

static const char *default_device_name(int n)
{
	switch (n)
	{
	case  0: return "Line Out";
	case  1: return "Speaker";
	case  2: return "Headphones";
	case  3: return "CD";
	case  4: return "SPDIF Out";
	case  5: return "Digital Out";
	case  6: return "Modem Line Side";
	case  7: return "Modem Handset Side";
	case  8: return "Line In";
	case  9: return "AUX";
	case 10: return "Mic.";
	case 11: return "Telephony";
	case 12: return "SPDIF In";
	case 13: return "Digital In";
	case 14: return "Reserved";
	case 15: return "Other";
	default: return "Unknown";
	}
}

static const char *connection_type_name(int n)
{
	switch (n)
	{
	case  0: return "Unknown";
	case  1: return "1/8\"";
	case  2: return "1/4\"";
	case  3: return "ATAPI";
	case  4: return "RCA";
	case  5: return "Optical";
	case  6: return "Other Digital";
	case  7: return "Other Analog";
	case  8: return "Multichannel Analog (DIN)";
	case  9: return "XLR/Professional";
	case 10: return "RJ-11";
	case 11: return "Combination";
	case 15: return "Other";
	default: return "Unknown";
	}
}

static const char *color_name(int n)
{
	switch (n)
	{
	case 0: return "Unknown";
	case 1: return "Black";
	case 2: return "Gray";
	case 3: return "Blue";
	case 4: return "Green";
	case 5: return "Red";
	case 6: return "Orange";
	case 7: return "Yellow";
	case 8: return "Purple";
	case 9: return "Pink";
	case 10: case 11: case 12: case 13: return "Reserved";
	case 14: return "White";
	case 15: return "Other";
	default: return "Unknown";
	}
}

static const char *location_name(int n)
{
	static char buffer[50];
	static const char *loc1;
	static const char *loc2;

	switch (n)  // Special locations
	{
	case 0x07: return "Rear Panel";
	case 0x17: return "Riser";
	case 0x37: return "Inside Mobile Lid";
	case 0x08: return "Drive Bay";
	case 0x18: return "Digital Display";
	case 0x38: return "Outside Mobile Lid";
	case 0x19: return "ATAPI";
	}

	switch (GET_BITS(n, 0, 4))
	{
	case 0: loc1 = "N/A"; break;
	case 1: loc1 = "Rear"; break;
	case 2: loc1 = "Front"; break;
	case 3: loc1 = "Left"; break;
	case 4: loc1 = "Right"; break;
	case 5: loc1 = "Top"; break;
	case 6: loc1 = "Bottom"; break;
	case 7: case 8: case 9:
		return "Special";
	case 10: case 11: case 12: case 13: case 14: case 15:
		return "Reserved";
	default:
		return "Unknown";
	}
	switch (GET_BITS(n, 4, 2))
	{
	case 0: loc2 = "External"; break;
	case 1: loc2 = "Internal"; break;
	case 2: loc2 = "Separate Chassis"; break;
	case 3: loc2 = "Other"; break;
	}
	sprintf(buffer, "%s %s\n", loc1, loc2);
	return buffer;
}

static int list_codecs(void)
{
	uint32_t commands[2], responses[2];
	HANDLE hDevice = open_device();
	if (hDevice == INVALID_HANDLE_VALUE)
		return 1;
	uint16_t codecBits;
	BOOL success = DeviceIoControl(
		hDevice,
		HDA_VXD_GET_CODECS,
		NULL, 0,
		&codecBits, sizeof(codecBits),
		NULL,
		NULL);
	if (!success)
		goto error;
	for (int i = 0; i < 16; i++)
	{
		if (codecBits & (1 << i))
		{
			commands[0] = MAKE_COMMAND(i, 0, VERB_GET_PARAMETER, PARAM_VENDOR_ID);
			commands[1] = MAKE_COMMAND(i, 0, VERB_GET_PARAMETER, PARAM_REVISION_ID);
			if (!run_verbs(hDevice, commands, responses, 2))
				goto error;
			printf("Codec %i: \n"
				   "  Vendor: 0x%04X (%s)\n"
				   "  Device: 0x%04X\n"
				   "  MajRev: %i, MinRev: %i, Rev: %i, Stepping: %i\n",
				i,
				VENDOR_ID_VEND_ID(responses[0]), vendor_name(VENDOR_ID_VEND_ID(responses[0])),
				VENDOR_ID_DEV_ID(responses[0]),
				REVISION_ID_MAJ_REV(responses[1]), REVISION_ID_MIN_REV(responses[1]), REVISION_ID_REV_ID(responses[1]), REVISION_ID_STEP_ID(responses[1]));
		}
	}
	close_device(hDevice);
	return 0;
error:
	printf("Command failed: %s\n", get_errmsg());
	close_device(hDevice);
	return 1;
}

static BOOL dump_widget(HANDLE hDevice, int cAddr, int nodeID)
{
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
	static const struct Flag pinCapFlags[] =
	{
		{  0, "Impedance Sense" },
		{  1, "Trigger Reqd" },
		{  2, "Pres Detect" },
		{  3, "Headphone Drive" },
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
	static const struct Flag pinControlFlags[] =
	{
		{ 5, "Input Enable" },
		{ 6, "Output Enable" },
		{ 7, "Headphone Enable" },
	};

	uint32_t commands[8], responses[8];
	printf("        Widget #%i\n", nodeID);
	commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_AUDIO_WIDGET_CAP);
	commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_CONN_LIST_LENGTH);
	if (!run_verbs(hDevice, commands, responses, 2))
		return FALSE;
	uint32_t caps = responses[0];
	int type = AUDIO_WIDGET_CAP_TYPE(responses[0]);
	puts("          Parameters:");
	printf("            AUDIO_WIDGET_CAP: 0x%08X\n", responses[0]);
	printf("              (type: %s, channels: %i, delay: %i, caps: %s)\n",
		widget_type_name(AUDIO_WIDGET_CAP_TYPE(responses[0])),
		AUDIO_WIDGET_CAP_CHANS(responses[0]),
		AUDIO_WIDGET_CAP_DELAY(responses[0]),
		flag_list(responses[0], audWidgetCapFlags, ARRAY_COUNT(audWidgetCapFlags)));
	if (caps & WIDGET_CAP_CONN_LIST)
		printf("            CONN_LIST_LENGTH: 0x%08X\n", responses[1]);

	commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_INPUT_AMP_CAP);
	commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_OUTPUT_AMP_CAP);
	if (!run_verbs(hDevice, commands, responses, 2))
		return FALSE;
	for (int i = 0; i < 2; i++)
	{
		printf("            %s_AMP_CAP: 0x%08X\n", (i == 0) ? "INPUT" : "OUTPUT", responses[i]);
		printf("              (offset: %i, steps: %i, stepSize: %.2fdB, MuteCapable=%s)\n",
			AMP_CAP_OFFSET(responses[i]),
			AMP_CAP_NUM_STEPS(responses[i]),
			0.25f * (1 + AMP_CAP_STEP_SIZE(responses[i])),
			bool_str(responses[i] & AMP_CAP_MUTE_CAPABLE));
	}

	if (type == WIDGET_TYPE_PIN_COMPLEX)
	{
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_PIN_CAP);
		if (!run_verbs(hDevice, commands, responses, 1))
			return FALSE;
		printf("            PIN_CAP: 0x%08X\n", responses[0]);
		printf("              (%s)\n", flag_list(responses[0], pinCapFlags, ARRAY_COUNT(pinCapFlags)));
	}

	if (caps & WIDGET_CAP_CONN_LIST)
	{
		puts("          Connections:");
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_CONN_LIST_LENGTH);
		commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_CONNECTION_SELECT_CTRL, 0);
		if (!run_verbs(hDevice, commands, responses, 2))
			return FALSE;
		BOOL longForm = (responses[0] & (1 << 7)) != 0;
		int numConnections = responses[0] & 0x3F;
		int perSet = longForm ? 2 : 4;
		int selected = GET_BITS(responses[1], 0, 8);
		printf("            ");
		for (int i = 0; i < numConnections; i++)
		{
			int index = i % perSet;
			if (index == 0)  // fetch the next set of entries
			{
				commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_CONNECTION_LIST_ENTRY, i - index);
				if (!run_verbs(hDevice, commands, responses, 1))
					return FALSE;
			}
			int connID = longForm ? ((responses[0] >> (index * 16)) & 0xFFFF)
			                      : ((responses[0] >> (index * 8)) & 0xFF);
			printf(i == selected ? "[%i]" : "%i", connID);
			printf("%c", i == numConnections - 1 ? '\n' : ' ');
		}
	}

	puts("          Controls:");
	if (type == WIDGET_TYPE_AUDIO_INPUT || type == WIDGET_TYPE_AUDIO_OUTPUT)
	{
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_CONVERTER_FORMAT, 0);
		if (!run_verbs(hDevice, commands, responses, 1))
			return FALSE;
		printf("            CONVERTER_FORMAT: 0x%08X\n", responses[0]);
		static const char *const bitsTable[] = { "8", "16", "20", "24", "32", "reserved", "reserved", "reserved" };
		int base = (responses[0] & SDFMT_BASE_44KHZ) ? 44100 : 48000;
		int mul = 1 + GET_BITS(responses[0], 11, 3);  // technically, anything over x4 here is reserved.
		int div = 1 + GET_BITS(responses[0], 8, 3);
		printf("              (type: %s, channels: %i, bits: %s, rate: %iHz (%i*%i/%i))\n",
			(responses[0] & (1 << 15)) ? "non-PCM" : "PCM",
			1 + GET_BITS(responses[0], 0, 4),
			bitsTable[GET_BITS(responses[0], 4, 3)],
			base * mul / div,
			base,
			mul,
			div);
	}
	if (type == WIDGET_TYPE_PIN_COMPLEX)
	{
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_CONFIG_DEFAULT, 0);
		commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PIN_CONTROL, 0);
		if (!run_verbs(hDevice, commands, responses, 2))
			return FALSE;
		printf("            CONFIG_DEFAULT: 0x%08X\n", responses[0]);
		printf("              (DefAssoc: %i, Seq: %i, Connectivity: %s, DefDevice: %s, ConnType: %s, Location: %s, Color: %s, Misc: %i)\n",
			CONFIG_DEFAULT_DEF_ASSOC(responses[0]),
			CONFIG_DEFAULT_SEQUENCE(responses[0]),
			port_connectivity_name(CONFIG_DEFAULT_PORT_CONNECTIVITY(responses[0])),
			default_device_name(CONFIG_DEFAULT_DEF_DEVICE(responses[0])),
			connection_type_name(CONFIG_DEFAULT_CONN_TYPE(responses[0])),
			location_name(CONFIG_DEFAULT_DEF_LOCATION(responses[0])),
			color_name(CONFIG_DEFAULT_COLOR(responses[0])),
			CONFIG_DEFAULT_MISC(responses[0]));
		printf("            PIN_CONTROL: 0x%08X\n", responses[1]);
		printf("              (%s)\n", flag_list(responses[1], pinControlFlags, ARRAY_COUNT(pinControlFlags)));
	}
	static const char *const ampNames[] = { "L Input", "R Input", "L Output", "R Output" };
	commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_AMP_GAIN_MUTE, GET_AMP_GAIN_MUTE_INPUT|GET_AMP_GAIN_MUTE_LEFT);
	commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_AMP_GAIN_MUTE, GET_AMP_GAIN_MUTE_INPUT|GET_AMP_GAIN_MUTE_RIGHT);
	commands[2] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_AMP_GAIN_MUTE, GET_AMP_GAIN_MUTE_OUTPUT|GET_AMP_GAIN_MUTE_LEFT);
	commands[3] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_AMP_GAIN_MUTE, GET_AMP_GAIN_MUTE_OUTPUT|GET_AMP_GAIN_MUTE_RIGHT);
	if (!run_verbs(hDevice, commands, responses, 4))
		return FALSE;
	for (int i = 0; i < 4; i++)
	{
		printf("            AMP_GAIN_MUTE (%s): 0x%08X\n", ampNames[i], responses[i]);
		printf("              (Gain: %i, Mute: %s)\n",
			AMP_GAIN_MUTE_GAIN(responses[i]), bool_str(responses[i] & AMP_GAIN_MUTE_MUTE));
	}
	if (type == WIDGET_TYPE_AUDIO_INPUT || type == WIDGET_TYPE_AUDIO_OUTPUT)
	{
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_CONVERTER_STREAM_CHANNEL, 0);
		if (!run_verbs(hDevice, commands, responses, 1))
			return FALSE;
		printf("            CONVERTER_STREAM_CHANNEL: 0x%08X\n", responses[0]);
		printf("              (Stream: %i, Channel: %i)\n",
			CONVERTER_STREAM_CHANNEL_STREAM(responses[0]),
			CONVERTER_STREAM_CHANNEL_CHANNEL(responses[0]));
	}
	return TRUE;
}

static BOOL dump_codec(HANDLE hDevice, int cAddr)
{
	uint32_t commands[8], responses[8];

	printf("Codec %i\n", cAddr);
	puts("Parameters:");
	commands[0] = MAKE_COMMAND(cAddr, 0, VERB_GET_PARAMETER, PARAM_VENDOR_ID);
	commands[1] = MAKE_COMMAND(cAddr, 0, VERB_GET_PARAMETER, PARAM_REVISION_ID);
	commands[2] = MAKE_COMMAND(cAddr, 0, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
	if (!run_verbs(hDevice, commands, responses, 3))
		return FALSE;
	printf("  VENDOR_ID: 0x%08X\n", responses[0]);
	printf("    (vendor: 0x%04X \"%s\", device: 0x%04X)\n",
		VENDOR_ID_VEND_ID(responses[0]),
		vendor_name(VENDOR_ID_VEND_ID(responses[0])),
		VENDOR_ID_DEV_ID(responses[0]));
	printf("  REVISION_ID: 0x%08X\n", responses[1]);
	printf("    (major: %i, minor: %i, rev: %i, step: %i)\n",
		REVISION_ID_MAJ_REV(responses[1]), REVISION_ID_MIN_REV(responses[1]), REVISION_ID_REV_ID(responses[1]), REVISION_ID_STEP_ID(responses[1]));
	printf("  SUB_NODE_COUNT: 0x%08X\n", responses[2]);
	printf("    (start: %i, count: %i)\n",
		SUB_NODE_COUNT_START_NODE(responses[2]), SUB_NODE_COUNT_NUM_NODES(responses[2]));

	int fgStart = SUB_NODE_COUNT_START_NODE(responses[2]);
	int fgCount = SUB_NODE_COUNT_NUM_NODES(responses[2]);
	puts("  Function Groups:");
	for (int nodeID = fgStart; nodeID < fgStart + fgCount; nodeID++)
	{
		printf("    Function group #%i\n", nodeID);
		puts("      Parameters:");
		commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_FUNC_GRP_TYPE);
		commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_SUPP_POWER_STATES);
		commands[2] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_SUB_NODE_COUNT);
		if (!run_verbs(hDevice, commands, responses, 3))
			return FALSE;
		printf("        FUNC_GRP_TYPE: 0x%08X\n", responses[0]);
		printf("          (type: %s, UnSolCapable: %s)\n",
			funcgrp_type_name(FUNC_GRP_TYPE_NODE_TYPE(responses[0])),
			bool_str(responses[0] & FUNC_GRP_TYPE_UNSOL_CAPABLE));
		printf("        SUPP_POWER_STATES: 0x%08X\n", responses[1]);
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
		printf("          (%s)\n", flag_list(responses[1], powerStateFlags, ARRAY_COUNT(powerStateFlags)));
		printf("        SUB_NODE_COUNT: 0x%08X\n", responses[2]);
		printf("          (start: %i, count: %i)\n",
			SUB_NODE_COUNT_START_NODE(responses[2]), SUB_NODE_COUNT_NUM_NODES(responses[2]));
		int widStart = SUB_NODE_COUNT_START_NODE(responses[2]);
		int widCount = SUB_NODE_COUNT_NUM_NODES(responses[2]);
		if (FUNC_GRP_TYPE_NODE_TYPE(responses[0]) == FUNC_GRP_AUDIO)
		{
			commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_AUDIO_FUNC_GRP_CAP);
			commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_SUPP_PCM_SIZE_RATE);
			commands[2] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_SUPP_STREAM_FORMATS);
			if (!run_verbs(hDevice, commands, responses, 3))
				return FALSE;
			printf("        AUDIO_FUNC_GRP_CAP: 0x%08X\n", responses[0]);
			printf("        SUPP_PCM_SIZE_RATE: 0x%08X\n", responses[1]);
			printf("        SUPP_STREAM_FORMATS: 0x%08X\n", responses[2]);

			commands[0] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_INPUT_AMP_CAP);
			commands[1] = MAKE_COMMAND(cAddr, nodeID, VERB_GET_PARAMETER, PARAM_OUTPUT_AMP_CAP);
			if (!run_verbs(hDevice, commands, responses, 2))
				return FALSE;
			for (int i = 0; i < 2; i++)
			{
				printf("        %s_AMP_CAP: 0x%08X\n", (i == 0) ? "INPUT" : "OUTPUT", responses[i]);
				printf("          (offset: %i, steps: %i, stepSize: %.2fdB, MuteCapable=%s)\n",
					AMP_CAP_OFFSET(responses[i]),
					AMP_CAP_NUM_STEPS(responses[i]),
					0.25f * (1 + AMP_CAP_STEP_SIZE(responses[i])),
					bool_str(responses[i] & AMP_CAP_MUTE_CAPABLE));
			}
		}
		puts("      Controls:");
		puts("      Widgets:");
		for (int widgetID = widStart; widgetID < widStart + widCount; widgetID++)
			if (!dump_widget(hDevice, cAddr, widgetID))
				return FALSE;
	}

	return TRUE;
}

static int dump_widgets(void)
{
	HANDLE hDevice = open_device();
	if (hDevice == INVALID_HANDLE_VALUE)
		return 1;
	uint16_t codecBits;
	BOOL success = DeviceIoControl(
		hDevice,
		HDA_VXD_GET_CODECS,
		NULL, 0,
		&codecBits, sizeof(codecBits),
		NULL,
		NULL);
	if (!success)
		goto error;
	if (codecBits == 0)
	{
		puts("No codecs found");
		close_device(hDevice);
		return 1;
	}
	for (int i = 0; i < 16; i++)
	{
		if (codecBits & (1 << i))
			if (!dump_codec(hDevice, i))
				goto error;
	}
	close_device(hDevice);
	return 0;
error:
	printf("Command failed: %s\n", get_errmsg());
	close_device(hDevice);
	return 1;
}

static int exec_verb(unsigned int codec_id, unsigned int node_id, uint32_t verb, uint32_t param)
{
	HANDLE hDevice = open_device();
	if (hDevice == INVALID_HANDLE_VALUE)
		return 1;
	uint32_t command = MAKE_COMMAND(codec_id, node_id, verb, param);
	uint32_t response = 0;
	BOOL success = DeviceIoControl(
		hDevice,
		HDA_VXD_EXEC_VERB,
		&command, sizeof(command),
		&response, sizeof(response),
		NULL,
		NULL);
	if (success)
		printf("%08X\n", response);
	else
		printf("Command failed: %s\n", get_errmsg());
	close_device(hDevice);
	return success ? 0 : 1;
}

static int dump_regs(void)
{
	HANDLE hDevice = open_device();
	if (hDevice == INVALID_HANDLE_VALUE)
		return 1;
	struct HDARegs regs;
	BOOL success = DeviceIoControl(
		hDevice,
		HDA_VXD_GET_BASE_REGS,
		NULL, 0,
		&regs, sizeof(regs),
		NULL,
		NULL);
	if (success)
	{
		printf("GCAP:       %04X\n"
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
			   regs.GCAP,
			   regs.VMIN, regs.VMAJ,
			   regs.OUTPAY, regs.INPAY,
			   regs.GCTL,
			   regs.WAKEEN,
			   regs.STATESTS,
			   regs.GSTS,
			   regs.OUTSTRMPAY, regs.INSTRMPAY,
			   regs.INTCTL, regs.INTSTS,
			   regs.WALCLK,
			   regs.SSYNC);
		printf("CORBLBASE:  %08X CORBUBASE: %08X\n"
		       "CORBWP:     %04X     CORBRP:    %04X\n"
		       "CORBCTL:    %02X       CORBSTS:   %02X       CORBSIZE: %02X\n",
			   regs.CORBLBASE, regs.CORBUBASE,
			   regs.CORBWP, regs.CORBRP,
			   regs.CORBCTL, regs.CORBSTS, regs.CORBSIZE);
		printf("RIRBLBASE:  %08X RIRBUBASE: %08X\n"
		       "RIRBWP:     %04X     RINTCNT:   %04X\n"
		       "RIRBCTL:    %02X       RIRBSTS:   %02X       RIRBSIZE: %02X\n",
			   regs.RIRBLBASE, regs.RIRBUBASE,
			   regs.RIRBWP, regs.RINTCNT,
			   regs.RIRBCTL, regs.RIRBSTS, regs.RIRBSIZE);
		printf("DPLBASE:    %08X DPUBASE:   %08X\n",
			   regs.DPLBASE, regs.DPUBASE);
		uint16_t gcap = regs.GCAP;
		int iss = GCAP_ISS(gcap);
		int oss = GCAP_OSS(gcap);
		int bss = GCAP_BSS(gcap);
		int nStreams = iss + oss + bss;
		for (int i = 0; i < nStreams; i++)
		{
			struct HDAStreamDesc sdesc;
			success = DeviceIoControl(
				hDevice,
				HDA_VXD_GET_STREAM_DESC(i),
				NULL, 0,
				&sdesc, sizeof(sdesc),
				NULL,
				NULL);
			if (success)
			{
				const char *type = "input";
				if (i >= iss)
					type = "output";
				if (i >= iss + oss)
					type = "bidirectional";
				printf("Stream %i (%s)\n", i, type);
				printf("SDCTL:  %02X%02X%02X   SDSTS:   %02X\n"
				       "SDLPIB: %08X SDCBL:   %08X\n"
				       "SDLVI:  %04X     SDFIFOS: %04X     SDFMT: %04X\n"
				       "SDBDPL: %08X SDBDPU:  %08X\n",
					   sdesc.SDCTLb2, sdesc.SDCTLb1, sdesc.SDCTLb0, sdesc.SDSTS,
					   sdesc.SDLPIB, sdesc.SDCBL,
					   sdesc.SDLVI, sdesc.SDFIFOS, sdesc.SDFMT,
					   sdesc.SDBDPL, sdesc.SDBDPU);
			}
			else
				goto fail;
		}
	}
	else
	{
fail:
		printf("Command failed: %s\n", get_errmsg());
	}
	close_device(hDevice);
	return success ? 0 : 1;
}

static int dump_pci_config(void)
{
	HANDLE hDevice = open_device();
	if (hDevice == INVALID_HANDLE_VALUE)
		return 1;
	BYTE pciConfig[256];
	BOOL success = DeviceIoControl(
		hDevice,
		HDA_VXD_GET_PCI_CONFIG,
		NULL, 0,
		pciConfig, sizeof(pciConfig),
		NULL,
		NULL);
	if (success)
	{
		puts("PCI Configuration space:");
		size_t offset = 0;
		for (offset = 0; offset < sizeof(pciConfig); offset++)
		{
			int column = offset % 16;
			if (column == 0)
				printf("%04X: ", offset);
			printf("%02X%c", pciConfig[offset], (column == 15) ? '\n' : ' ');
		}
	}
	else
		printf("Command failed: %s\n", get_errmsg());
	close_device(hDevice);
	return success ? 0 : 1;
}

static void list_verbs(void)
{
	const struct EnumItem *item;
	puts("available verbs:");
	for (item = verbs; item->name != NULL; item++)
		printf("  0x03X: %s\n", item->value, item->name);
}

static void list_get_parameter_params(void)
{
	const struct EnumItem *item;
	puts("available param values for GET_PARAMETER:");
	for (item = params; item->name != NULL; item++)
		printf("  0x02X: %s\n", item->value, item->name);
}

static BOOL parse_int(const char *paramName, const char *str, unsigned long int *value)
{
	char *endptr;
	*value = strtoul(str, &endptr, 0);
	if (*endptr == 0)
		return TRUE;
	printf("Error: %s must be an integer\n", paramName);
	return FALSE;
}

static BOOL parse_enum(const char *paramName, const char *str, unsigned long int *value, const struct EnumItem *list)
{
	const struct EnumItem *item;
	char *endptr;
	*value = strtoul(str, &endptr, 0);
	if (*endptr == 0)  // number
	{
		for (item = list; item->name != NULL; item++)
			if (*value == item->value)
				return TRUE;
	}
	else  // not a number
	{
		for (item = list; item->name != NULL; item++)
			if (strcmp(str, item->name) == 0)
			{
				*value = item->value;
				return TRUE;
			}
	}
	printf("Error: invalid value for %s\n", paramName);
	return FALSE;
}

int main(int argc, char **argv)
{
	const char *opt;

	if (argc < 2)
		goto bad_args;
	opt = argv[1];
	if (strcmp("-c", opt) == 0)
	{
		if (argc != 2)
			goto bad_args;
		return list_codecs();
	}
	else if (strcmp("-w", opt) == 0)
	{
		if (argc != 2)
			goto bad_args;
		return dump_widgets();
	}
	if (strcmp("-r", opt) == 0)
	{
		if (argc != 2)
			goto bad_args;
		return dump_regs();
	}
	else if (strcmp("-p", opt) == 0)
	{
		if (argc != 2)
			goto bad_args;
		return dump_pci_config();
	}
	else if (strcmp("-v", opt) == 0)
	{
		unsigned long int codec_id, node_id, verb, param;

		if (argc != 6)
			goto bad_args;
		if (!parse_int( "codec_id", argv[2], &codec_id)
		 || !parse_int( "node_id",  argv[3], &node_id))
			goto bad_args;
		if (!parse_enum("verb",     argv[4], &verb, verbs))
		{
			list_verbs();
			goto bad_args;
		}
		if (verb == VERB_GET_PARAMETER)
		{
			if (!parse_enum("param", argv[5], &param, params))
				goto bad_args;
		}
		else
		{
			if (!parse_int( "param", argv[5], &param))
				goto bad_args;
		}
		return exec_verb(codec_id, node_id, verb, param);
	}
	else if (strcmp("-lv", opt) == 0)
	{
		list_verbs();
		return 0;
	}
	else if (strcmp("-lp", opt) == 0)
	{
		list_get_parameter_params();
		return 0;
	}
	else if (strcmp("-h", opt) == 0)
	{
		usage(argv[0]);
		return 0;
	}
	else
		goto bad_args;

	return 0;

bad_args:
	usage(argv[0]);
	return 1;
}
