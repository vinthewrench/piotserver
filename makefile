# Makefile for piotserver
#
# Top-level application build.
# Driver plugins are built from drivers/<PLUGIN_NAME>.
#
# Linux plugin support depends on the piotserver executable exporting symbols:
#
#     LDFLAGS += -rdynamic
#
# Common runtime code that must be shared by the app and plugins is built into:
#
#     lib/libpiotcore.so
#
# Useful commands:
#
#     make -j4              Build app, shared core, and plugins in parallel, then strip products.
#     make -j4 nostrip      Build app, shared core, and plugins without stripping products.
#     make -j4 core         Build only shared core library.
#     make -j4 app          Build only app and shared core library without stripping the app.
#     make -j4 plugins      Build only plugins in parallel.
#     make strip            Strip app, shared core, and plugins.
#     make clean            Remove app, shared core, app objects, plugin objects, and plugin products.
#     make clean-plugins    Clean only plugin object directories and plugin products.
#     make distclean        Remove clean products plus generated auxiliary files.

APP_NAME := piotserver
APP_VERSION := 1.5.0-field
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)

CXX := clang++
CC  := clang

BUILD_DIR := build
SRC_DIR   := src
LIB_DIR   := lib

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

	CORE_LIB_NAME := libpiotcore.dylib
	CORE_SHARED_FLAG := -dynamiclib
else
	#
	# Needed for dlopen()/dlsym() and for plugins that resolve symbols
	# from the main piotserver executable.
	#
	LDFLAGS += -rdynamic
	LDFLAGS += -Wl,-rpath,'$$ORIGIN/$(LIB_DIR)'
	LDLIBS += -ldl
	PLATFORM_CPP_SOURCES :=

	CORE_LIB_NAME := libpiotcore.so
	CORE_SHARED_FLAG := -shared
endif

CORE_LIB := $(LIB_DIR)/$(CORE_LIB_NAME)

#
# Common runtime code shared by the app and plugins.
#
# Keep this small. Do not put application/server logic here.
#
CORE_CPP_SOURCES := \
	$(SRC_DIR)/I2C.cpp \
	$(SRC_DIR)/LogMgr.cpp \
	$(SRC_DIR)/TimeStamp.cpp

CORE_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/core/%.o,$(CORE_CPP_SOURCES))
CORE_DEPS    := $(CORE_OBJECTS:.o=.d)

#
# Main application sources.
#
# I2C.cpp, LogMgr.cpp, and TimeStamp.cpp are intentionally not listed here.
# They are provided by libpiotcore.
#
CPP_SOURCES := \
	$(SRC_DIR)/Action.cpp \
	$(SRC_DIR)/Actuator_Device.cpp \
	$(SRC_DIR)/base64.cpp \
	$(SRC_DIR)/EventTrigger.cpp \
	$(SRC_DIR)/GPIO.cpp \
	$(SRC_DIR)/IncidentMgr.cpp \
	$(SRC_DIR)/lunar.cpp \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/pIoTServerAPISecretMgr.cpp \
	$(SRC_DIR)/pIoTServerDB_Core.cpp \
	$(SRC_DIR)/pIoTServerDB_APISecrets.cpp \
	$(SRC_DIR)/pIoTServerDB_Incidents.cpp \
	$(SRC_DIR)/pIoTServerDB_Properties.cpp \
	$(SRC_DIR)/pIoTServerDB_Rules.cpp \
	$(SRC_DIR)/pIoTServerDB_Sequences.cpp \
	$(SRC_DIR)/pIoTServerDB_SQLValues.cpp \
	$(SRC_DIR)/pIoTServerDB_Values.cpp \
	$(SRC_DIR)/pIoTServerEvaluator.cpp \
	$(SRC_DIR)/pIoTServerMgr.cpp \
	$(SRC_DIR)/REST_URL.cpp \
	$(SRC_DIR)/RESTServerConnection.cpp \
	$(SRC_DIR)/RESTutils.cpp \
	$(SRC_DIR)/RPi_RelayBoardDevice.cpp \
	$(SRC_DIR)/Rule.cpp \
	$(SRC_DIR)/Sequence.cpp \
	$(SRC_DIR)/ServerCmdQueue.cpp \
	$(SRC_DIR)/ServerNouns.cpp \
	$(SRC_DIR)/sha256.cpp \
	$(SRC_DIR)/SolarTimeMgr.cpp \
	$(SRC_DIR)/Sprinkler_Device.cpp \
	$(SRC_DIR)/sunset.cpp \
	$(SRC_DIR)/TCPClientInfo.cpp \
	$(SRC_DIR)/TCPServer.cpp \
	$(SRC_DIR)/W1_Device.cpp \
	$(PLATFORM_CPP_SOURCES)

C_SOURCES := \
	$(SRC_DIR)/http_parser.c \
	$(SRC_DIR)/yuarel.c

CPP_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

OBJECTS := \
	$(CPP_OBJECTS) \
	$(C_OBJECTS)

DEPS := \
	$(OBJECTS:.o=.d) \
	$(CORE_DEPS)

PLUGIN_DIRS := \
	POWERCONTROL \
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
	TMP10X	\
	FAULT_SIG

.PHONY: all nostrip core app plugins strip strip-app strip-core strip-plugins clean clean-plugins distclean run dirs print $(PLUGIN_DIRS)

all: core app plugins strip

nostrip: core app plugins

core: $(CORE_LIB)

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

$(CORE_LIB): $(CORE_OBJECTS)
	@mkdir -p $(LIB_DIR)
	$(CXX) $(CORE_SHARED_FLAG) -o $@ $^

$(APP_NAME): $(OBJECTS) $(CORE_LIB)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) -L$(LIB_DIR) -lpiotcore $(LDLIBS)

$(BUILD_DIR)/core/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fPIC -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

strip: strip-app strip-core strip-plugins

strip-app: app
ifneq ($(UNAME_S),Darwin)
	strip $(APP_NAME)
endif

strip-core: core
ifneq ($(UNAME_S),Darwin)
	strip $(CORE_LIB)
endif

strip-plugins: plugins
ifneq ($(UNAME_S),Darwin)
	@if ls plugins/*.so >/dev/null 2>&1; then \
		strip plugins/*.so; \
	fi
endif

run: nostrip
	./$(APP_NAME)

clean: clean-plugins
	rm -rf $(BUILD_DIR)
	rm -rf $(LIB_DIR)
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
	@echo "APP_NAME:         $(APP_NAME)"
	@echo "UNAME_S:          $(UNAME_S)"
	@echo "CXX:              $(CXX)"
	@echo "CC:               $(CC)"
	@echo "CPPFLAGS:         $(CPPFLAGS)"
	@echo "CXXFLAGS:         $(CXXFLAGS)"
	@echo "CFLAGS:           $(CFLAGS)"
	@echo "LDFLAGS:          $(LDFLAGS)"
	@echo "LDLIBS:           $(LDLIBS)"
	@echo "CORE_LIB:         $(CORE_LIB)"
	@echo "CORE_CPP_SOURCES:"
	@printf '  %s\n' $(CORE_CPP_SOURCES)
	@echo "PLUGIN_DIRS:"
	@printf '  %s\n' $(PLUGIN_DIRS)
	@echo "CPP_SOURCES:"
	@printf '  %s\n' $(CPP_SOURCES)
	@echo "C_SOURCES:"
	@printf '  %s\n' $(C_SOURCES)

-include $(DEPS)
