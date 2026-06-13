# Makefile for VALVEMASTER pIoTServer plugin
#
# Builds plugins/VALVEMASTER.so on Linux.
# Builds plugins/VALVEMASTER.dylib on macOS.
#
# This plugin intentionally uses common runtime code from libpiotcore.
# In particular, I2C.cpp is not compiled into the plugin. The plugin resolves
# I2C symbols from libpiotcore.so so all I2C users share one implementation.

CC  := clang
CXX := clang++

PLUGIN_NAME := VALVEMASTER
VALVEMASTER_DRIVER_VERSION ?= 2.0.0

ROOT_DIR     := ../..
SRC_DIR      := $(ROOT_DIR)/src
BUILD_DIR    := $(ROOT_DIR)/build/drivers/$(PLUGIN_NAME)
PLUGIN_DIR   := $(ROOT_DIR)/plugins
CORE_LIB_DIR := $(ROOT_DIR)/lib

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

#
# Make the VALVEMASTER driver version available to the source.
#
# The production pIoTServer plugin should advertise a stable integration
# version. Keep temporary dev/test suffixes in the standalone test tree unless
# you intentionally want them visible on the farm runtime.
#
CXXFLAGS += -DVALVEMASTER_DRIVER_VERSION=\"$(VALVEMASTER_DRIVER_VERSION)\"

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
	# plugins/VALVEMASTER.so needs to find lib/libpiotcore.so at runtime.
	# From plugins/, that path is ../lib.
	PLUGIN_LDFLAGS := -shared -Wl,-rpath,'$$ORIGIN/../lib'

	LDLIBS += -L$(CORE_LIB_DIR) -lpiotcore
endif

TARGET := $(PLUGIN_DIR)/$(PLUGIN_NAME).$(PLUGIN_EXT)

OBJS := \
	$(BUILD_DIR)/VALVEMASTER_factory.o \
	$(BUILD_DIR)/VALVEMASTER_Device.o \
	$(BUILD_DIR)/VALVEMASTER.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs print

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/VALVEMASTER_factory.o: VALVEMASTER_factory.cpp VALVEMASTER_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/VALVEMASTER_Device.o: VALVEMASTER_Device.cpp VALVEMASTER_Device.hpp VALVEMASTER.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/VALVEMASTER.o: VALVEMASTER.cpp VALVEMASTER.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -f $(TARGET)

print:
	@echo "PLUGIN_NAME:     $(PLUGIN_NAME)"
	@echo "VALVEMASTER_DRIVER_VERSION: $(VALVEMASTER_DRIVER_VERSION)"
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
