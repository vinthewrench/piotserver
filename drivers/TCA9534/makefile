# Makefile for TCA9534 pIoTServer plugin
#
# Builds plugins/TCA9534.so on Linux.
# Builds plugins/TCA9534.dylib on macOS.
#
# This plugin intentionally references host-provided symbols from piotserver.
# The top-level piotserver executable must export symbols on Linux, usually:
#
#     LDFLAGS += -rdynamic
#
# Do not link host singleton implementation files such as LogMgr.cpp into the
# plugin. The plugin should use the host process symbols.

CC  := clang
CXX := clang++

PLUGIN_NAME := TCA9534

ROOT_DIR   := ../..
SRC_DIR    := $(ROOT_DIR)/src
BUILD_DIR  := $(ROOT_DIR)/build/drivers/$(PLUGIN_NAME)
PLUGIN_DIR := $(ROOT_DIR)/plugins

UNAME_S := $(shell uname -s)

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I.

WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic

CXXFLAGS := \
	-std=gnu++23 \
	$(WARNINGS) \
	-O2 \
	-g \
	-fPIC \
	-MMD \
	-MP

LDFLAGS :=
LDLIBS  :=

ifeq ($(UNAME_S),Darwin)
	CC  := clang
	CXX := clang++

	PLUGIN_EXT := dylib

	# macOS plugin build.
	#
	# The plugin intentionally references symbols supplied by the host process.
	# -undefined dynamic_lookup tells the macOS linker that unresolved symbols
	# may be resolved from the executable that loads this plugin with dlopen().
	PLUGIN_LDFLAGS := -dynamiclib -undefined dynamic_lookup
else
	PLUGIN_EXT := so

	# Linux plugin build.
	#
	# Shared objects used as plugins commonly leave host-provided symbols to be
	# resolved by the process that calls dlopen().
	PLUGIN_LDFLAGS := -shared
endif

TARGET := $(PLUGIN_DIR)/$(PLUGIN_NAME).$(PLUGIN_EXT)

OBJS := \
	$(BUILD_DIR)/TCA9534_factory.o \
	$(BUILD_DIR)/TCA9534_Device.o \
	$(BUILD_DIR)/TCA9534.o \
	$(BUILD_DIR)/I2C.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs print

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/TCA9534_factory.o: TCA9534_factory.cpp TCA9534_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/TCA9534_Device.o: TCA9534_Device.cpp TCA9534_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/TCA9534.o: TCA9534.cpp TCA9534.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/I2C.o: $(SRC_DIR)/I2C.cpp $(SRC_DIR)/I2C.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -f $(TARGET)

print:
	@echo "PLUGIN_NAME: $(PLUGIN_NAME)"
	@echo "UNAME_S:     $(UNAME_S)"
	@echo "TARGET:      $(TARGET)"
	@echo "CC:          $(CC)"
	@echo "CXX:         $(CXX)"
	@echo "CPPFLAGS:    $(CPPFLAGS)"
	@echo "CXXFLAGS:    $(CXXFLAGS)"
	@echo "LDFLAGS:     $(LDFLAGS)"
	@echo "PLUGIN_LDFLAGS: $(PLUGIN_LDFLAGS)"
	@echo "LDLIBS:      $(LDLIBS)"
	@echo "OBJS:"
	@printf '  %s\n' $(OBJS)

-include $(DEPS)
