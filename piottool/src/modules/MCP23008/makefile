#
# piottool/src/modules/MCP23008/makefile
#
# Builds MCP23008Tool.so for piottool.
#

MODULE_NAME := MCP23008Tool

ROOT_DIR     := ../../../..
PIOTTOOL_DIR := ../../../
SRC_DIR      := $(PIOTTOOL_DIR)/src
DRIVER_DIR   := $(ROOT_DIR)/drivers/MCP23008
BUILD_DIR    := $(ROOT_DIR)/build/piottool/modules/MCP23008
LIB_DIR      := $(ROOT_DIR)/lib
PLUGIN_DIR   := $(PIOTTOOL_DIR)/plugins

CXX := clang++

UNAME_S := $(shell uname -s)

CXXSTD := -std=c++23

WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I$(ROOT_DIR)/src \
	-I$(DRIVER_DIR)

CXXFLAGS := \
	$(CXXSTD) \
	$(WARNINGS) \
	-g \
	-O0 \
	-fPIC

LDFLAGS := \
	-L$(LIB_DIR)

LDLIBS := \
	-lpiotcore \
	-pthread

ifeq ($(UNAME_S),Darwin)
	PRODUCT := $(PLUGIN_DIR)/$(MODULE_NAME).dylib
	SHARED_FLAG := -dynamiclib
	LDFLAGS += -Wl,-rpath,@loader_path/../../lib
else
	PRODUCT := $(PLUGIN_DIR)/$(MODULE_NAME).so
	SHARED_FLAG := -shared
	LDFLAGS += -Wl,-rpath,'$$ORIGIN/../../lib'
	LDLIBS += -ldl
endif

LOCAL_OBJECTS := \
	$(BUILD_DIR)/MCP23008Tool.o

DRIVER_OBJECTS := \
	$(BUILD_DIR)/drivers/MCP23008.o

OBJECTS := \
	$(LOCAL_OBJECTS) \
	$(DRIVER_OBJECTS)

DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean print

all: $(PRODUCT)

$(PRODUCT): $(OBJECTS)
	@mkdir -p $(PLUGIN_DIR)
	$(CXX) $(SHARED_FLAG) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/MCP23008Tool.o: MCP23008Tool.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/drivers/MCP23008.o: $(DRIVER_DIR)/MCP23008.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(PRODUCT)

print:
	@echo "MODULE_NAME:    $(MODULE_NAME)"
	@echo "UNAME_S:        $(UNAME_S)"
	@echo "CXX:            $(CXX)"
	@echo "CPPFLAGS:       $(CPPFLAGS)"
	@echo "CXXFLAGS:       $(CXXFLAGS)"
	@echo "LDFLAGS:        $(LDFLAGS)"
	@echo "LDLIBS:         $(LDLIBS)"
	@echo "PRODUCT:        $(PRODUCT)"
	@echo "OBJECTS:"
	@printf '  %s\n' $(OBJECTS)

-include $(DEPS)
