DEBUG=0
#set later according to platform
IS_X86 = 0
FAST = 1

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
   SHARED := -shared -Wl,--version-script=mednafen/libretro/link.T -Wl,-no-undefined
   ENDIANNESS_DEFINES := -DLSB_FIRST
   ifneq ($(shell uname -p | grep -E '((i.|x)86|amd64)'),)
      IS_X86 = 1
   endif
   LIBS := -pthread -lz
else ifeq ($(platform), osx)
   TARGET := libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   ENDIANNESS_DEFINES := -DLSB_FIRST
   IS_X86 = 1
   LIBS := -pthread -lz
else ifeq ($(platform), ps3)
   TARGET := libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN
   HAVE_RZLIB := 1
else ifeq ($(platform), sncps3)
   TARGET := libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   ENDIANNESS_DEFINES := -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN

   HAVE_RZLIB := 1
   CXXFLAGS += -Xc+=exceptions
else ifeq ($(platform), xenon)
   TARGET := libretro_xenon360.a
   CC = xenon-gcc
   CXX = xenon-g++
   AR = xenon-ar
   ENDIANNESS_DEFINES += -D__LIBXENON__ -m32 -D__ppc__ -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN

   LIBS := -pthread -lz
else ifeq ($(platform), wii)
   TARGET := libretro_wii.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar
   ENDIANNESS_DEFINES += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -DMSB_FIRST -DBYTE_ORDER=BIG_ENDIAN

   HAVE_RZLIB := 1
   EXTRA_INCLUDES := -I$(DEVKITPRO)/libogc/include

else
   TARGET := retro.dll
   CC = gcc
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=mednafen/libretro/link.T
   LIBS := -lz
   ENDIANNESS_DEFINES += -D__WIN32__ -D__WIN32_LIBRETRO__ -Wno-missing-field-initializers -DMSB_FIRST
   IS_X86 = 1
endif

ifeq ($(IS_X86), 1)
X86_DEFINES = -DARCH_X86
CFLAGS +=  $(X86_DEFINES)
CXXFLAGS +=  $(X86_DEFINES)
endif

ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g
CXXFLAGS += -O0 -g
else
CFLAGS += -O3 -g
CXXFLAGS += -O3 -g
endif


MEDNAFEN_DIR := mednafen
LIBRETRO_DIR := $(MEDNAFEN_DIR)/libretro
PCE_DIR := $(MEDNAFEN_DIR)/pce
PCE_FAST_DIR := $(MEDNAFEN_DIR)/pce_fast

HW_CPU_SOURCES_C := $(MEDNAFEN_DIR)/hw_cpu/c68k/c68k.c \
	$(MEDNAFEN_DIR)/hw_cpu/c68k/c68kexec.c

HW_CPU_SOURCES := $(MEDNAFEN_DIR)/hw_cpu/huc6280/huc6280.cpp

HW_MISC_SOURCES := $(MEDNAFEN_DIR)/hw_misc/arcade_card/arcade_card.cpp

HW_SOUND_SOURCES := $(MEDNAFEN_DIR)/hw_sound/pce_psg/pce_psg.cpp

HW_VIDEO_SOURCES := $(MEDNAFEN_DIR)/hw_video/huc6270/vdc.cpp

ifeq ($(HAVE_RZLIB), 1)
CFLAGS += -DHAVE_RZLIB=1
CXXFLAGS += -DHAVE_RZLIB=1
endif

PCE_SOURCES := $(PCE_DIR)/vce.cpp \
	$(PCE_DIR)/pce.cpp \
	$(PCE_DIR)/input.cpp \
	$(PCE_DIR)/huc.cpp \
	$(PCE_DIR)/tsushin.cpp \
	$(PCE_DIR)/subhw.cpp \
	$(PCE_DIR)/input/gamepad.cpp \
	$(PCE_DIR)/input/mouse.cpp \
	$(PCE_DIR)/input/tsushinkb.cpp \
	$(PCE_DIR)/mcgenjin.cpp

