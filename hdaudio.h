#pragma once

#include <stddef.h>
#include <stdint.h>
#include "static_assert.h"

//------------------------------------------------------------------------------
// Macros
//------------------------------------------------------------------------------

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
// Extracts numBits bits starting at startBit
#define GET_BITS(reg, startBit, numBits) (((reg) >> startBit) & ((1 << numBits) - 1))

//------------------------------------------------------------------------------
// HDA Commands
//------------------------------------------------------------------------------

#define MAKE_COMMAND(codecAddr, nodeID, verb, payload) ((((codecAddr) & 0xF) << 28) | (((nodeID) & 0xFF) << 20) | ((verb) << 8) | (payload))

// HDA command verbs
enum
{
	VERB_GET_CONVERTER_FORMAT = 0xA00,
	VERB_SET_CONVERTER_FORMAT = 0x200,

	VERB_GET_AMP_GAIN_MUTE   = 0xB00,
	VERB_SET_AMP_GAIN_MUTE   = 0x300,

	VERB_GET_PARAMETER     = 0xF00,

	VERB_GET_CONNECTION_SELECT_CTRL = 0xF01,
	VERB_SET_CONNECTION_SELECT_CTRL = 0x701,

	VERB_GET_CONNECTION_LIST_ENTRY   = 0xF02,

	VERB_GET_PROCESSING_STATE     = 0xF03,
	VERB_SET_PROCESSING_STATE     = 0x703,

	VERB_GET_IN_CONVERTER_SDI_SELECT = 0xF04,
	VERB_SET_IN_CONVERTER_SDI_SELECT = 0x704,

	VERB_GET_POWER_STATE    = 0xF05,
	VERB_SET_POWER_STATE    = 0x705,

	VERB_GET_CONVERTER_STREAM_CHANNEL = 0xF06,
	VERB_SET_CONVERTER_STREAM_CHANNEL = 0x706,

	VERB_GET_PIN_CONTROL    = 0xF07,
	VERB_SET_PIN_CONTROL    = 0x707,

	VERB_GET_UNSOLRESP     = 0xF08,
	VERB_SET_UNSOLRESP     = 0x708,

	VERB_GET_PIN_SENSE      = 0xF09,
	VERB_EXEC_PIN_SENSE     = 0x709,

	VERB_GET_EAPD_ENABLE    = 0xF0C,
	VERB_SET_EAPD_ENABLE    = 0x70C,

	VERB_GET_DIGICONVERT   = 0xF0D,
	VERB_SET_DIGICONVERT0  = 0x70D,  // sets bits 0-7
	VERB_SET_DIGICONVERT1  = 0x70E,  // sets bits 8-15
	VERB_SET_DIGICONVERT2  = 0x73E,  // sets bits 16-23
	VERB_SET_DIGICONVERT3  = 0x73F,  // sets bits 24-31

	VERB_GET_GPI_DATA      = 0xF10,
	VERB_SET_GPI_DATA      = 0x710,

	// not sure if this is F15/715 or F14/714. The spec is not clear on this.
	VERB_GET_GPIO_DATA     = 0xF15,
	VERB_SET_GPIO_DATA     = 0x715,

	VERB_GET_CONFIG_DEFAULT = 0xF1C,
	VERB_SET_CONFIG_DEFAULT0 = 0x71C,
	VERB_SET_CONFIG_DEFAULT1 = 0x71D,
	VERB_SET_CONFIG_DEFAULT2 = 0x71E,
	VERB_SET_CONFIG_DEFAULT3 = 0x71F,

	VERB_GET_CONV_CHAN_COUNT = 0xF2D,
	VERB_SET_CONV_CHAN_COUNT = 0x72D,

	VERB_GET_ASP_CHAN_MAP    = 0xF34,
	VERB_SET_ASP_CHAN_MAP    = 0x734,
};

