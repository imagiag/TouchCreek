# Creek firmware for the Synthux Touch 2 (Daisy Seed)
TARGET = CreekTouch

# USB carries MIDI in the normal build. `make monitor` builds CreekTouch-monitor
# instead, which sends CPU / voice / stack stats over USB serial once a second
# for web/monitor.html (no MIDI) and adds the P09 stress test. Only built when
# needed (not part of releases).

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
ifeq ($(MONITOR),1)
C_DEFS += -DCREEK_MONITOR=1 -DCREEK_STRESS=1
endif
LDFLAGS += -Wl,--no-warn-rwx-segments

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)

monitor:
	$(MAKE) MONITOR=1 BUILD_DIR=build-monitor TARGET=CreekTouch-monitor

.PHONY: monitor
