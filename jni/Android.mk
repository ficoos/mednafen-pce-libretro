LOCAL_PATH := $(call my-dir)
FAST = 1

include $(CLEAR_VARS)

MEDNAFEN_DIR := ../mednafen
LIBRETRO_DIR := $(MEDNAFEN_DIR)/libretro
PCE_DIR := $(MEDNAFEN_DIR)/pce
PCE_FAST_DIR := $(MEDNAFEN_DIR)/pce_fast

LOCAL_MODULE    := libretro

LIBRETRO_SOURCES := $(LIBRETRO_DIR)/libretro.cpp $(LIBRETRO_DIR)/thread.cpp



HW_CPU_SOURCES := $(MEDNAFEN_DIR)/hw_cpu/huc6280/huc6280.cpp

HW_MISC_SOURCES := $(MEDNAFEN_DIR)/hw_misc/arcade_card/arcade_card.cpp

HW_SOUND_SOURCES := $(MEDNAFEN_DIR)/hw_sound/pce_psg/pce_psg.cpp

HW_VIDEO_SOURCES := $(MEDNAFEN_DIR)/hw_video/huc6270/vdc.cpp

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
	$(PCE_FAST_DIR)/pce_huc6280.cpp \
	$(PCE_FAST_DIR)/input.cpp \
	$(PCE_FAST_DIR)/pce.cpp \
	$(PCE_FAST_DIR)/tsushin.cpp \
	$(PCE_FAST_DIR)/input/gamepad.cpp \
	$(PCE_FAST_DIR)/input/mouse.cpp \
	$(PCE_FAST_DIR)/input/tsushinkb.cpp \
	$(PCE_FAST_DIR)/vdc.cpp

ifeq ($(FAST), 1)
PCE_CORE_SOURCES := $(PCE_FAST_SOURCES)
SHARED_FLAGS += -DWANT_PCE_FAST_EMU
else
PCE_CORE_SOURCES := $(PCE_SOURCES)
SHARED_FLAGS += -DWANT_PCE_EMU
HW_CPU_SOURCES_C := $(MEDNAFEN_DIR)/hw_cpu/c68k/c68k.c \
	$(MEDNAFEN_DIR)/hw_cpu/c68k/c68kexec.c
endif

MEDNAFEN_SOURCES := $(MEDNAFEN_DIR)/settings.cpp \
	$(MEDNAFEN_DIR)/general.cpp \
	$(MEDNAFEN_DIR)/mednafen-endian.cpp \
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
	$(MEDNAFEN_DIR)/video/surface.cpp \
	$(MEDNAFEN_DIR)/sound/Blip_Buffer.cpp \
	$(MEDNAFEN_DIR)/sound/Fir_Resampler.cpp \
	$(MEDNAFEN_DIR)/mednafen.cpp \
	$(MEDNAFEN_DIR)/video/video.cpp \
	$(MEDNAFEN_DIR)/state.cpp \
	$(MEDNAFEN_DIR)/file.cpp \
	$(MEDNAFEN_DIR)/okiadpcm.cpp \
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

LOCAL_SRC_FILES :=$(LIBRETRO_SOURCES) $(HW_CPU_SOURCES) $(HW_MISC_SOURCES) $(HW_SOUND_SOURCES) $(HW_VIDEO_SOURCES) $(PCE_CORE_SOURCES) $(MEDNAFEN_SOURCES)

SHARED_FLAGS += -O3 -DLSB_FIRST -D__LIBRETRO__ -Wno-write-strings -DANDROID -D__STDC_LIMIT_MACROS -D_LOW_ACCURACY_ -DMEDNAFEN_VERSION_NUMERIC=924 -DPSS_STYLE=1 -DMPC_FIXED_POINT -DSTDC_HEADERS -DSIZEOF_DOUBLE=8 -DHAVE_MKDIR -DMEDNAFEN_VERSION=\"0.9.24\" -DBLARGG_LITTLE_ENDIAN

LOCAL_CFLAGS =  $(SHARED_FLAGS)
LOCAL_CXXFLAGS = $(SHARED_FLAGS) -fexceptions

LOCAL_C_INCLUDES = 

include $(BUILD_SHARED_LIBRARY)
