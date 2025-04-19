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

static const char *get_error(void)
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
		printf("Could not open HD audio device: %s\n", get_error());
	return h;
}

static void close_device(HANDLE h)
{
	CloseHandle(h);
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
		printf("Command failed: %s\n", get_error());
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
		printf("Command failed: %s\n", get_error());
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
		printf("Command failed: %s\n", get_error());
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
	HANDLE hDevice;

	if (argc < 2)
		goto bad_args;
	opt = argv[1];
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