PCE_FAST_SOURCES := $(PCE_FAST_DIR)/huc.cpp \
	$(PCE_FAST_DIR)/huc6280.cpp \
	$(PCE_FAST_DIR)/input.cpp \
	$(PCE_FAST_DIR)/pce.cpp \
	$(PCE_FAST_DIR)/tsushin.cpp \
	$(PCE_FAST_DIR)/input/gamepad.cpp \
	$(PCE_FAST_DIR)/input/mouse.cpp \
	$(PCE_FAST_DIR)/input/tsushinkb.cpp \
	$(PCE_FAST_DIR)/vdc.cpp

ifeq ($(FAST), 1)
PCE_CORE_SOURCES := $(PCE_FAST_SOURCES)
PCE_CORE_DEFINES := -DWANT_PCE_FAST_EMU
else
PCE_CORE_SOURCES := $(PCE_SOURCES)
PCE_CORE_DEFINES := -DWANT_PCE_EMU
endif

MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(MEDNAFEN_DIR)/mempatcher.cpp \
        $(MEDNAFEN_DIR)/error.cpp \
	$(MEDNAFEN_DIR)/cdrom/cdromif.cpp \
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
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/string/escape.cpp \
	$(MEDNAFEN_DIR)/string/ConvertUTF.cpp \
	$(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp \
	$(MEDNAFEN_DIR)/mednafen.cpp \
	$(MEDNAFEN_DIR)/memory.cpp \
	$(MEDNAFEN_DIR)/video/video.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/okiadpcm.cpp \
	$(MEDNAFEN_DIR)/Stream.cpp \
	$(MEDNAFEN_DIR)/MemoryStream.cpp \
	$(MEDNAFEN_DIR)/tests.cpp \
	$(MEDNAFEN_DIR)/md5.cpp

MPC_SRC := $(wildcard $(MEDNAFEN_DIR)/mpcdec/*.c)
TREMOR_SRC := $(wildcard $(MEDNAFEN_DIR)/tremor/*.c)
TRIO_SRC := $(wildcard $(MEDNAFEN_DIR)/trio/*.c)

SOURCES_C := $(MPC_SRC) \
	$(TREMOR_SRC) \
	$(TRIO_SRC) \
        $(ZLIB_SRC)

SOURCES_C += $(HW_CPU_SOURCES_C)

LIBRETRO_SOURCES := $(LIBRETRO_DIR)/libretro.cpp $(LIBRETRO_DIR)/thread.cpp

SOURCES := $(LIBRETRO_SOURCES) $(HW_CPU_SOURCES) $(HW_MISC_SOURCES) $(HW_SOUND_SOURCES) $(HW_VIDEO_SOURCES) $(PCE_CORE_SOURCES) $(MEDNAFEN_SOURCES)
OBJECTS := $(SOURCES:.cpp=.o) $(SOURCES_C:.c=.o)

all: $(TARGET)

FLAGS += -ffast-math  -funroll-loops -fsigned-char
FLAGS += -I. -I$(MEDNAFEN_DIR) -I$(MEDNAFEN_DIR)/include -I$(MEDNAFEN_DIR)/intl -I$(MEDNAFEN_DIR)/hw_cpu -I$(MEDNAFEN_DIR)/hw_misc -I$(MEDNAFEN_DIR)/hw_sound -I$(MEDNAFEN_DIR)/hw_video -I$(MEDNAFEN_DIR)/compress $(EXTRA_INCLUDES)

FLAGS += $(ENDIANNESS_DEFINES) -DHAVE_MKDIR -DSIZEOF_DOUBLE=8 $(WARNINGS) -DMEDNAFEN_VERSION=\"0.9.24\" -DMEDNAFEN_VERSION_NUMERIC=924 -DPSS_STYLE=1 -DMPC_FIXED_POINT $(PCE_CORE_DEFINES) -DSTDC_HEADERS -D__LIBRETRO__ -D__STDC_LIMIT_MACROS -D_LOW_ACCURACY_

CXXFLAGS += $(FLAGS) $(fpic)
CFLAGS += $(FLAGS) $(fpic) -std=gnu99

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
	$(CXX) $(SHARED) -o $@ $^ $(LDFLAGS) $(LIBS)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
