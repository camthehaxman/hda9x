# OpenWatcom makefile
# Please set the WATCOM environment variable to the directory where OpenWatcom
# is installed, and add the OpenWatcom bin directory (this will be binl or
# binnt, depending on your system) to your PATH environment variable, and run
# "wmake" to build

MODULE_NAME = hdaudio

DRV_BIN  = $(MODULE_NAME).drv
VXD_BIN  = $(MODULE_NAME).vxd
INF_FILE = $(MODULE_NAME).inf

DISK_FILES = $(DRV_BIN) $(VXD_BIN) $(INF_FILE)

default: install.img

clean: .symbolic
	rm *.obj *.drv *.vxd *.err *.map *.img fixlink

# Automatically delete target files if recipe commands fail
.ERASE

#-------------------------------------------------------------------------------
# Compiler options
#-------------------------------------------------------------------------------

INCLUDES = -I$(%WATCOM)/h -I$(%WATCOM)/h/win -Iddk -Iextern/tinyprintf
DEFINES  = -dDRV_VER_MAJOR=0 -dDRV_VER_MINOR=1 -dDEBUG=1

# Common compiler flags
# -q           quiet mode
# -zastd=c99   use C99 features
# -wx          maximum warning level
# -wcd=303     disable warning about unused functions/variables
# -wcd=202     disable warning about unused static functions
# -s           remove stack overflow checks
# -4           enable use of 486 instructions
# -bt=windows  target Windows operating system
# -d3          enable debugging info
# -fo=$@       output filename (expanded in recipe)
# $<           input filename (expanded in recipe)
CFLAGS = -q -zastd=c99 -wx -wcd=303 -wcd=202 -s -bt=windows -d3 -fo=$@ $< $(INCLUDES) $(DEFINES)

# Compile 32-bit C code (for VxD)
COMPILE32 = wcc386 $(CFLAGS)

# Assemble 32-bit assembly code (for VxD)
ASM32 = wasm -4 -mf -cx -fo=$@ $< $(DEFINES)

# Compile 16-bit C code (for userspace driver DLL)
COMPILE16 = wcc $(CFLAGS) -mc -zu -bd -zc

#-------------------------------------------------------------------------------
# 16-bit userspace driver DLL
#-------------------------------------------------------------------------------

DRV_OBJS = hda_drv16.obj tinyprintf16.obj

# Compile
hda_drv16.obj : hda_drv16.c .autodepend
	$(COMPILE16)
tinyprintf16.obj : extern/tinyprintf/tinyprintf.c .autodepend
	$(COMPILE16)

# Link
$(DRV_BIN) : $(DRV_OBJS)
	wlink op quiet, start=LibMain @<<$@.lnk
sys windows dll initglobal
file { $< }
library $(%WATCOM)/lib286/win/mmsystem
name $@
option map=$@.map
option modname=hdaudio
option description 'wave:High Definition Audio Driver'
export WEP.1
export DriverProc.2
export wodMessage.3
<<

#-------------------------------------------------------------------------------
# 32-bit kernel-mode VxD
#-------------------------------------------------------------------------------

VXD_OBJS = vxd_entry.obj hda_main.obj hda_debug.obj memory.obj tinyprintf32.obj

# Compile
vxd_entry.obj : vxd_entry.asm
	$(ASM32)
hda_main.obj : hda_main.c .autodepend
	$(COMPILE32)
hda_debug.obj : hda_debug.c .autodepend
	$(COMPILE32)
memory.obj : memory.c .autodepend
	$(COMPILE32)
tinyprintf32.obj : extern/tinyprintf/tinyprintf.c .autodepend
	$(COMPILE32)
# fixlink tool
fixlink: extern/fixlink/fixlink.c
	cc -Dstricmp=strcasecmp $< -o $@

# Link
$(VXD_BIN) : $(VXD_OBJS) fixlink
	wlink op quiet @<<$@.lnk
sys win_vxd dynamic
file { $(VXD_OBJS) }
library $(%WATCOM)/lib386/win/clib3r
name $@
option map=$@.map
export HDAUDIO_DDB.1
<<
	./fixlink -vxd32 $@

#-------------------------------------------------------------------------------
# Installation media
#-------------------------------------------------------------------------------

install.img : $(DISK_FILES)
	@echo Creating setup disk $@
	dd if=/dev/zero of=$@ bs=512 count=2880
	mformat -i $@
	mcopy -i $@ $< ::

install.iso : $(DISK_FILES)
	@echo Creating setup CD-ROM $@
	$(QUIET) mkisofs -o $@ $<
