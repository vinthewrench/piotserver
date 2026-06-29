# Makefile for SHUTDOWN_SIG pIoTServer plugin
#
# Builds plugins/SHUTDOWN_SIG.so on Linux.
# Builds plugins/SHUTDOWN_SIG.dylib on macOS.
#
# This plugin intentionally uses common runtime code from libpiotcore.

CC  := clang
CXX := clang++

PLUGIN_NAME := SHUTDOWN_SIG
SHUTDOWN_SIG_DRIVER_VERSION ?= 1.0.0

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

CXXFLAGS += -DSHUTDOWN_SIG_DRIVER_VERSION=\"$(SHUTDOWN_SIG_DRIVER_VERSION)\"

LDFLAGS :=
LDLIBS  :=

ifeq ($(UNAME_S),Darwin)
	CC  := clang
	CXX := clang++

	PLUGIN_EXT := dylib
	PLUGIN_LDFLAGS := -dynamiclib -undefined dynamic_lookup
else
	PLUGIN_EXT := so
	PLUGIN_LDFLAGS := -shared -Wl,-rpath,'$$ORIGIN/../lib'

	LDLIBS += -L$(CORE_LIB_DIR) -lpiotcore
endif

TARGET := $(PLUGIN_DIR)/$(PLUGIN_NAME).$(PLUGIN_EXT)

OBJS := \
	$(BUILD_DIR)/SHUTDOWN_SIG_factory.o \
	$(BUILD_DIR)/SHUTDOWN_SIG_Device.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs print

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/SHUTDOWN_SIG_Device.o: SHUTDOWN_SIG_Device.cpp SHUTDOWN_SIG_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/SHUTDOWN_SIG_factory.o: SHUTDOWN_SIG_factory.cpp SHUTDOWN_SIG_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -f $(TARGET)

print:
	@echo "PLUGIN_NAME:              $(PLUGIN_NAME)"
	@echo "SHUTDOWN_SIG_DRIVER_VERSION: $(SHUTDOWN_SIG_DRIVER_VERSION)"
	@echo "UNAME_S:                  $(UNAME_S)"
	@echo "TARGET:                   $(TARGET)"
	@echo "CC:                       $(CC)"
	@echo "CXX:                      $(CXX)"
	@echo "CPPFLAGS:                 $(CPPFLAGS)"
	@echo "CXXFLAGS:                 $(CXXFLAGS)"
	@echo "LDFLAGS:                  $(LDFLAGS)"
	@echo "PLUGIN_LDFLAGS:           $(PLUGIN_LDFLAGS)"
	@echo "LDLIBS:                   $(LDLIBS)"
	@echo "CORE_LIB_DIR:             $(CORE_LIB_DIR)"
	@echo "OBJS:"
	@printf '  %s\n' $(OBJS)

-include $(DEPS)
