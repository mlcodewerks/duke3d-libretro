# GNU Make libretro build. Run from the repository root.
# Native: make -j4 ; cross: make platform=rpi4 CROSS_COMPILE=arm-linux-gnueabihf-
.DEFAULT_GOAL := all
SHELL := /bin/sh
HOST_OS := $(shell uname -s 2>/dev/null)
ifeq ($(origin platform),undefined)
platform := unix
ifeq ($(HOST_OS),Darwin)
platform := osx
else ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_OS)),)
platform := windows
else ifeq ($(OS),Windows_NT)
platform := windows
endif
endif

CROSS_COMPILE ?=
CC_ORIGIN := $(origin CC)
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin CC),undefined)
CC := $(CROSS_COMPILE)gcc
endif
TARGET_NAME ?= chocolate_duke3d_libretro
OUT_DIR ?= .
DEBUG ?= 0
BUILD_DIR ?= build-make/$(platform)/$(if $(filter 1,$(DEBUG)),debug,release)
include Makefile.common

PLATFORM_CFLAGS := -fPIC
PLATFORM_LDFLAGS := -shared -Wl,--no-undefined
PLATFORM_LIBS := -lm
EXT := so
EXE_EXT :=
SUPPORTED_PLATFORMS := unix linux windows win mingw osx macos aarch64 armv7 \
 rpi rpi1 rpi2 rpi3 rpi4 rpi5 rpi3_64 rpi4_64 rpi5_64
ifeq ($(filter $(platform),$(SUPPORTED_PLATFORMS)),)
$(error Unknown platform '$(platform)'. Supported: $(SUPPORTED_PLATFORMS))
endif

ifneq ($(filter windows win mingw,$(platform)),)
EXT := dll
EXE_EXT := .exe
PLATFORM_CFLAGS :=
PLATFORM_LIBS :=
endif
ifneq ($(filter osx macos,$(platform)),)
EXT := dylib
PLATFORM_LDFLAGS := -dynamiclib -Wl,-undefined,error
CORE_DEFINES += -DPLATFORM_MACOSX=1 -D_XOPEN_SOURCE=700
ifneq ($(filter default undefined,$(CC_ORIGIN)),)
CC := $(CROSS_COMPILE)clang
endif
endif

ifneq ($(filter rpi%,$(platform)),)
COMPILER_TARGET := $(shell $(CC) -dumpmachine 2>/dev/null)
RPI_64 := $(filter rpi3_64 rpi4_64 rpi5_64,$(platform))$(filter aarch64% arm64%,$(COMPILER_TARGET))
ifneq ($(filter rpi rpi1,$(platform)),)
PLATFORM_CFLAGS += -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -marm
else
ifneq ($(filter rpi2,$(platform)),)
RPI_CPU := cortex-a7
else ifneq ($(filter rpi3 rpi3_64,$(platform)),)
RPI_CPU := cortex-a53
else ifneq ($(filter rpi4 rpi4_64,$(platform)),)
RPI_CPU := cortex-a72
else
RPI_CPU := cortex-a76
endif
PLATFORM_CFLAGS += -mcpu=$(RPI_CPU)
ifeq ($(strip $(RPI_64)),)
ifeq ($(platform),rpi2)
PLATFORM_CFLAGS += -mfpu=neon-vfpv4 -mfloat-abi=hard -marm
else
PLATFORM_CFLAGS += -mfpu=neon-fp-armv8 -mfloat-abi=hard -marm
endif
endif
endif
endif
ifeq ($(platform),aarch64)
PLATFORM_CFLAGS += -march=armv8-a
endif
ifeq ($(platform),armv7)
PLATFORM_CFLAGS += -march=armv7-a -mfpu=neon -mfloat-abi=hard -marm
endif

ifeq ($(DEBUG),1)
OPTFLAGS := -O0 -g
else
OPTFLAGS := -O2 -DNDEBUG
endif
TARGET := $(OUT_DIR)/$(TARGET_NAME).$(EXT)
OBJECTS := $(addprefix $(BUILD_DIR)/,$(SOURCES_C:.c=.o))
DEPS := $(OBJECTS:.o=.d)
COMPILE_FLAGS = $(CPPFLAGS) $(CORE_DEFINES) $(CORE_INCLUDES) $(CORE_CFLAGS) $(OPTFLAGS) $(PLATFORM_CFLAGS) $(CFLAGS)
LINK_FLAGS = $(PLATFORM_LDFLAGS) $(PLATFORM_CFLAGS) $(LDFLAGS)
TESTS := $(BUILD_DIR)/core_smoke$(EXE_EXT) $(BUILD_DIR)/backend_checks$(EXE_EXT)

