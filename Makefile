TARGET := libretro-pce.so

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
	$(MEDNAFEN_DIR)/netplay.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/player.cpp \
	$(MEDNAFEN_DIR)/cdplay.cpp \
	$(MEDNAFEN_DIR)/FileWrapper.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/tests.cpp \
	$(MEDNAFEN_DIR)/movie.cpp \
	$(MEDNAFEN_DIR)/endian.cpp \
	$(MEDNAFEN_DIR)/qtrecord.cpp \
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
	$(MEDNAFEN_DIR)/video/tblur.cpp \
	$(MEDNAFEN_DIR)/video/png.cpp \
	$(MEDNAFEN_DIR)/video/Deinterlacer.cpp \
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/video/resize.cpp \
	$(MEDNAFEN_DIR)/string/escape.cpp \
	$(MEDNAFEN_DIR)/string/ConvertUTF.cpp \
	$(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp \
	$(MEDNAFEN_DIR)/sound/Stereo_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/WAVRecord.cpp \
	$(MEDNAFEN_DIR)/sound/sound.cpp \
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


LDFLAGS += -Wl,--no-undefined -fPIC -shared -lz -Wl,--version-script=link.T -pthread
FLAGS += -ffast-math -msse -msse2 -funroll-loops -O3 -g -Wall -fPIC -fno-strict-overflow
FLAGS += -I. -Imednafen -Imednafen/include -Imednafen/intl -Imednafen/hw_cpu -Imednafen/hw_misc -Imednafen/hw_sound -Imednafen/hw_video -pthread

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

FLAGS += -DLSB_FIRST -DHAVE_MKDIR -DSIZEOF_DOUBLE=8 $(WARNINGS) \
			-DMEDNAFEN_VERSION=\"0.9.22\" -DMEDNAFEN_VERSION_NUMERIC=922 -DPSS_STYLE=1 -DMPC_FIXED_POINT -DARCH_X86 \
			-DWANT_PCE_EMU -DSTDC_HEADERS

CXXFLAGS += $(FLAGS)
CFLAGS += $(FLAGS) -std=gnu99

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
