# Makefile for BME280 pIoTServer plugin
#
# Builds plugins/BME280.so on Linux.
# Builds plugins/BME280.dylib on macOS.
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

PLUGIN_NAME := BME280

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
	-Wpedantic \
	-Wno-misleading-indentation \
	-Wno-sign-compare \
	-Wno-unused-parameter \
	-Wno-ignored-qualifiers \
	-Wno-reorder-ctor \
	-Wno-deprecated-copy-with-user-provided-copy \
	-Wno-unused-label \
	-Wno-unused-variable \
	-Wno-unused-but-set-parameter \
	-Wno-missing-field-initializers \
	-Wno-self-assign \
	-Wno-unused-const-variable

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
	$(BUILD_DIR)/BME280_factory.o \
	$(BUILD_DIR)/BME280_Device.o \
	$(BUILD_DIR)/BME280.o \
	$(BUILD_DIR)/I2C.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs print

all: dirs $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/BME280_factory.o: BME280_factory.cpp BME280_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/BME280_Device.o: BME280_Device.cpp BME280_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/BME280.o: BME280.cpp BME280.hpp
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
