# Makefile for piotserver
#
# Top-level application build.
# Driver plugins are built from drivers/<PLUGIN_NAME>.
#
# Linux plugin support depends on the piotserver executable exporting symbols:
#
#     LDFLAGS += -rdynamic
#
# Individual driver plugins are built by their own Makefiles.
#
# Useful commands:
#
#     make -j4              Build app and plugins in parallel.
#     make -j4 plugins      Build only plugins in parallel.
#     make clean            Remove app, app objects, plugin objects, and plugin products.
#     make clean-plugins    Clean only plugin object directories and plugin products.
#     make distclean        Remove clean products plus generated auxiliary files.

APP_NAME := piotserver
APP_VERSION := 1.3.0-field
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)


CXX := clang++
CC  := clang

BUILD_DIR := build
SRC_DIR   := src

UNAME_S := $(shell uname -s)

CXXSTD := -std=c++23

WARNINGS := \
	-Wall \
	-Wextra \
	-Wpedantic

CPPFLAGS := \
	-I$(SRC_DIR)

CPPFLAGS += -DPIOTSERVER_VERSION='"$(APP_VERSION) ($(GIT_HASH))"'

CXXFLAGS := \
	$(CXXSTD) \
	$(WARNINGS) \
	-g \
	-O0

CFLAGS := \
	$(WARNINGS) \
	-g \
	-O0

LDFLAGS :=

LDLIBS := \
	-lsqlite3 \
	-pthread

ifeq ($(UNAME_S),Darwin)
	CPPFLAGS += -Imacstuff/macincludes
	PLATFORM_CPP_SOURCES := macstuff/macincludes/macos_gpiod.cpp
else
	#
	# Needed for dlopen()/dlsym() and for plugins that resolve symbols
	# from the main piotserver executable.
	#
	LDFLAGS += -rdynamic
	LDLIBS += -ldl
	PLATFORM_CPP_SOURCES :=
endif

CPP_SOURCES := \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/ServerNouns.cpp \
	$(SRC_DIR)/RPi_RelayBoardDevice.cpp \
	$(SRC_DIR)/W1_Device.cpp \
	$(SRC_DIR)/Actuator_Device.cpp \
	$(SRC_DIR)/Sprinkler_Device.cpp \
	$(SRC_DIR)/I2C.cpp \
	$(SRC_DIR)/GPIO.cpp \
	$(SRC_DIR)/pIoTServerAPISecretMgr.cpp \
	$(SRC_DIR)/pIoTServerDB.cpp \
	$(SRC_DIR)/pIoTServerMgr.cpp \
	$(SRC_DIR)/pIoTServerEvaluator.cpp \
	$(SRC_DIR)/EventTrigger.cpp \
	$(SRC_DIR)/Sequence.cpp \
	$(SRC_DIR)/Action.cpp \
	$(SRC_DIR)/SolarTimeMgr.cpp \
	$(SRC_DIR)/sunset.cpp \
	$(SRC_DIR)/lunar.cpp \
	$(SRC_DIR)/base64.cpp \
	$(SRC_DIR)/RESTutils.cpp \
	$(SRC_DIR)/ServerCmdQueue.cpp \
	$(SRC_DIR)/TCPClientInfo.cpp \
	$(SRC_DIR)/TCPServer.cpp \
	$(SRC_DIR)/REST_URL.cpp \
	$(SRC_DIR)/RESTServerConnection.cpp \
	$(SRC_DIR)/sha256.cpp \
	$(SRC_DIR)/TimeStamp.cpp \
	$(SRC_DIR)/LogMgr.cpp \
	$(PLATFORM_CPP_SOURCES)

C_SOURCES := \
	$(SRC_DIR)/http_parser.c \
	$(SRC_DIR)/yuarel.c

CPP_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

OBJECTS := \
	$(CPP_OBJECTS) \
	$(C_OBJECTS)

DEPS := $(OBJECTS:.o=.d)

PLUGIN_DIRS := \
	VALVEMASTER \
	SAMPLE \
	ADS1115 \
	BME280 \
	MCP3427 \
	MCP23008 \
	PCA9536 \
	PCA9671 \
	PWRGATE \
	QWIIC_RELAY \
	QwiicButton \
	SHT25 \
	SHT30 \
	TCA9534 \
	TANKDEPTH \
	VELM6030 \
	TMP10X

.PHONY: all app plugins clean clean-plugins distclean run dirs print $(PLUGIN_DIRS)

all: app plugins

app: $(APP_NAME)

#
# Each plugin target is independent, so:
#
#     make -j4 plugins
#
# builds multiple driver plugins in parallel.
#
plugins: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C drivers/$@

$(APP_NAME): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: all
	./$(APP_NAME)

clean: clean-plugins
	rm -rf $(BUILD_DIR)
	rm -f $(APP_NAME)

clean-plugins:
	@for plugin in $(PLUGIN_DIRS); do \
		$(MAKE) -C drivers/$$plugin clean; \
	done
	rm -f plugins/*.so
	rm -f plugins/*.dylib

distclean: clean
	rm -f logfile.txt
	rm -f compile_commands.json
	rm -f *.db
	rm -f *.db-shm
	rm -f *.db-wal
print:
	@echo "APP_NAME:    $(APP_NAME)"
	@echo "UNAME_S:     $(UNAME_S)"
	@echo "CXX:         $(CXX)"
	@echo "CC:          $(CC)"
	@echo "CPPFLAGS:    $(CPPFLAGS)"
	@echo "CXXFLAGS:    $(CXXFLAGS)"
	@echo "CFLAGS:      $(CFLAGS)"
	@echo "LDFLAGS:     $(LDFLAGS)"
	@echo "LDLIBS:      $(LDLIBS)"
	@echo "PLUGIN_DIRS:"
	@printf '  %s\n' $(PLUGIN_DIRS)
	@echo "CPP_SOURCES:"
	@printf '  %s\n' $(CPP_SOURCES)
	@echo "C_SOURCES:"
	@printf '  %s\n' $(C_SOURCES)

-include $(DEPS)
