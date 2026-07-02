//
// VALVEMASTERTool.cpp
// piottool module for the VALVEMASTER I2C-to-RS485 irrigation controller.
//

#include "modules/VALVEMASTER/VALVEMASTERTool.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>
#include <iomanip>

#include "PToolModuleAPI.hpp"
#include "VALVEMASTER.hpp"

#define VALVEMASTER_DEFAULT_ADDR 0x09

#define VALVEMASTER_STATUS_BUSY      (1u << 0)
#define VALVEMASTER_STATUS_ERROR     (1u << 1)
#define VALVEMASTER_STATUS_POWER_ON  (1u << 2)

#define VALVEMASTER_RESULT_OK                    0x00
#define VALVEMASTER_RESULT_BAD_COMMAND           0x01
#define VALVEMASTER_RESULT_BAD_NODE              0x02
#define VALVEMASTER_RESULT_BAD_CHANNEL           0x03
#define VALVEMASTER_RESULT_NODE_NOT_FOUND        0x04
#define VALVEMASTER_RESULT_UNSUPPORTED_CHANNEL   0x05
#define VALVEMASTER_RESULT_CONFIG_REQUIRED       0x06
#define VALVEMASTER_RESULT_ADDRESS_IN_USE        0x07
#define VALVEMASTER_RESULT_BUSY                  0x08
#define VALVEMASTER_RESULT_RS485_TIMEOUT         0x09
#define VALVEMASTER_RESULT_RS485_BAD_CHECKSUM    0x0A
#define VALVEMASTER_RESULT_RS485_BAD_REPLY       0x0B
#define VALVEMASTER_RESULT_RESERVED_0C           0x0C
#define VALVEMASTER_RESULT_POWER_OFF             0x0E

#define VALVEMASTER_TOOL_SHORT_TIMEOUT_MS        5000
#define VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS       15000
#define VALVEMASTER_TOOL_WHO_TIMEOUT_MS          45000
#define VALVEMASTER_TOOL_MOVE_TIMEOUT_MS         30000
#define VALVEMASTER_TOOL_CALLBACK_WAIT_STEP_MS   25

