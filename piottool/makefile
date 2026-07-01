#
# piottool/makefile
#
# Standalone pIoTServer hardware/tool utility.
#
# This builds:
#
#     piottool
#
# inside this directory.
#
# Dynamic tool modules are built into:
#
#     plugins/
#
# It links against:
#
#     ../lib/libpiotcore.so
#
# The top-level makefile is responsible for building libpiotcore first:
#
#     make piottool
#

APP_NAME := piottool

ROOT_DIR  := ..
SRC_DIR   := src
BUILD_DIR := $(ROOT_DIR)/build/piottool
LIB_DIR   := $(ROOT_DIR)/lib

TARGET := $(APP_NAME)

CXX := clang++
CC  := clang

UNAME_S := $(shell uname -s)

CXXSTD := -std=c++23

WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I$(ROOT_DIR)/src

CXXFLAGS := \
	$(CXXSTD) \
	$(WARNINGS) \
	-g \
	-O0

CFLAGS := \
	$(WARNINGS) \
	-g \
	-O0

LDFLAGS := \
	-L$(LIB_DIR)

LDLIBS := \
	-lpiotcore \
	-pthread

ifeq ($(UNAME_S),Darwin)
	CORE_LIB_NAME := libpiotcore.dylib
	LDFLAGS += -Wl,-rpath,@loader_path/../lib
else
	CORE_LIB_NAME := libpiotcore.so
	LDFLAGS += -Wl,-rpath,'$$ORIGIN/../lib'
	LDLIBS += -ldl
endif

CORE_LIB := $(LIB_DIR)/$(CORE_LIB_NAME)

CPP_SOURCES := \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/CmdLineParser.cpp \
	$(SRC_DIR)/ModuleRegistry.cpp \
	$(SRC_DIR)/ModuleLoader.cpp

C_SOURCES :=

CPP_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

OBJECTS := \
	$(CPP_OBJECTS) \
	$(C_OBJECTS)

DEPS := $(OBJECTS:.o=.d)

MODULE_DIRS := \
	PCA9536	\
	TMP10X

.PHONY: all modules clean clean-modules distclean print $(MODULE_DIRS)

all: $(TARGET) modules

modules: $(MODULE_DIRS)

$(MODULE_DIRS):
	$(MAKE) -C $(SRC_DIR)/modules/$@

$(TARGET): $(OBJECTS) $(CORE_LIB)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean: clean-modules
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)

clean-modules:
	@for module in $(MODULE_DIRS); do \
		$(MAKE) -C $(SRC_DIR)/modules/$$module clean; \
	done
	rm -f plugins/*.so
	rm -f plugins/*.dylib

distclean: clean

print:
	@echo "APP_NAME:       $(APP_NAME)"
	@echo "UNAME_S:        $(UNAME_S)"
	@echo "CXX:            $(CXX)"
	@echo "CC:             $(CC)"
	@echo "CPPFLAGS:       $(CPPFLAGS)"
	@echo "CXXFLAGS:       $(CXXFLAGS)"
	@echo "CFLAGS:         $(CFLAGS)"
	@echo "LDFLAGS:        $(LDFLAGS)"
	@echo "LDLIBS:         $(LDLIBS)"
	@echo "CORE_LIB:       $(CORE_LIB)"
	@echo "TARGET:         $(TARGET)"
	@echo "MODULE_DIRS:"
	@printf '  %s\n' $(MODULE_DIRS)
	@echo "CPP_SOURCES:"
	@printf '  %s\n' $(CPP_SOURCES)
	@echo "C_SOURCES:"
	@printf '  %s\n' $(C_SOURCES)

-include $(DEPS)
