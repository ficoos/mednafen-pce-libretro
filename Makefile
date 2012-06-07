DEBUG=0

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

CC         = gcc
LRETRO_PORT_SRC    = mednafen

ifeq ($(platform), unix)
   TARGET := libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,-no-undefined
   ENDIANNESS_DEFINES := -DLSB_FIRST
else ifeq ($(platform), osx)
   TARGET := libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES := -DLSB_FIRST
else ifeq ($(platform), ps3)
   TARGET := libretro.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES := -DWORDS_BIGENDIAN=1
else ifeq ($(platform), sncps3)
   TARGET := libretro.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES := -DWORDS_BIGENDIAN=1
else ifeq ($(platform), xenon)
   TARGET := libretro.a
   CC = xenon-gcc
   CXX = xenon-g++
   AR = xenon-ar
   CFLAGS += -D__LIBXENON__ -m32
   CXXFLAGS += -D__LIBXENON__ -m32
   ENDIANNESS_DEFINES := -D__ppc__ -DWORDS_BIGENDIAN=1
else ifeq ($(platform), wii)
   TARGET := libretro.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar
   CFLAGS += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
   CXXFLAGS += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
   ENDIANNESS_DEFINES := -DWORDS_BIGENDIAN
else
   TARGET := retro.dll
   CC = gcc
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=link.T
   CFLAGS += -D__WIN32__ -D__WIN32_LIBRETRO__ -Wno-missing-field-initializers
endif

ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g
else
CFLAGS += -O3
endif

PORTOBJECTS = ./libretro.o \
              ./stubs.o 

OBJECTS    = ./$(LRETRO_PORT_SRC)/hw_cpu/huc6280/huc6280.o \
             ./$(LRETRO_PORT_SRC)/hw_cpu/c68k/c68k.o \
             ./$(LRETRO_PORT_SRC)/hw_cpu/c68k/c68kexec.o \
             ./$(LRETRO_PORT_SRC)/hw_misc/arcade_card/arcade_card.o \
             ./$(LRETRO_PORT_SRC)/hw_sound/pce_psg/pce_psg.o \
             ./$(LRETRO_PORT_SRC)/hw_video/huc6270/vdc.o \
	     ./$(LRETRO_PORT_SRC)/pce/vce.o \
	     ./$(LRETRO_PORT_SRC)/pce/pce.o \
	     ./$(LRETRO_PORT_SRC)/pce/input.o \
	     ./$(LRETRO_PORT_SRC)/pce/huc.o \
	     ./$(LRETRO_PORT_SRC)/pce/hes.o \
	     ./$(LRETRO_PORT_SRC)/pce/tsushin.o \
	     ./$(LRETRO_PORT_SRC)/pce/subhw.o \
	     ./$(LRETRO_PORT_SRC)/pce/mcgenjin.o \
	     ./$(LRETRO_PORT_SRC)/pce/input/gamepad.o \
	     ./$(LRETRO_PORT_SRC)/pce/input/tsushinkb.o \
	     ./$(LRETRO_PORT_SRC)/pce/input/mouse.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/cdromif.o \
	     ./$(LRETRO_PORT_SRC)/mednafen.o \
	     ./$(LRETRO_PORT_SRC)/error.o \
	     ./$(LRETRO_PORT_SRC)/math_ops.o \
	     ./$(LRETRO_PORT_SRC)/settings.o \
	     ./$(LRETRO_PORT_SRC)/general.o \
	     ./$(LRETRO_PORT_SRC)/player.o \
	     ./$(LRETRO_PORT_SRC)/cdplay.o \
	     ./$(LRETRO_PORT_SRC)/FileWrapper.o \
	     ./$(LRETRO_PORT_SRC)/state.o \
	     ./$(LRETRO_PORT_SRC)/tests.o \
	     ./$(LRETRO_PORT_SRC)/endian.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/CDAccess.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/CDAccess_Image.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/CDUtility.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/lec.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/SimpleFIFO.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/audioreader.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/galois.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/pcecd.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/scsicd.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/recover-raw.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/l-ec.o \
	     ./$(LRETRO_PORT_SRC)/cdrom/crc32.o \
	     ./$(LRETRO_PORT_SRC)/memory.o \
	     ./$(LRETRO_PORT_SRC)/mempatcher.o \
	     ./$(LRETRO_PORT_SRC)/video/video.o \
	     ./$(LRETRO_PORT_SRC)/video/text.o \
	     ./$(LRETRO_PORT_SRC)/video/font-data.o \
	     ./$(LRETRO_PORT_SRC)/video/Deinterlacer.o \
	     ./$(LRETRO_PORT_SRC)/video/surface.o \
	     ./$(LRETRO_PORT_SRC)/video/resize.o \
	     ./$(LRETRO_PORT_SRC)/string/escape.o \
	     ./$(LRETRO_PORT_SRC)/string/ConvertUTF.o \
	     ./$(LRETRO_PORT_SRC)/sound/Blip_Buffer.o \
	     ./$(LRETRO_PORT_SRC)/sound/Fir_Resampler.o \
	     ./$(LRETRO_PORT_SRC)/sound/Stereo_Buffer.o \
	     ./$(LRETRO_PORT_SRC)/file.o \
	     ./$(LRETRO_PORT_SRC)/okiadpcm.o \
	     ./$(LRETRO_PORT_SRC)/md5.o \
	     ./$(LRETRO_PORT_SRC)/trio/trio.o \
	     ./$(LRETRO_PORT_SRC)/trio/trionan.o \
	     ./$(LRETRO_PORT_SRC)/trio/triostr.o \
	     ./$(LRETRO_PORT_SRC)/string/world_strtod.o \
	     ./$(LRETRO_PORT_SRC)/compress/blz.o \
	     ./$(LRETRO_PORT_SRC)/compress/unzip.o \
	     ./$(LRETRO_PORT_SRC)/compress/minilzo.o \
	     ./$(LRETRO_PORT_SRC)/compress/quicklz.o \
	     ./$(LRETRO_PORT_SRC)/compress/ioapi.o \
	     ./$(LRETRO_PORT_SRC)/resampler/resample.o
    