namespace {

static bool parse_u8(const std::string& s, uint8_t& out)
{
    if (s.empty()) {
        return false;
    }

    char* end = nullptr;
    unsigned long value = std::strtoul(s.c_str(), &end, 0);

    if (end == nullptr || *end != '\0' || value > 255) {
        return false;
    }

    out = static_cast<uint8_t>(value);
    return true;
}

static bool parseNode(const std::string& s, uint8_t& node)
{
    if (!parse_u8(s, node)) {
        return false;
    }

    return node >= 1 && node <= 254;
}

static bool parseConfigNode(const std::string& s, uint8_t& node)
{
    if (!parse_u8(s, node)) {
        return false;
    }

    return node == 0 || (node >= 1 && node <= 254);
}

static bool parseChannel(const std::string& s, uint8_t& channel)
{
    if (!parse_u8(s, channel)) {
        return false;
    }

    return channel >= 1 && channel <= 16;
}

static const char* opStatusName(valvemaster_op_status_t status)
{
    switch (status) {
        case VALVEMASTER_OP_OK:
            return "OK";

        case VALVEMASTER_OP_FAILED:
            return "FAILED";

        case VALVEMASTER_OP_FLUSHED:
            return "FLUSHED";

        case VALVEMASTER_OP_STOPPED:
            return "STOPPED";

        default:
            return "UNKNOWN";
    }
}

static const char* resultName(uint8_t result)
{
    switch (result) {
        case VALVEMASTER_RESULT_OK:
            return "OK";

        case VALVEMASTER_RESULT_BAD_COMMAND:
            return "BAD_COMMAND";

        case VALVEMASTER_RESULT_BAD_NODE:
            return "BAD_NODE";

        case VALVEMASTER_RESULT_BAD_CHANNEL:
            return "BAD_CHANNEL";

        case VALVEMASTER_RESULT_NODE_NOT_FOUND:
            return "NODE_NOT_FOUND";

        case VALVEMASTER_RESULT_UNSUPPORTED_CHANNEL:
            return "UNSUPPORTED_CHANNEL";

        case VALVEMASTER_RESULT_CONFIG_REQUIRED:
            return "CONFIG_REQUIRED";

        case VALVEMASTER_RESULT_ADDRESS_IN_USE:
            return "ADDRESS_IN_USE";

        case VALVEMASTER_RESULT_BUSY:
            return "BUSY";

        case VALVEMASTER_RESULT_RS485_TIMEOUT:
            return "RS485_TIMEOUT";

        case VALVEMASTER_RESULT_RS485_BAD_CHECKSUM:
            return "RS485_BAD_CHECKSUM";

        case VALVEMASTER_RESULT_RS485_BAD_REPLY:
            return "RS485_BAD_REPLY";

        case VALVEMASTER_RESULT_RESERVED_0C:
            return "RESERVED_0C";

        case VALVEMASTER_RESULT_POWER_OFF:
            return "POWER_OFF";

        default:
            return "UNKNOWN";
    }
}

static void printCharOrHex(uint8_t value)
{
    if (value >= 32 && value <= 126) {
        std::printf("%c", static_cast<char>(value));
    }
    else {
        std::printf("0x%02X", value);
    }
}

static void printStatusByte(uint8_t status)
{
    std::printf("status: 0x%02X\n", status);
    std::printf("  busy:        %s\n", (status & VALVEMASTER_STATUS_BUSY) ? "yes" : "no");
    std::printf("  error:       %s\n", (status & VALVEMASTER_STATUS_ERROR) ? "yes" : "no");
    std::printf("  field_power: %s\n", (status & VALVEMASTER_STATUS_POWER_ON) ? "on" : "off");
}

static bool printStatus(VALVEMASTER& device)
{
    uint8_t status = 0;
    uint8_t result = 0;
    uint8_t powerState = 0;

    if (!device.readFirmwareStatus(status)) {
        std::fprintf(stderr, "failed to read status\n");
        return false;
    }

    if (!device.readFirmwareResult(result)) {
        std::fprintf(stderr, "failed to read result\n");
        return false;
    }

    if (!device.readFirmwarePowerState(powerState)) {
        std::fprintf(stderr, "failed to read power state\n");
        return false;
    }

    printStatusByte(status);
    std::printf("power_state: 0x%02X\n", powerState);
    std::printf("result: 0x%02X %s\n", result, resultName(result));

    return true;
}

static bool printReply(VALVEMASTER& device)
{
    valvemaster_reply_t reply = {};

    if (!device.readFirmwareReply(reply)) {
        std::fprintf(stderr, "failed to read reply registers\n");
        return false;
    }

    std::printf("reply:\n");
    std::printf("  node: %u\n", reply.node);
    std::printf("  cmd:  ");
    printCharOrHex(reply.cmd);
    std::printf(" / 0x%02X\n", reply.cmd);
    std::printf("  arg0: 0x%02X", reply.arg0);

    if (reply.cmd == 'R') {
        std::printf(" / channel %u", reply.arg0);
    }

    std::printf("\n");
    std::printf("  arg1: 0x%02X", reply.arg1);

    if (reply.cmd == 'R') {
        if (reply.arg1 == 'O') {
            std::printf(" / OPEN");
        }
        else if (reply.arg1 == 'C') {
            std::printf(" / CLOSED");
        }
    }
    else if (reply.cmd == 'V') {
        std::printf(" / version %u.%02u", reply.arg0, reply.arg1);
    }

    std::printf("\n");

    return true;
}

static bool printMap(VALVEMASTER& device)
{
    std::vector<uint8_t> nodes;

    if (!device.readFirmwareNodeMap(nodes)) {
        std::fprintf(stderr, "failed to read node map\n");
        return false;
    }

    std::printf("nodes: %zu\n", nodes.size());

    for (uint8_t node : nodes) {
        std::printf("  %u\n", node);
    }

    return true;
}

static bool printVersion(VALVEMASTER& device)
{
    std::string version;

    if (!device.getVersion(version)) {
        std::fprintf(stderr, "failed to read master version\n");
        return false;
    }

    std::printf("master version: %s\n", version.c_str());
    return true;
}

static void printCommandResult(valvemaster_op_status_t opStatus,
                               uint8_t firmwareResult,
                               uint8_t firmwareStatus)
{
    std::printf("op_status: %s\n", opStatusName(opStatus));
    std::printf("firmware_result: 0x%02X %s\n",
                firmwareResult,
                resultName(firmwareResult));
    std::printf("firmware_status: 0x%02X\n", firmwareStatus);
    std::printf("  busy:        %s\n", (firmwareStatus & VALVEMASTER_STATUS_BUSY) ? "yes" : "no");
    std::printf("  error:       %s\n", (firmwareStatus & VALVEMASTER_STATUS_ERROR) ? "yes" : "no");
    std::printf("  field_power: %s\n", (firmwareStatus & VALVEMASTER_STATUS_POWER_ON) ? "on" : "off");
}

struct GenericCompletionState
{
    std::mutex mtx;
    std::condition_variable cv;
    bool called = false;
    valvemaster_op_status_t opStatus = VALVEMASTER_OP_FAILED;
    uint8_t firmwareResult = 0;
    uint8_t firmwareStatus = 0;
};

struct ValveStatusCompletionState
{
    std::mutex mtx;
    std::condition_variable cv;
    bool called = false;
    valvemaster_op_status_t opStatus = VALVEMASTER_OP_FAILED;
    uint8_t firmwareResult = 0;
    uint8_t firmwareStatus = 0;
    uint8_t node = 0;
    uint8_t channel = 0;
    bool open = false;
};

struct NodeVersionCompletionState
{
    std::mutex mtx;
    std::condition_variable cv;
    bool called = false;
    valvemaster_op_status_t opStatus = VALVEMASTER_OP_FAILED;
    uint8_t firmwareResult = 0;
    uint8_t firmwareStatus = 0;
    uint8_t node = 0;
    uint8_t versionHi = 0;
    uint8_t versionLo = 0;
};

static bool waitForGenericCompletion(VALVEMASTER&,
                                     GenericCompletionState& state,
                                     uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> lock(state.mtx);

    if (!state.cv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [&state] { return state.called; })) {
        std::fprintf(stderr, "timed out waiting for VALVEMASTER command completion\n");
        return false;
    }

