# Makefile for TANKDEPTH pIoTServer plugin
#
# Builds plugins/TANKDEPTH.so on Linux.
# Builds plugins/TANKDEPTH.dylib on macOS.
#
# This plugin intentionally uses common runtime code from libpiotcore.
# In particular, I2C.cpp is not compiled into the plugin. The plugin resolves
# I2C symbols from libpiotcore.so so all I2C users share one implementation.
#
# TANKDEPTH still compiles ADS1115.cpp plugin-locally because it reuses the
# ADS1115 device helper code directly.

CC  := clang
CXX := clang++

PLUGIN_NAME := TANKDEPTH

ROOT_DIR     := ../..
SRC_DIR      := $(ROOT_DIR)/src
ADS1115_DIR  := ../ADS1115
BUILD_DIR    := $(ROOT_DIR)/build/drivers/$(PLUGIN_NAME)
PLUGIN_DIR   := $(ROOT_DIR)/plugins
CORE_LIB_DIR := $(ROOT_DIR)/lib

UNAME_S := $(shell uname -s)

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I$(ADS1115_DIR) \
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
	PLUGIN_LDFLAGS := -dynamiclib -undefined dynamic_lookup
else
	PLUGIN_EXT := so

	# Linux plugin build.
	#
	# plugins/TANKDEPTH.so needs to find lib/libpiotcore.so at runtime.
	# From plugins/, that path is ../lib.
	PLUGIN_LDFLAGS := -shared -Wl,-rpath,'$$ORIGIN/../lib'

	LDLIBS += -L$(CORE_LIB_DIR) -lpiotcore
endif

TARGET := $(PLUGIN_DIR)/$(PLUGIN_NAME).$(PLUGIN_EXT)

OBJS := \
	$(BUILD_DIR)/TankDepth_factory.o \
	$(BUILD_DIR)/TankDepth_Device.o \
	$(BUILD_DIR)/ADS1115.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs print

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/TankDepth_factory.o: TankDepth_factory.cpp TankDepth_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/TankDepth_Device.o: TankDepth_Device.cpp TankDepth_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ADS1115.o: $(ADS1115_DIR)/ADS1115.cpp $(ADS1115_DIR)/ADS1115.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -f $(TARGET)

print:
	@echo "PLUGIN_NAME:     $(PLUGIN_NAME)"
	@echo "UNAME_S:         $(UNAME_S)"
	@echo "TARGET:          $(TARGET)"
	@echo "CC:              $(CC)"
	@echo "CXX:             $(CXX)"
	@echo "CPPFLAGS:        $(CPPFLAGS)"
	@echo "CXXFLAGS:        $(CXXFLAGS)"
	@echo "LDFLAGS:         $(LDFLAGS)"
	@echo "PLUGIN_LDFLAGS:  $(PLUGIN_LDFLAGS)"
	@echo "LDLIBS:          $(LDLIBS)"
	@echo "CORE_LIB_DIR:    $(CORE_LIB_DIR)"
	@echo "OBJS:"
	@printf '  %s\n' $(OBJS)

-include $(DEPS)