// Parameters for VERB_GET_PARAMETER
enum
{
	PARAM_VENDOR_ID           = 0,
	PARAM_REVISION_ID         = 2,
	PARAM_SUB_NODE_COUNT      = 4,
	PARAM_FUNC_GRP_TYPE       = 5,  // see section 7.3.4.4 (applies to function groups)
	PARAM_AUDIO_FUNC_GRP_CAP  = 8,  // see section 7.3.4.5 (applies to audio function group)
	PARAM_AUDIO_WIDGET_CAP    = 9,
	PARAM_SUPP_PCM_SIZE_RATE  = 10,
	PARAM_SUPP_STREAM_FORMATS = 11,
	PARAM_PIN_CAP             = 12,
	PARAM_INPUT_AMP_CAP       = 13,
	PARAM_CONN_LIST_LENGTH    = 14,
	PARAM_SUPP_POWER_STATES   = 15,
	PARAM_PROCESSING_CAP      = 16,
	PARAM_GPIO_COUNT          = 17,
	PARAM_OUTPUT_AMP_CAP      = 18,
	PARAM_VOLUME_KNOB_CAP     = 19,
};

// Function group types
enum
{
	FUNC_GRP_AUDIO = 1,
	FUNC_GRP_MODEM = 2,
	// 0x80-0xFF are vendor defined, everything else is reserved
};

// Widget types
enum
{
	WIDGET_TYPE_AUDIO_OUTPUT   =  0,
	WIDGET_TYPE_AUDIO_INPUT    =  1,
	WIDGET_TYPE_AUDIO_MIXER    =  2,
	WIDGET_TYPE_AUDIO_SELECTOR =  3,
	WIDGET_TYPE_PIN_COMPLEX    =  4,
	WIDGET_TYPE_POWER          =  5,
	WIDGET_TYPE_VOLUME_KNOB    =  6,
	WIDGET_TYPE_BEEP_GENERATOR =  7,
	WIDGET_TYPE_VENDOR_DEFINED = 15,
};

// Fields for the PARAM_AUDIO_WIDGET_CAP parameter
#define WIDGET_CAP_INPUT_AMP          (1 << 1)
#define WIDGET_CAP_OUTPUT_AMP         (1 << 2)
#define WIDGET_CAP_AMP_PARAM_OVERRIDE (1 << 3)
#define WIDGET_CAP_FMT_OVERRIDE       (1 << 4)
#define WIDGET_CAP_STRIPE             (1 << 5)
#define WIDGET_CAP_PROCESSOR          (1 << 6)
#define WIDGET_CAP_UNSOLICITED        (1 << 7)
#define WIDGET_CAP_CONN_LIST          (1 << 8)
#define WIDGET_CAP_DIGITAL            (1 << 9)
#define WIDGET_CAP_POWER_CNTRL        (1 << 10)
#define WIDGET_CAP_LR_SWAP            (1 << 11)
#define WIDGET_CAP_CHAN_COUNT(cap)        (1 + (((cap) & 1) | (((cap) >> 12) & 0xF)))
#define WIDGET_CAP_TYPE(cap) (((cap) >> 20) & 0xF)

// Fields for the PARAM_SUPP_PCM_SIZE_RATE parameter
#define PCM_SUPP_8BIT  (1 << 16)
#define PCM_SUPP_16BIT (1 << 17)
#define PCM_SUPP_20BIT (1 << 18)
#define PCM_SUPP_24BIT (1 << 19)
#define PCM_SUPP_32BIT (1 << 20)
#define PCM_SUPP_8000HZ   (1 << 0)
#define PCM_SUPP_11025HZ  (1 << 1)
#define PCM_SUPP_16000HZ  (1 << 2)
#define PCM_SUPP_22050HZ  (1 << 3)
#define PCM_SUPP_32000HZ  (1 << 4)
#define PCM_SUPP_44100HZ  (1 << 5)
#define PCM_SUPP_48000HZ  (1 << 6)
#define PCM_SUPP_88200HZ  (1 << 7)
#define PCM_SUPP_96000HZ  (1 << 8)
#define PCM_SUPP_176400HZ (1 << 9)
#define PCM_SUPP_192000HZ (1 << 10)
#define PCM_SUPP_384000HZ (1 << 11)

