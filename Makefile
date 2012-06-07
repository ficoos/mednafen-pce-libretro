DEBUG = 0

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

ifeq ($(platform), unix)
   TARGET := libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,-version-script=link.T -Wl,-no-undefined
   ENDIANNESS_DEFINES = -DLSB_FIRST -DARCH_X86 -msse -msse2
else ifeq ($(platform), osx)
   TARGET := libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES = -DLSB_FIRST-DARCH_X86 -msse -msse2
else ifeq ($(platform), ps3)
   TARGET := libretro.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES = -DBLARGG_BIG_ENDIAN=1 -DWORDS_BIGENDIAN
   PLATFORM_DEFINES := -D__CELLOS_LV2__ -D__POWERPC__ -D__ppc__ -DUSE_CACHE_PREFETCH -DBRANCHLESS_GBA_GFX
else ifeq ($(platform), sncps3)
   TARGET := libretro.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES = -DBLARGG_BIG_ENDIAN=1 -DWORDS_BIGENDIAN
   PLATFORM_DEFINES := -D__CELLOS_LV2__ -D__POWERPC__ -D__ppc__ -DUSE_CACHE_PREFETCH -DBRANCHLESS_GBA_GFX
else ifeq ($(platform), xenon)
   TARGET := libretro.a
   CC = xenon-gcc
   CXX = xenon-g++
   AR = xenon-ar
   ENDIANNESS_DEFINES = -DBLARGG_BIG_ENDIAN=1 -DWORDS_BIGENDIAN
   PLATFORM_DEFINES := -D__LIBXENON__ -D__POWERPC__ -D__ppc__
else ifeq ($(platform), wii)
   TARGET := libretro.a
   CC = powerpc-eabi-gcc
   CXX = powerpc-eabi-g++
   AR = powerpc-eabi-ar
   ENDIANNESS_DEFINES = -DBLARGG_BIG_ENDIAN=1 -DWORDS_BIGENDIAN
   PLATFORM_DEFINES += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -D__ppc__
else
   TARGET := retro.dll
   CC = gcc
   CXX = g++
   SHARED := -shared -static-libgcc -static-libstdc++ -Wl,-no-undefined -Wl,-version-script=link.T
   ENDIANNESS_DEFINES = -DLSB_FIRST -DARCH_X86 -msse -msse2
endif

ifeq ($(DEBUG), 1)
	CFLAGS += -O0 -g
	CXXFLAGS += -O0 -g
else
	CFLAGS += -O3
	CXXFLAGS += -O3
endif

MEDNAFEN_DIR := mednafen
PCE_DIR := $(MEDNAFEN_DIR)/pce

HW_CPU_SOURCES := $(MEDNAFEN_DIR)/hw_cpu/huc6280/huc6280.cpp \
	$(MEDNAFEN_DIR)/hw_cpu/c68k/c68k.c \
	$(MEDNAFEN_DIR)/hw_cpu/c68k/c68kexec.c

HW_MISC_SOURCES := $(MEDNAFEN_DIR)/hw_misc/arcade_card/arcade_card.cpp

HW_SOUND_SOURCES := $(MEDNAFEN_DIR)/hw_sound/pce_psg/pce_psg.cpp

HW_VIDEO_SOURCES := $(MEDNAFEN_DIR)/hw_video/huc6270/vdc.cpp

PCE_SOURCES := $(PCE_DIR)/vce.cpp \
	$(PCE_DIR)/pce.cpp \
	$(PCE_DIR)/input.cpp \
	$(PCE_DIR)/huc.cpp \
	$(PCE_DIR)/hes.cpp \
	$(PCE_DIR)/tsushin.cpp \
	$(PCE_DIR)/subhw.cpp \
	$(PCE_DIR)/mcgenjin.cpp \
	$(PCE_DIR)/input/gamepad.cpp \
	$(PCE_DIR)/input/tsushinkb.cpp \
	$(PCE_DIR)/input/mouse.cpp

MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/cdrom/cdromif.cpp \
	$(MEDNAFEN_DIR)/mednafen.cpp \
	$(MEDNAFEN_DIR)/error.cpp \
	$(MEDNAFEN_DIR)/math_ops.cpp \
	$(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/player.cpp \
	$(MEDNAFEN_DIR)/cdplay.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/tests.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDAccess_Image.cpp \
	$(MEDNAFEN_DIR)/cdrom/CDUtility.cpp \
	$(MEDNAFEN_DIR)/cdrom/lec.cpp \
	$(MEDNAFEN_DIR)/cdrom/SimpleFIFO.cpp \
	$(MEDNAFEN_DIR)/cdrom/audioreader.cpp \
	$(MEDNAFEN_DIR)/cdrom/galois.cpp \
	$(MEDNAFEN_DIR)/cdrom/pcecd.cpp \
	$(MEDNAFEN_DIR)/cdrom/scsicd.cpp \
	$(MEDNAFEN_DIR)/cdrom/recover-raw.cpp \
	$(MEDNAFEN_DIR)/cdrom/l-ec.cpp \
	$(MEDNAFEN_DIR)/cdrom/crc32.cpp \
	$(MEDNAFEN_DIR)/memory.cpp \
	$(MEDNAFEN_DIR)/mempatcher.cpp \
	$(MEDNAFEN_DIR)/video/video.cpp \
	$(MEDNAFEN_DIR)/video/text.cpp \
	$(MEDNAFEN_DIR)/video/font-data.cpp \
	$(MEDNAFEN_DIR)/video/Deinterlacer.cpp \
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/video/resize.cpp \
	$(MEDNAFEN_DIR)/string/escape.cpp \
	$(MEDNAFEN_DIR)/string/ConvertUTF.cpp \
	$(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp \
	$(MEDNAFEN_DIR)/sound/Stereo_Buffer.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/okiadpcm.cpp \
	$(MEDNAFEN_DIR)/md5.cpp

MPC_SRC := $(wildcard $(MEDNAFEN_DIR)/mpcdec/*.c)
TREMOR_SRC := $(wildcard $(MEDNAFEN_DIR)/tremor/*.c)

SOURCES_C := $(MEDNAFEN_DIR)/trio/trio.c \
	$(MPC_SRC) \
	$(TREMOR_SRC) \
	$(MEDNAFEN_DIR)/trio/trionan.c \
	$(MEDNAFEN_DIR)/trio/triostr.c \
	$(MEDNAFEN_DIR)/string/world_strtod.c \
	$(MEDNAFEN_DIR)/compress/blz.c \
	$(MEDNAFEN_DIR)/compress/unzip.c \
	$(MEDNAFEN_DIR)/compress/minilzo.c \
	$(MEDNAFEN_DIR)/compress/quicklz.c \
	$(MEDNAFEN_DIR)/compress/ioapi.c \
	$(MEDNAFEN_DIR)/resampler/resample.c

LIBRETRO_SOURCES := libretro.cpp stubs.cpp

SOURCES := $(LIBRETRO_SOURCES) $(HW_CPU_SOURCES) $(HW_MISC_SOURCES) $(HW_SOUND_SOURCES) $(HW_VIDEO_SOURCES) $(PCE_SOURCES) $(MEDNAFEN_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)


LDFLAGS += -Wl,--no-undefined -lz -Wl,--version-script=link.T -pthread
FLAGS += -ffast-math -funroll-loops
FLAGS += -I. -Imednafen -Imednafen/include -Imednafen/intl -Imednafen/hw_cpu -Imednafen/hw_misc -Imednafen/hw_sound -Imednafen/hw_video $(ENDIANNESS_DEFINES) -pthread

WARNINGS := -Wall \
	-Wno-narrowing \
	-Wno-unused-but-set-variable \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-function \
	-Wno-uninitialized \
	-Wno-unused-result \
	-Wno-strict-aliasing \
	-Wno-overflow

FLAGS += -DHAVE_MKDIR -DSIZEOF_DOUBLE=8 $(WARNINGS) \
			-DMEDNAFEN_VERSION=\"0.9.22\" -DMEDNAFEN_VERSION_NUMERIC=922 -DPSS_STYLE=1 -DMPC_FIXED_POINT -DWANT_PCE_EMU -DSTDC_HEADERS

CXXFLAGS += $(FLAGS)
CFLAGS += $(FLAGS) -std=gnu99

$(TARGET): $(OBJECTS)
ifeq ($(platform), ps3)
	$(AR) rcs $@ $(OBJS)
else ifeq ($(platform), sncps3)
	$(AR) rcs $@ $(OBJS)
else ifeq ($(platform), xenon)
	$(AR) rcs $@ $(OBJS)
else ifeq ($(platform), wii)
	$(AR) rcs $@ $(OBJS)
else
	$(CXX) -o $@ $^ $(SHARED) $(FLAGS) $(LDFLAGS)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
