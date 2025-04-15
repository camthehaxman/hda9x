// Debugging routines for the VxD

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

// Initializes printf support
void hda_debug_init(void)
{
	if (!initialized)
	{
		init_printf(NULL, putc);
		initialized = TRUE;
	}
}

void hda_debug_assert_fail(const char *expr, const char *file, int line)
{
	dprintf("*** ASSERTION FAILED ***\n");
	printf("file %s, line %i: %s\n", file, line, expr);
	while (1)
		;
}