// Fields for the PARAM_PIN_CAP parameter
#define PINCAP_PRESENCEDETECT (1 << 2)
#define PINCAP_OUTPUT         (1 << 4)
#define PINCAP_INPUT          (1 << 5)
#define PINCAP_HDMI           (1 << 7)
#define PINCAP_EAPD           (1 << 16)

// Fields for VERB_GET_PIN_CONTROL/VERB_SET_PIN_CONTROL
#define PIN_CONTROL_INPUT_ENABLE     (1 << 5)
#define PIN_CONTROL_OUTPUT_ENABLE    (1 << 6)
#define PIN_CONTROL_HEADPHONE_ENABLE (1 << 7)

//------------------------------------------------------------------------------
// HDA Registers
//------------------------------------------------------------------------------

// Stream descriptor registers
struct HDAStreamDesc
{
	volatile uint8_t  SDCTLb0;     // Control
// byte 0
#define SDCTLb0_SRST (1 << 0)  // stream reset
#define SDCTLb0_RUN  (1 << 1)  // stream run
#define SDCTLb0_IOCE (1 << 2)  // interrupt on completion enable
#define SDCTLb0_FEIE (1 << 3)  // FIFO error interrupt enable
#define SDCTLb0_DEIE (1 << 4)  // descriptor error interrupt enable
	volatile uint8_t  SDCTLb1;     // reserved
// byte 1 (reserved)
	volatile uint8_t  SDCTLb2;
// byte 2
#define SDCTLb2_STRIPE_MASK 3  // stripe control
#define SDCTLb2_TP   (1 << 2)  // traffic priority
#define SDCTLb2_DIR  (1 << 3)  // bidirectional direction control
#define SDCTLb2_STRM(n) (((n) & 0xF) << 4) // stream number
#define SDCTLb2_STRM_MASK 0xF0

	volatile uint8_t  SDSTS;       // Status
// SDSTS fields
#define SDSTS_FIFORDY (1 << 5)  // FIFO ready
#define SDSTS_DESE    (1 << 4)  // descriptor error
#define SDSTS_FIFOE   (1 << 3)  // FIFO error
#define SDSTS_BCIS    (1 << 2)  // buffer completion interrupt status

	volatile uint32_t SDLPIB;      // Link Position in Current Buffer
	volatile uint32_t SDCBL;       // Cyclic Buffer Length
	volatile uint16_t SDLVI;       // Last Valid Index
	uint8_t reserved0E[0x10-0xE];  // Reserved
	volatile uint16_t SDFIFOS;     // FIFO Data

	volatile uint16_t SDFMT;       // Format
// SDFMT fields
#define SDFMT_BASE_48KHZ (0 << 14)
#define SDFMT_BASE_44KHZ (1 << 14)
#define SDFMT_MULT_X1    (0 << 11)
#define SDFMT_MULT_X2    (1 << 11)
#define SDFMT_MULT_X3    (2 << 11)
#define SDFMT_MULT_X4    (3 << 11)
#define SDFMT_DIV_1      (0 << 8)
#define SDFMT_DIV_2      (1 << 8)
#define SDFMT_DIV_3      (2 << 8)
#define SDFMT_DIV_4      (3 << 8)
#define SDFMT_DIV_5      (4 << 8)
#define SDFMT_DIV_6      (5 << 8)
#define SDFMT_DIV_7      (6 << 8)
#define SDFMT_DIV_8      (7 << 8)
#define SDFMT_BITS_8     (0 << 4)
#define SDFMT_BITS_16    (1 << 4)
#define SDFMT_BITS_20    (2 << 4)
#define SDFMT_BITS_24    (3 << 4)
#define SDFMT_BITS_32    (4 << 4)
#define SDFMT_CHAN(n)    (((n)-1) & 0xF)
#define SDFMT_CHAN_MASK  0xF