    return true;
}


static bool waitForValveStatusCompletion(VALVEMASTER&,
                                         ValveStatusCompletionState& state,
                                         uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> lock(state.mtx);

    if (!state.cv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [&state] { return state.called; })) {
        std::fprintf(stderr, "timed out waiting for VALVEMASTER valve-status completion\n");
        return false;
    }

    return true;
}


static bool waitForNodeVersionCompletion(VALVEMASTER&,
                                         NodeVersionCompletionState& state,
                                         uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> lock(state.mtx);

    if (!state.cv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [&state] { return state.called; })) {
        std::fprintf(stderr, "timed out waiting for VALVEMASTER node-version completion\n");
        return false;
    }

    return true;
}

static int runGenericCommand(VALVEMASTER& device,
                             const char* label,
                             uint32_t timeoutMs,
                             std::function<bool(VALVEMASTERCompletion)> submit)
{
    GenericCompletionState state;

    if (!submit([&state](valvemaster_op_status_t opStatus,
                         uint8_t firmwareResult,
                         uint8_t firmwareStatus) {
            {
                std::lock_guard<std::mutex> lock(state.mtx);

                state.called = true;
                state.opStatus = opStatus;
                state.firmwareResult = firmwareResult;
                state.firmwareStatus = firmwareStatus;
            }

            state.cv.notify_all();
        })) {
        std::fprintf(stderr, "%s failed: could not queue command\n", label);
        return EXIT_FAILURE;
    }

    if (!waitForGenericCompletion(device, state, timeoutMs)) {
        return EXIT_FAILURE;
    }

    if (state.opStatus != VALVEMASTER_OP_OK ||
        state.firmwareResult != VALVEMASTER_RESULT_OK) {
        std::fprintf(stderr, "%s failed\n", label);

        printCommandResult(state.opStatus,
                           state.firmwareResult,
                           state.firmwareStatus);

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


static int runValveStatusCommand(VALVEMASTER& device,
                                 uint8_t node,
                                 uint8_t channel)
{
    ValveStatusCompletionState state;

    if (!device.getValve(node,
                         channel,
                         [&state](valvemaster_op_status_t opStatus,
                                  uint8_t firmwareResult,
                                  uint8_t firmwareStatus,
                                  uint8_t replyNode,
                                  uint8_t replyChannel,
                                  bool open) {
            {
                std::lock_guard<std::mutex> lock(state.mtx);

                state.called = true;
                state.opStatus = opStatus;
                state.firmwareResult = firmwareResult;
                state.firmwareStatus = firmwareStatus;
                state.node = replyNode;
                state.channel = replyChannel;
                state.open = open;
            }

            state.cv.notify_all();
        })) {
        std::fprintf(stderr,
                     "channel status node %u channel %u failed: could not queue command\n",
                     node,
                     channel);
        return EXIT_FAILURE;
    }

    if (!waitForValveStatusCompletion(device,
                                      state,
                                      VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS)) {
        return EXIT_FAILURE;
    }

    if (state.opStatus != VALVEMASTER_OP_OK ||
        state.firmwareResult != VALVEMASTER_RESULT_OK) {
        std::fprintf(stderr,
                     "channel status node %u channel %u failed\n",
                     node,
                     channel);

        printCommandResult(state.opStatus,
                           state.firmwareResult,
                           state.firmwareStatus);

        return EXIT_FAILURE;
    }

    std::printf("node %u channel %u: %s\n",
                state.node,
                state.channel,
                state.open ? "OPEN" : "CLOSED");

    return EXIT_SUCCESS;
}

static int runNodeVersionCommand(VALVEMASTER& device,
                                 uint8_t node)
{
    NodeVersionCompletionState state;

    if (!device.getNodeVersion(node,
                               [&state](valvemaster_op_status_t opStatus,
                                        uint8_t firmwareResult,
                                        uint8_t firmwareStatus,
                                        uint8_t replyNode,
                                        uint8_t versionHi,
                                        uint8_t versionLo) {
            {
                std::lock_guard<std::mutex> lock(state.mtx);

                state.called = true;
                state.opStatus = opStatus;
                state.firmwareResult = firmwareResult;
                state.firmwareStatus = firmwareStatus;
                state.node = replyNode;
                state.versionHi = versionHi;
                state.versionLo = versionLo;
            }

            state.cv.notify_all();
        })) {
        std::fprintf(stderr,
                     "version node %u failed: could not queue command\n",
                     node);
        return EXIT_FAILURE;
    }

    if (!waitForNodeVersionCompletion(device,
                                      state,
                                      VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS)) {
        return EXIT_FAILURE;
    }

    if (state.opStatus != VALVEMASTER_OP_OK ||
        state.firmwareResult != VALVEMASTER_RESULT_OK) {
        std::fprintf(stderr,
                     "version node %u failed\n",
                     node);

        printCommandResult(state.opStatus,
                           state.firmwareResult,
                           state.firmwareStatus);

        return EXIT_FAILURE;
    }

    std::printf("node %u version: %u.%02u\n",
                state.node,
                state.versionHi,
                state.versionLo);

    return EXIT_SUCCESS;
}

static int runOpenCloseCommand(VALVEMASTER& device,
                               uint8_t node,
                               uint8_t channel,
                               bool open)
{
    char label[96];

    std::snprintf(label,
                  sizeof(label),
                  "set node %u channel %u %s",
                  node,
                  channel,
                  open ? "on" : "off");

    return runGenericCommand(device,
                             label,
                             VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                             [&device, node, channel, open](VALVEMASTERCompletion completion) {
                                 return device.setValve(node,
                                                        channel,
                                                        open,
                                                        completion);
                             });
}

static int runMoveCommand(VALVEMASTER& device,
                          uint8_t oldNode,
                          uint8_t newNode)
{
    int result = EXIT_SUCCESS;
    char label[96];

    std::snprintf(label,
                  sizeof(label),
                  "config node %u",
                  oldNode);

    result = runGenericCommand(device,
                               label,
                               VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                               [&device, oldNode](VALVEMASTERCompletion completion) {
                                   return device.configNode(oldNode, completion);
                               });

    if (result != EXIT_SUCCESS) {
        return result;
    }

    std::snprintf(label,
                  sizeof(label),
                  "assign node %u",
                  newNode);

    result = runGenericCommand(device,
                               label,
                               VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                               [&device, newNode](VALVEMASTERCompletion completion) {
                                   return device.assignNode(newNode, completion);
                               });

    if (result != EXIT_SUCCESS) {
        return result;
    }

    std::printf("move: node %u -> %u OK\n",
                oldNode,
                newNode);

    /*
     * The firmware node map can still contain the old node immediately after
     * ASSIGN. Refresh discovery before printing confirmation so the displayed
     * map reflects the new address.
     */
    result = runGenericCommand(device,
                               "who",
                               VALVEMASTER_TOOL_WHO_TIMEOUT_MS,
                               [&device](VALVEMASTERCompletion completion) {
                                   return device.who(completion);
                               });

    if (result != EXIT_SUCCESS) {
        std::fprintf(stderr,
                     "move: node %u -> %u succeeded, but node-map refresh failed\n",
                     oldNode,
                     newNode);
        return result;
    }

    printMap(device);

    return EXIT_SUCCESS;
}

static uint8_t getAddress(PToolContext& ctx)
{
    if (ctx.hasAddressOverride) {
        return static_cast<uint8_t>(ctx.addressOverride);
    }

    return VALVEMASTER_DEFAULT_ADDR;
}

} // namespace


const char* VALVEMASTERTool::name() const
{
    return "valve";
}


const char* VALVEMASTERTool::description() const
{
    return "VALVEMASTER I2C-to-RS485 irrigation valve controller";
}


bool VALVEMASTERTool::hasDefaultAddress() const
{
    return true;
}


uint32_t VALVEMASTERTool::defaultAddress() const
{
    return VALVEMASTER_DEFAULT_ADDR;
}


void VALVEMASTERTool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "VALVEMASTER valve module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool valve <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x09\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                                      Show VALVEMASTER help\n";
    out << "  status                                    Read status/result/power registers\n";
    out << "  diag                                      Read version, status, reply, and node map\n";
    out << "  reply                                     Read last reply registers\n";
    out << "  power on                                  Turn field power on\n";
    out << "  power off                                 Turn field power off\n";
    out << "  who                                       Run node discovery\n";
    out << "  map                                       Read current node map\n";
    out << "  nodemap                                   Same as map\n";
    out << "  ping <node 1-254>                         Ping node\n";
    out << "  set <node 1-254> <channel 1-16> <on|off>  Set valve channel\n";
    out << "  closeall                                  Close all valve channels\n";
    out << "  all-off                                   Same as closeall\n";
    out << "  channel <node 1-254> <channel 1-16> status Read valve channel status\n";
    out << "  version master                            Read VALVEMASTER firmware version\n";
    out << "  version <node 1-254>                      Read node firmware version\n";
    out << "  identify <node 1-254>                     Identify node\n";
    out << "  cancel                                    Cancel node identify/config behavior\n";
    out << "\n";
    out << "Provisioning:\n";
    out << "  config [node 0-254]                       Put node into config mode\n";
    out << "  assign <new-node 1-254>                   Assign node address\n";
    out << "  move <old-node 1-254> <new-node 1-254>    Config old node, then assign new node\n";
    out << "\n";
    out << "Fault/testing:\n";
    out << "  fault set                                 Set VALVEMASTER firmware error latch\n";
    out << "  fault clear                               Clear VALVEMASTER firmware error latch\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool valve status\n";
    out << "  piottool valve version master\n";
    out << "  piottool valve power on\n";
    out << "  piottool valve ping 1\n";
    out << "  piottool valve closeall\n";
    out << "  piottool valve power off\n";
    out << "  piottool --addr 0x09 valve status\n";
    out << "\n";
    out << "Notes:\n";
    out << "  Commands that talk to RS-485 valve nodes require field power on first.\n";
    out << "  This module uses the existing lower VALVEMASTER driver class.\n";
    out << "  It does not read /dev/i2c directly from the tool module.\n";
    out << "\n";
}


