#
# arch/x86/kernel/acpi/realmode/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

always		:= wakeup.bin
targets		:= wakeup.elf wakeup.lds

wakeup-y	+= wakeup.o wakemain.o video-mode.o copy.o bioscall.o regs.o

# The link order of the video-*.o modules can matter.  In particular,
# video-vga.o *must* be listed first, followed by video-vesa.o.
# Hardware-specific drivers should follow in the order they should be
# probed, and video-bios.o should typically be last.
wakeup-y	+= video-vga.o
wakeup-y	+= video-vesa.o
wakeup-y	+= video-bios.o

targets		+= $(wakeup-y)

bootsrc		:= $(src)/../../../boot

# ---------------------------------------------------------------------------

# How to compile the 16-bit code.  Note we always compile for -march=i386,
# that way we can complain to the user if the CPU is insufficient.
# Compile with _SETUP since this is similar to the boot-time setup code.
KBUILD_CFLAGS	:= $(REALMODE_CFLAGS) -D_SETUP -D_WAKEUP \
		   -I$(srctree)/$(bootsrc)
KBUILD_AFLAGS	:= $(KBUILD_CFLAGS) -D__ASSEMBLY__
GCOV_PROFILE := n

WAKEUP_OBJS = $(addprefix $(obj)/,$(wakeup-y))

LDFLAGS_wakeup.elf	:= -T

CPPFLAGS_wakeup.lds += -P -C

$(obj)/wakeup.elf: $(obj)/wakeup.lds $(WAKEUP_OBJS) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_wakeup.bin	:= -O binary

$(obj)/wakeup.bin: $(obj)/wakeup.elf FORCE
	$(call if_changed,objcopy)