.PHONY: all clean check gameplay print-config FORCE
all: $(TARGET)

$(TARGET): $(OBJECTS) $(TARGET).build.cfg
	@mkdir -p "$(dir $@)"
	$(CC) $(LINK_FLAGS) -o "$@" $(OBJECTS) $(PLATFORM_LIBS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c $(BUILD_DIR)/build.cfg Makefile Makefile.common
	@mkdir -p "$(dir $@)"
	$(CC) $(COMPILE_FLAGS) -MMD -MP -c "$<" -o "$@"

# Rebuild when the compiler or flags change, even with the same BUILD_DIR.
$(BUILD_DIR)/build.cfg: FORCE
	@mkdir -p "$(dir $@)"
	@printf '%s\n' '$(CC)' '$(COMPILE_FLAGS)' '$(LINK_FLAGS) $(PLATFORM_LIBS) $(LDLIBS)' > "$@.tmp"
	@test -r "$@" && test "$$(cat "$@.tmp")" = "$$(cat "$@")" || cp "$@.tmp" "$@"
	@rm -f "$@.tmp"

# Track which platform produced an output when several targets share OUT_DIR.
$(TARGET).build.cfg: $(BUILD_DIR)/build.cfg FORCE
	@mkdir -p "$(dir $@)"
	@printf '%s\n' '$(abspath $(BUILD_DIR))' > "$@.tmp"
	@cat "$(BUILD_DIR)/build.cfg" >> "$@.tmp"
	@test -r "$@" && test "$$(cat "$@.tmp")" = "$$(cat "$@")" || cp "$@.tmp" "$@"
	@rm -f "$@.tmp"

$(BUILD_DIR)/core_smoke$(EXE_EXT): tests/core_smoke.c $(OBJECTS)
	$(CC) $(COMPILE_FLAGS) -o "$@" "$<" $(OBJECTS) $(PLATFORM_CFLAGS) $(LDFLAGS) $(PLATFORM_LIBS) $(LDLIBS)

$(BUILD_DIR)/backend_checks$(EXE_EXT): tests/backend_checks.c $(OBJECTS)
	$(CC) $(COMPILE_FLAGS) -o "$@" "$<" $(OBJECTS) $(PLATFORM_CFLAGS) $(LDFLAGS) $(PLATFORM_LIBS) $(LDLIBS)

$(BUILD_DIR)/freeaim_gameplay$(EXE_EXT): tests/freeaim_gameplay.c $(OBJECTS)
	$(CC) $(COMPILE_FLAGS) -o "$@" "$<" $(OBJECTS) $(PLATFORM_CFLAGS) $(LDFLAGS) $(PLATFORM_LIBS) $(LDLIBS)

# Only run these on the target machine (or with an explicitly supplied runner).
RUNNER ?=
check: $(TARGET) $(TESTS)
	$(RUNNER) "$(BUILD_DIR)/core_smoke$(EXE_EXT)"
	$(RUNNER) "$(BUILD_DIR)/backend_checks$(EXE_EXT)"

GRP ?=
gameplay: $(TARGET) $(BUILD_DIR)/core_smoke$(EXE_EXT) $(BUILD_DIR)/freeaim_gameplay$(EXE_EXT)
	@test -n "$(GRP)" || { echo 'Set GRP to an absolute Duke3D GRP path'; exit 1; }
	$(RUNNER) "$(BUILD_DIR)/core_smoke$(EXE_EXT)" "$(GRP)"
	$(RUNNER) "$(BUILD_DIR)/freeaim_gameplay$(EXE_EXT)" "$(GRP)"

print-config:
	@printf '%s\n' 'platform=$(platform)' 'CC=$(CC)' 'TARGET=$(TARGET)' 'BUILD_DIR=$(BUILD_DIR)' 'CFLAGS=$(COMPILE_FLAGS)' 'LDFLAGS=$(LINK_FLAGS) $(PLATFORM_LIBS) $(LDLIBS)'

clean:
	rm -f $(OBJECTS) $(DEPS) $(TESTS) "$(BUILD_DIR)/freeaim_gameplay$(EXE_EXT)" "$(BUILD_DIR)/build.cfg" "$(TARGET)" "$(TARGET).build.cfg"

-include $(DEPS)