void VALVEMASTERTool::printCommandHelp(const std::string& command,
                                       std::ostream& out) const
{
    if(command == "status") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve status\n";
        out << "\n";
        out << "Reads STATUS, RESULT, and POWER_STATE registers.\n";
        out << "\n";
        return;
    }

    if(command == "diag") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve diag\n";
        out << "\n";
        out << "Reads master version, status/result/power, last reply, and node map.\n";
        out << "\n";
        return;
    }

    if(command == "reply") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve reply\n";
        out << "\n";
        out << "Reads the last firmware reply register block.\n";
        out << "\n";
        return;
    }

    if(command == "power") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve power on\n";
        out << "  piottool valve power off\n";
        out << "\n";
        out << "Turns field power on or off using the lower VALVEMASTER driver pacing.\n";
        out << "\n";
        return;
    }

    if(command == "who") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve who\n";
        out << "\n";
        out << "Runs node discovery. Field power must be on first.\n";
        out << "\n";
        return;
    }

    if(command == "map" || command == "nodemap") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve map\n";
        out << "  piottool valve nodemap\n";
        out << "\n";
        out << "Reads the firmware node map.\n";
        out << "\n";
        return;
    }

    if(command == "ping") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve ping <node 1-254>\n";
        out << "\n";
        out << "Pings one valve node. Field power must be on first.\n";
        out << "\n";
        return;
    }

    if(command == "set") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve set <node 1-254> <channel 1-16> <on|off>\n";
        out << "\n";
        out << "Sets one valve channel open/on or closed/off.\n";
        out << "\n";
        return;
    }

    if(command == "closeall" || command == "all-off") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve closeall\n";
        out << "  piottool valve all-off\n";
        out << "\n";
        out << "Broadcasts close-all. Field power must be on first.\n";
        out << "\n";
        return;
    }

    if(command == "channel") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve channel <node 1-254> <channel 1-16> status\n";
        out << "\n";
        out << "Reads live valve channel status from one node.\n";
        out << "\n";
        return;
    }

    if(command == "version") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve version master\n";
        out << "  piottool valve version <node 1-254>\n";
        out << "\n";
        out << "Reads VALVEMASTER firmware version or valve node firmware version.\n";
        out << "\n";
        return;
    }

    if(command == "identify") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve identify <node 1-254>\n";
        out << "\n";
        out << "Starts node identify behavior. Use cancel to stop if needed.\n";
        out << "\n";
        return;
    }

    if(command == "cancel") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve cancel\n";
        out << "\n";
        out << "Cancels node-side identify/config behavior.\n";
        out << "\n";
        return;
    }

    if(command == "config") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve config [node 0-254]\n";
        out << "\n";
        out << "Puts a node into config mode. Node 0 means unassigned/config address.\n";
        out << "\n";
        return;
    }

    if(command == "assign") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve assign <new-node 1-254>\n";
        out << "\n";
        out << "Assigns a new node address to the node currently in config mode.\n";
        out << "\n";
        return;
    }

    if(command == "move") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve move <old-node 1-254> <new-node 1-254>\n";
        out << "\n";
        out << "Runs config old-node, then assign new-node.\n";
        out << "\n";
        return;
    }

    if(command == "fault") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool valve fault set\n";
        out << "  piottool valve fault clear\n";
        out << "\n";
        out << "Sets or clears the VALVEMASTER firmware error latch.\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for valve command: " << command << "\n";
}

