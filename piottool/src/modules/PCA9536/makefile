#
# piottool/src/modules/PCA9536/makefile
#
# Builds PCA9536Tool.so for piottool.
#

MODULE_NAME := PCA9536Tool

ROOT_DIR     := ../../../..
PIOTTOOL_DIR := ../../../
SRC_DIR      := $(PIOTTOOL_DIR)/src
DRIVER_DIR   := $(ROOT_DIR)/drivers/PCA9536
BUILD_DIR    := $(ROOT_DIR)/build/piottool/modules/PCA9536
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

CPP_SOURCES := \
	PCA9536Tool.cpp \
	$(DRIVER_DIR)/PCA9536.cpp

OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean print

all: $(PRODUCT)

$(PRODUCT): $(OBJECTS)
	@mkdir -p $(PLUGIN_DIR)
	$(CXX) $(SHARED_FLAG) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
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
	@echo "CPP_SOURCES:"
	@printf '  %s\n' $(CPP_SOURCES)

-include $(DEPS)
