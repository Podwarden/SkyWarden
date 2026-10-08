HEAP_SIZE      = 8388208
STACK_SIZE     = 61800
PRODUCT = SkyWarden.pdx

ifndef PLAYDATE_SDK_PATH
PLAYDATE_SDK_PATH = $(HOME)/Developer/PlaydateSDK
endif
SDK = $(PLAYDATE_SDK_PATH)

SRC = src/bally.c \
      src/physics.c \
      src/wind.c \
      src/balloon.c \
      src/camera.c \
      src/tower.c \
      src/decor.c \
      src/enemies.c \
      src/audio_util.c \
      src/audio.c \
      src/nav.c \
      src/tutorial.c \
      engine/src/engine.c \
      engine/src/dispatcher.c \
      engine/src/ay_mini.c \
      engine/src/formats/psg.c \
      engine/src/formats/vtx.c \
      engine/src/formats/ym.c \
      engine/src/formats/pt3.c \
      engine/src/formats/ay.c \
      engine/src/formats/ay_z80_glue.c \
      third_party/lh5/lh5.c \
      third_party/pt3/pt3.c \
      third_party/z80emu/z80emu.c \
      src/music_map.c \
      src/music.c \
      src/capture_audio.c

UINCDIR = src engine/include engine/src third_party/lh5 third_party/pt3 third_party/z80emu

ASRC =

include $(PLAYDATE_SDK_PATH)/C_API/buildsupport/common.mk

# macOS: ensure pdc finds CoreLibs (mirrors playdate/Makefile)
override PDCFLAGS = -sdkpath $(PLAYDATE_SDK_PATH)