int VALVEMASTERTool::run(const std::string& command,
                         const std::vector<std::string>& args,
                         PToolContext& ctx)
{
    const uint8_t addr = getAddress(ctx);
    int error = 0;
    int exitCode = EXIT_SUCCESS;

    if(command == "help") {
        if(args.empty()) {
            printHelp(std::cout);
        }
        else {
            printCommandHelp(args[0], std::cout);
        }

        return EXIT_SUCCESS;
    }

    VALVEMASTER device;

    if(!device.begin(addr, error)) {
        std::cerr << "valve: begin failed at bus "
                  << ctx.bus
                  << " address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(addr)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "valve @ bus "
              << ctx.bus
              << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(device.getDevAddr())
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    if(command == "status") {
        exitCode = printStatus(device) ? EXIT_SUCCESS : EXIT_FAILURE;
        device.stop();
        return exitCode;
    }

    if(command == "reply") {
        exitCode = printReply(device) ? EXIT_SUCCESS : EXIT_FAILURE;
        device.stop();
        return exitCode;
    }

    if(command == "map" || command == "nodemap") {
        exitCode = printMap(device) ? EXIT_SUCCESS : EXIT_FAILURE;
        device.stop();
        return exitCode;
    }

    if(command == "diag") {
        bool ok = true;

        ok = printVersion(device) && ok;
        ok = printStatus(device) && ok;
        ok = printReply(device) && ok;
        ok = printMap(device) && ok;

        device.stop();
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if(command == "power") {
        if(args.size() != 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[0] == "on") {
            exitCode =
                runGenericCommand(device,
                                  "power on",
                                  VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                                  [&device](VALVEMASTERCompletion completion) {
                                      return device.powerOn(completion);
                                  });

            if(exitCode == EXIT_SUCCESS) {
                std::cout << "power: on\n";
            }

            device.stop();
            return exitCode;
        }

        if(args[0] == "off") {
            exitCode =
                runGenericCommand(device,
                                  "power off",
                                  VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                                  [&device](VALVEMASTERCompletion completion) {
                                      return device.powerOff(completion);
                                  });

            if(exitCode == EXIT_SUCCESS) {
                std::cout << "power: off\n";
            }

            device.stop();
            return exitCode;
        }

        printCommandHelp(command, std::cerr);
        device.stop();
        return EXIT_FAILURE;
    }

    if(command == "who") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode =
            runGenericCommand(device,
                              "who",
                              VALVEMASTER_TOOL_WHO_TIMEOUT_MS,
                              [&device](VALVEMASTERCompletion completion) {
                                  return device.who(completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            printMap(device);
        }

        device.stop();
        return exitCode;
    }

    if(command == "ping") {
        uint8_t node = 0;

        if(args.size() != 1 || !parseNode(args[0], node)) {
            std::cerr << "valve: bad or missing node\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        char label[64];

        std::snprintf(label,
                      sizeof(label),
                      "ping node %u",
                      node);

        exitCode =
            runGenericCommand(device,
                              label,
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device, node](VALVEMASTERCompletion completion) {
                                  return device.pingNode(node, completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "ping: node "
                      << static_cast<unsigned>(node)
                      << " OK\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "set") {
        uint8_t node = 0;
        uint8_t channel = 0;
        bool open = false;

        if(args.size() != 3 ||
           !parseNode(args[0], node) ||
           !parseChannel(args[1], channel)) {
            std::cerr << "valve: bad set arguments\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[2] == "on" || args[2] == "open") {
            open = true;
        }
        else if(args[2] == "off" || args[2] == "close" || args[2] == "closed") {
            open = false;
        }
        else {
            std::cerr << "valve: bad state: "
                      << args[2]
                      << ", expected on or off\n";
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = runOpenCloseCommand(device, node, channel, open);

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "set: node "
                      << static_cast<unsigned>(node)
                      << " channel "
                      << static_cast<unsigned>(channel)
                      << " "
                      << (open ? "on" : "off")
                      << "\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "closeall" || command == "all-off") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode =
            runGenericCommand(device,
                              "closeall",
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device](VALVEMASTERCompletion completion) {
                                  return device.allOff(completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "closeall: OK\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "channel") {
        uint8_t node = 0;
        uint8_t channel = 0;

        if(args.size() != 3 ||
           args[2] != "status" ||
           !parseNode(args[0], node) ||
           !parseChannel(args[1], channel)) {
            std::cerr << "valve: bad channel status arguments\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = runValveStatusCommand(device, node, channel);
        device.stop();
        return exitCode;
    }

    if(command == "version") {
        if(args.size() != 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[0] == "master") {
            exitCode = printVersion(device) ? EXIT_SUCCESS : EXIT_FAILURE;
            device.stop();
            return exitCode;
        }

        uint8_t node = 0;

        if(!parseNode(args[0], node)) {
            std::cerr << "valve: bad node: "
                      << args[0]
                      << "\n";
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = runNodeVersionCommand(device, node);

        device.stop();
        return exitCode;
    }

    if(command == "identify") {
        uint8_t node = 0;

        if(args.size() != 1 || !parseNode(args[0], node)) {
            std::cerr << "valve: bad or missing node\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        char label[64];

        std::snprintf(label,
                      sizeof(label),
                      "identify node %u",
                      node);

        exitCode =
            runGenericCommand(device,
                              label,
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device, node](VALVEMASTERCompletion completion) {
                                  return device.identifyNode(node, completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "identify: node "
                      << static_cast<unsigned>(node)
                      << " OK\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "cancel") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode =
            runGenericCommand(device,
                              "cancel",
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device](VALVEMASTERCompletion completion) {
                                  return device.cancel(completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "cancel: OK\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "config") {
        uint8_t node = 0;

        if(args.size() > 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args.size() == 1 && !parseConfigNode(args[0], node)) {
            std::cerr << "valve: bad config node: "
                      << args[0]
                      << "\n";
            device.stop();
            return EXIT_FAILURE;
        }

        char label[64];

        std::snprintf(label,
                      sizeof(label),
                      "config node %u",
                      node);

        exitCode =
            runGenericCommand(device,
                              label,
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device, node](VALVEMASTERCompletion completion) {
                                  return device.configNode(node, completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "config: node "
                      << static_cast<unsigned>(node)
                      << " OK\n";
        }

        device.stop();
        return exitCode;
    }

    if(command == "assign") {
        uint8_t node = 0;

        if(args.size() != 1 || !parseNode(args[0], node)) {
            std::cerr << "valve: bad or missing new node\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        char label[64];

        std::snprintf(label,
                      sizeof(label),
                      "assign node %u",
                      node);

        exitCode =
            runGenericCommand(device,
                              label,
                              VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                              [&device, node](VALVEMASTERCompletion completion) {
                                  return device.assignNode(node, completion);
                              });

        if(exitCode == EXIT_SUCCESS) {
            std::cout << "assign: node "
                      << static_cast<unsigned>(node)
                      << " OK\n";
            printMap(device);
        }

        device.stop();
        return exitCode;
    }

    if(command == "move") {
        uint8_t oldNode = 0;
        uint8_t newNode = 0;

        if(args.size() != 2 ||
           !parseNode(args[0], oldNode) ||
           !parseNode(args[1], newNode)) {
            std::cerr << "valve: bad move arguments\n";
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(oldNode == newNode) {
            std::cerr << "valve: old node and new node are the same: "
                      << static_cast<unsigned>(oldNode)
                      << "\n";
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = runMoveCommand(device, oldNode, newNode);
        device.stop();
        return exitCode;
    }

    if(command == "fault") {
        if(args.size() != 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[0] == "set") {
            exitCode =
                runGenericCommand(device,
                                  "fault set",
                                  VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                                  [&device](VALVEMASTERCompletion completion) {
                                      return device.setError(completion);
                                  });

            if(exitCode == EXIT_SUCCESS) {
                std::cout << "fault: set\n";
            }

            device.stop();
            return exitCode;
        }

        if(args[0] == "clear") {
            exitCode =
                runGenericCommand(device,
                                  "fault clear",
                                  VALVEMASTER_TOOL_NORMAL_TIMEOUT_MS,
                                  [&device](VALVEMASTERCompletion completion) {
                                      return device.clearError(completion);
                                  });

            if(exitCode == EXIT_SUCCESS) {
                std::cout << "fault: clear\n";
            }

            device.stop();
            return exitCode;
        }

        printCommandHelp(command, std::cerr);
        device.stop();
        return EXIT_FAILURE;
    }

    std::cerr << "valve: unknown command: "
              << command
              << "\n";
    std::cerr << "try: piottool valve help\n";

    device.stop();
    return EXIT_FAILURE;
}

extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new VALVEMASTERTool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