	uint8_t reserved14[0x1B-0x18];
	volatile uint32_t SDBDPL;      // Buffer Descriptor List Pointer - Lower
	volatile uint32_t SDBDPU;      // Buffer Descriptor List Pointer - Upper
};

static_assert(sizeof(struct HDAStreamDesc) == 0x20, HDAStreamDesc_size);

#define CHECK_REG(start, end, reg) \
	static_assert((start) == offsetof(struct HDAStreamDesc, reg), check_offset_##reg); \
	static_assert(1 + (end) - (start) == sizeof(((struct HDAStreamDesc *)0)->reg), check_size_##reg);
CHECK_REG(0x00, 0x00, SDCTLb0)
CHECK_REG(0x01, 0x01, SDCTLb1)
CHECK_REG(0x02, 0x02, SDCTLb2)
CHECK_REG(0x03, 0x03, SDSTS)
CHECK_REG(0x04, 0x07, SDLPIB)
CHECK_REG(0x08, 0x0B, SDCBL)
CHECK_REG(0x0C, 0x0D, SDLVI)
CHECK_REG(0x10, 0x11, SDFIFOS)
CHECK_REG(0x12, 0x13, SDFMT)
CHECK_REG(0x18, 0x1B, SDBDPL)
CHECK_REG(0x1C, 0x1F, SDBDPU)
#undef CHECK_REG

// General registers
struct HDARegs
{
	volatile uint16_t GCAP;         // Global Capabilities
// GCAP fields
#define GCAP_OSS(reg)  GET_BITS(reg, 12, 4)  // number of output streams supported
#define GCAP_ISS(reg)  GET_BITS(reg,  8, 4)  // number of input streams supported
#define GCAP_BSS(reg)  GET_BITS(reg,  3, 5)  // number of bidirectional streams supported
#define GCAP_NSDO(reg) GET_BITS(reg,  1, 2)  // number of serial data out signals
#define GCAP_64OK      (1 << 0)               // 64-bit addressing supported

	volatile uint8_t  VMIN;         // Minor Version
	volatile uint8_t  VMAJ;         // Major Version
	volatile uint16_t OUTPAY;       // Output Payload Capability
	volatile uint16_t INPAY;        // Input Payload Capability

	volatile uint32_t GCTL;         // Global Control
// GCTL fields
#define GCTL_UNSOL  (1 << 8)
#define GCTL_FCNTRL (1 << 1)
#define GCTL_CRST   (1 << 0)

	volatile uint16_t WAKEEN;       // Wake Enable
// WAKEEN fields
#define WAKEEN_SDIWEN_MASK 0x7FFF  // SDIN wake enable flags

	volatile uint16_t STATESTS;     // Wake Status
// STATESTS fields
#define STATESTS_SDIWAKE_MASK 0x7FFF

	volatile uint16_t GSTS;         // Global Status
// GSTS fields
#define GSTS_FSTS (1 << 1)  // flush status

	uint8_t reserved12[0x18-0x12];  // Reserved
	volatile uint16_t OUTSTRMPAY;   // Output Stream Payload Capability
	volatile uint16_t INSTRMPAY;    // Input Stream Payload Capability
	uint8_t reserved1C[0x20-0x1C];  // Reserved

	volatile uint32_t INTCTL;       // Interrupt Control
// INTCTL fields
#define INTCTL_GIE (1 << 31)  // global interrupt enable
#define INTCTL_CIE (1 << 30)  // controller interrupt enable
#define INTCTL_SIE_MASK 0x3FFFFFFF  // stream interrupt enable

	volatile uint32_t INTSTS;       // Interrupt Status
// INTSTS fields
#define INTSTS_GIS (1 << 31)  // global interrupt status
#define INTSTS_CIS (1 << 30)  // controller interrupt status
#define INTSTS_SIS_MASK 0x3FFFFFFF  // stream interrupt status

	uint8_t reserved28[0x30-0x28];  // Reserved
	volatile uint32_t WALCLK;       // Wall Clock Counter
	uint8_t reserved34[0x38-0x34];  // Reserved
	volatile uint32_t SSYNC;        // Stream Synchronization
	uint8_t reserved3C[0x40-0x3C];  // Reserved
	volatile uint32_t CORBLBASE;    // CORB Lower Base Address
	volatile uint32_t CORBUBASE;    // CORB Upper Base Address

	volatile uint16_t CORBWP;       // CORB Write Pointer
// CORBWP fields
#define CORBWP_CORBWP_MASK 0xFF  // CORB write pointer

	volatile uint16_t CORBRP;       // CORB Read Pointer
// CORBRP fields
#define CORBRP_CORBRPRST (1 << 15)  // CORB read pointer reset
#define CORBRP_CORBRP_MASK 0xFF     // CORB read pointer

	volatile uint8_t  CORBCTL;      // CORB Control
// CORBCTL fields
#define CORBCTL_CORBRUN (1 << 1)  // enable CORB DMA engine
#define CORBCTL_CMEIE   (1 << 0)  // CORB memory error interrupt enable

	volatile uint8_t  CORBSTS;      // CORB Status

	volatile uint8_t  CORBSIZE;     // CORB Size
// CORBSIZE fields
#define CORBSIZE_CORBSZCAP_2ENT   (1 << 4)  // CORB supports 2 entries
#define CORBSIZE_CORBSZCAP_16ENT  (1 << 5)  // CORB supports 16 entries
#define CORBSIZE_CORBSZCAP_256ENT (1 << 6)  // CORB supports 256 entries
#define CORBSIZE_CORBSIZE_MASK 3
#define CORBSIZE_CORBSIZE_2ENT   0
#define CORBSIZE_CORBSIZE_16ENT  1
#define CORBSIZE_CORBSIZE_256ENT 2

	uint8_t reserved4F[0x50-0x4F];  // Reserved
	volatile uint32_t RIRBLBASE;    // RIRB Lower Base Address
	volatile uint32_t RIRBUBASE;    // RIRB Upper Base Address

	volatile uint16_t RIRBWP;       // RIRB Write Pointer
// RIRBWP fields
#define RIRBWP_RIRBWPRST (1 << 15)  // RIRB write pointer reset
#define RIRBWP_RIRBWP_MASK 0xFF     // RIRB write pointer

	volatile uint16_t RINTCNT;      // Response Interrupt Count
// RINTCNT fields
#define RINTCNT_RINTCNT_MASK 0xFF  // N response interrupt count

	volatile uint8_t  RIRBCTL;      // RIRB Control
// RIRBCTL fields
#define RIRBCTL_RIRBOIC   (1 << 2)  // response overrun interrupt control
#define RIRBCTL_RIRBDMAEN (1 << 1)  // enable RIRB DMA engine
#define RIRBCTL_RINTCTL   (1 << 0)  // response interrupt control

	volatile uint8_t  RIRBSTS;      // RIRB Status

	volatile uint8_t  RIRBSIZE;     // RIRB Size
// RIRBSIZE fields
#define RIRBSIZE_RIRBSZCAP_2ENT   (1 << 4)  // RIRB supports 2 entries
#define RIRBSIZE_RIRBSZCAP_16ENT  (1 << 5)  // RIRB supports 16 entries
#define RIRBSIZE_RIRBSZCAP_256ENT (1 << 6)  // RIRB supports 256 entries
#define RIRBSIZE_RIRBSIZE_MASK 3
#define RIRBSIZE_RIRBSIZE_2ENT   0
#define RIRBSIZE_RIRBSIZE_16ENT  1
#define RIRBSIZE_RIRBSIZE_256ENT 2

	uint8_t reserved5F[0x60-0x5F];  // Reserved
	volatile uint32_t ICOI;         // Immediate Command Output Interface
	volatile uint32_t ICII;         // Immediate Command Input Interface

	volatile uint16_t ICIS;         // Immediate Command Status
// ICIS fields
#define ICIS_ICB         (1 << 0)
#define ICIS_IRV         (1 << 1)
#define ICIS_ICVER       (1 << 2)
#define ICIS_IRRUNSOL    (1 << 3)
#define ICIS_IRRADD(reg) GET_BITS(reg, 4, 4)

	uint8_t reserved6A[0x6F-0x6A];  // Reserved
	volatile uint32_t DPLBASE;      // DMA Position Buffer Lower Base
	volatile uint32_t DPUBASE;      // DMA Position Buffer Upper Base
	uint8_t reserved78[0x80-0x78];
	struct HDAStreamDesc SDESC[];
};

#define CHECK_REG(start, end, reg) \
	static_assert((start) == offsetof(struct HDARegs, reg), check_offset_##reg); \
	static_assert(1 + (end) - (start) == sizeof(((struct HDARegs *)0)->reg), check_size_##reg);
CHECK_REG(0x00, 0x01, GCAP)
CHECK_REG(0x02, 0x02, VMIN)
CHECK_REG(0x03, 0x03, VMAJ)
CHECK_REG(0x04, 0x05, OUTPAY)
CHECK_REG(0x06, 0x07, INPAY)
CHECK_REG(0x08, 0x0B, GCTL)
CHECK_REG(0x0C, 0x0D, WAKEEN)
CHECK_REG(0x0E, 0x0F, STATESTS)
CHECK_REG(0x10, 0x11, GSTS)
CHECK_REG(0x18, 0x19, OUTSTRMPAY)
CHECK_REG(0x1A, 0x1B, INSTRMPAY)
CHECK_REG(0x20, 0x23, INTCTL)
CHECK_REG(0x24, 0x27, INTSTS)
CHECK_REG(0x30, 0x33, WALCLK)
CHECK_REG(0x38, 0x3B, SSYNC)
CHECK_REG(0x40, 0x43, CORBLBASE)
CHECK_REG(0x44, 0x47, CORBUBASE)
CHECK_REG(0x48, 0x49, CORBWP)
CHECK_REG(0x4A, 0x4B, CORBRP)
CHECK_REG(0x4C, 0x4C, CORBCTL)
CHECK_REG(0x4D, 0x4D, CORBSTS)
CHECK_REG(0x4E, 0x4E, CORBSIZE)
CHECK_REG(0x50, 0x53, RIRBLBASE)
CHECK_REG(0x54, 0x57, RIRBUBASE)
CHECK_REG(0x58, 0x59, RIRBWP)
CHECK_REG(0x5A, 0x5B, RINTCNT)
CHECK_REG(0x5C, 0x5C, RIRBCTL)
CHECK_REG(0x5D, 0x5D, RIRBSTS)
CHECK_REG(0x5E, 0x5E, RIRBSIZE)
CHECK_REG(0x60, 0x63, ICOI)
CHECK_REG(0x64, 0x67, ICII)
CHECK_REG(0x68, 0x69, ICIS)
CHECK_REG(0x70, 0x73, DPLBASE)
CHECK_REG(0x74, 0x77, DPUBASE)
#undef CHECK_REG

extern struct HDARegs *hdaRegs;

typedef uint16_t nodeid_t;

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
	// specific to Audio Output / Audio Input
	uint8_t streamTag;
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

BOOL hda_run_commands(const uint32_t *commands, uint32_t *responses, unsigned int count);
BOOL hda_run_command(uint32_t command, uint32_t *response);

//------------------------------------------------------------------------------
// Debug
//------------------------------------------------------------------------------

#include "tinyprintf.h"

#if DEBUG
#define dprintf printf
#else
#define dprintf(...)
#endif

#if DEBUG
#define ASSERT(expr) ((expr)?(void)0:hda_debug_assert_fail(#expr,__FILE__,__LINE__))
#else
#define ASSERT(expr) ((void)(expr))
#endif

void hda_debug_init(void);
void hda_debug_dump_regs(void);
void hda_debug_dump_codec(struct HDACodec *codec);
void hda_debug_assert_fail(const char *expr, const char *file, int line);