INCLUDES   = -I. -Imednafen -Imednafen/include -Imednafen/intl -Imednafen/hw_cpu -Imednafen/hw_misc -Imednafen/hw_sound -Imednafen/hw_video
DEFINES    = -DHAVE_MKDIR -DSIZEOF_DOUBLE=8 $(WARNINGS) -DMEDNAFEN_VERSION=\"0.9.22\" -DMEDNAFEN_VERSION_NUMERIC=922 -DPSS_STYLE=1 -DMPC_FIXED_POINT -DWANT_PCE_EMU -DSTDC_HEADERS

ifeq ($(platform), sncps3)
WARNINGS_DEFINES =
CODE_DEFINES =
else
WARNINGS_DEFINES = -Wall -W -Wno-unused-parameter
CODE_DEFINES = -fomit-frame-pointer
endif

COMMON_DEFINES += $(CODE_DEFINES) $(WARNINGS_DEFINES) -DNDEBUG=1 $(fpic)

CFLAGS     += $(DEFINES) $(COMMON_DEFINES) -std=gnu99 $(ENDIANNESS_DEFINES)
CXXFLAGS   += $(DEFINES) $(COMMON_DEFINES) $(ENDIANNESS_DEFINES)

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(platform), ps3)
	$(AR) rcs $@ $(OBJECTS)
else ifeq ($(platform), sncps3)
	$(AR) rcs $@ $(OBJECTS)
else ifeq ($(platform), xenon)
	$(AR) rcs $@ $(OBJECTS)
else ifeq ($(platform), wii)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CC) $(fpic) $(SHARED) $(INCLUDES) -o $@ $(OBJECTS) -lm
endif

%.o: %.c
	$(CC) $(INCLUDES) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean

