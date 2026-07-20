# Makefile for the MQTT pIoTServer plugin
#
# Builds plugins/MQTT.so on Linux.
# Builds plugins/MQTT.dylib on macOS.

CC  := clang
CXX := clang++
PKG_CONFIG ?= pkg-config

PLUGIN_NAME := MQTT

ROOT_DIR     := ../..
SRC_DIR      := $(ROOT_DIR)/src
BUILD_DIR    := $(ROOT_DIR)/build/drivers/$(PLUGIN_NAME)
PLUGIN_DIR   := $(ROOT_DIR)/plugins
CORE_LIB_DIR := $(ROOT_DIR)/lib

UNAME_S := $(shell uname -s)

MOSQUITTO_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmosquitto 2>/dev/null)
MOSQUITTO_LIBS   := $(shell $(PKG_CONFIG) --libs libmosquitto 2>/dev/null)

CPPFLAGS := \
	-I$(SRC_DIR) \
	-I. \
	$(MOSQUITTO_CFLAGS)

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
LDLIBS  := $(MOSQUITTO_LIBS) -pthread

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
	$(BUILD_DIR)/MQTT_factory.o \
	$(BUILD_DIR)/MQTT_Device.o \
	$(BUILD_DIR)/MQTT.o

DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean dirs check-deps print

all: check-deps dirs $(TARGET)

check-deps:
	@$(PKG_CONFIG) --exists libmosquitto || { \
		echo "Missing build dependency: libmosquitto development files"; \
		exit 1; \
	}

dirs:
	mkdir -p $(BUILD_DIR)
	mkdir -p $(PLUGIN_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(PLUGIN_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/MQTT_factory.o: MQTT_factory.cpp MQTT_Device.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/MQTT_Device.o: MQTT_Device.cpp MQTT_Device.hpp MQTT.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/MQTT.o: MQTT.cpp MQTT.hpp
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
	@echo "CXX:             $(CXX)"
	@echo "CPPFLAGS:        $(CPPFLAGS)"
	@echo "CXXFLAGS:        $(CXXFLAGS)"
	@echo "PLUGIN_LDFLAGS:  $(PLUGIN_LDFLAGS)"
	@echo "LDLIBS:          $(LDLIBS)"

-include $(DEPS)
