# Creek firmware for the Synthux Touch 2 (Daisy Seed)
TARGET = CreekTouch

# DEBUG = 1 prints CPU load and voice stats over USB serial
# DEBUG = 1

CPP_SOURCES = CreekTouch.cpp $(wildcard creek/*.cpp) $(wildcard ui/*.cpp) $(wildcard touch/*.cpp)
C_INCLUDES = -Icommon/ -Itouch/

# libDaisy: git submodule in lib/libDaisy (Synthux's fork, pinned).
# Clone with --recurse-submodules, or run: git submodule update --init --recursive
LIBDAISY_DIR ?= lib/libDaisy
# No DaisySP needed.

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

CPP_STANDARD = -std=gnu++17
# -Os keeps the 128 KB internal flash comfortable; the DSP files in creek/ force -O3.
OPT = -Os
ifeq ($(DEBUG),1)
C_DEFS += -DDEBUG=1
endif
LDFLAGS += -Wl,--no-warn-rwx-segments

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)
