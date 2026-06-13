#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <deque>

#include "VALVEMASTER.hpp"
#include "LogMgr.hpp"


// -----------------------------------------------------------------------------
// VALVEMASTER firmware register map
// -----------------------------------------------------------------------------

#define VALVEMASTER_REG_COMMAND        0x00 //!< Command register
#define VALVEMASTER_REG_STATUS         0x01 //!< Status register
#define VALVEMASTER_REG_ARG0           0x02 //!< Command argument 0 register
#define VALVEMASTER_REG_ARG1           0x03 //!< Command argument 1 register
#define VALVEMASTER_REG_ARG2           0x04 //!< Command argument 2 register
#define VALVEMASTER_REG_RESULT         0x05 //!< Last command result register
#define VALVEMASTER_REG_POWER_STATE    0x06 //!< Field bus power state register
#define VALVEMASTER_REG_NODE_COUNT     0x07 //!< Discovered node count register
#define VALVEMASTER_REG_REPLY_NODE     0x08 //!< Reply node address register
#define VALVEMASTER_REG_REPLY_CMD      0x09 //!< Reply command/status character register
#define VALVEMASTER_REG_REPLY_ARG0     0x0A //!< Reply argument 0 register
#define VALVEMASTER_REG_REPLY_ARG1     0x0B //!< Reply argument 1 register
#define VALVEMASTER_REG_VERSION_HI     0x10 //!< Firmware version high byte register
#define VALVEMASTER_REG_VERSION_LO     0x11 //!< Firmware version low byte register
#define VALVEMASTER_REG_NODE_MAP_BASE  0x20 //!< Base register for discovered node map


// -----------------------------------------------------------------------------
// VALVEMASTER firmware command values
// -----------------------------------------------------------------------------

#define VALVEMASTER_CMD_NONE               0x00 //!< No command
#define VALVEMASTER_CMD_POWER_ON           0x01 //!< Turn field bus power on
#define VALVEMASTER_CMD_POWER_OFF          0x02 //!< Turn field bus power off
#define VALVEMASTER_CMD_WHO                0x03 //!< Discover valve nodes on the field bus
#define VALVEMASTER_CMD_PING               0x04 //!< Ping a valve node
#define VALVEMASTER_CMD_SET_CHANNEL        0x05 //!< Set a valve channel open or closed
#define VALVEMASTER_CMD_GET_CHANNEL_STATUS 0x06 //!< Read valve channel status
#define VALVEMASTER_CMD_GET_NODE_VERSION   0x07 //!< Read valve node firmware version
#define VALVEMASTER_CMD_IDENTIFY           0x08 //!< Identify a valve node
#define VALVEMASTER_CMD_CANCEL             0x09 //!< Send cancel command to valve nodes
#define VALVEMASTER_CMD_CONFIG             0x0A //!< Put valve node into config mode
#define VALVEMASTER_CMD_ASSIGN             0x0B //!< Assign valve node address
#define VALVEMASTER_CMD_CLEAR_ERROR        0x0C //!< Clear master error status
#define VALVEMASTER_CMD_SET_ERROR          0x0D //!< Force master error status
#define VALVEMASTER_CMD_CLOSE_ALL          0x0F //!< Close all valve channels


// -----------------------------------------------------------------------------
// VALVEMASTER firmware status bits
// -----------------------------------------------------------------------------

#define VALVEMASTER_STATUS_BUSY      (1u << 0) //!< Command is currently executing
#define VALVEMASTER_STATUS_ERROR     (1u << 1) //!< Error/result fault is latched
#define VALVEMASTER_STATUS_POWER_ON  (1u << 2) //!< Field bus power is on


// -----------------------------------------------------------------------------
// VALVEMASTER firmware result values
// -----------------------------------------------------------------------------

#define VALVEMASTER_RESULT_OK                    0x00 //!< Command completed successfully
#define VALVEMASTER_RESULT_BAD_COMMAND           0x01 //!< Unknown or invalid command
#define VALVEMASTER_RESULT_BAD_NODE              0x02 //!< Invalid node address
#define VALVEMASTER_RESULT_BAD_CHANNEL           0x03 //!< Invalid channel number
#define VALVEMASTER_RESULT_NODE_NOT_FOUND        0x04 //!< Target node was not found
#define VALVEMASTER_RESULT_UNSUPPORTED_CHANNEL   0x05 //!< Target node does not support requested channel
#define VALVEMASTER_RESULT_CONFIG_REQUIRED       0x06 //!< Node configuration is required
#define VALVEMASTER_RESULT_ADDRESS_IN_USE        0x07 //!< Requested node address is already in use
#define VALVEMASTER_RESULT_BUSY                  0x08 //!< Master already has a pending command
#define VALVEMASTER_RESULT_RS485_TIMEOUT         0x09 //!< RS-485 reply timed out
#define VALVEMASTER_RESULT_RS485_BAD_CHECKSUM    0x0A //!< RS-485 reply checksum failed
#define VALVEMASTER_RESULT_RS485_BAD_REPLY       0x0B //!< RS-485 reply was malformed or unexpected
#define VALVEMASTER_RESULT_RESERVED_0C           0x0C //!< Reserved result value
#define VALVEMASTER_RESULT_POWER_OFF             0x0E //!< Field bus power is off


// -----------------------------------------------------------------------------
// Host-side timing / retry policy
// -----------------------------------------------------------------------------

#define VALVEMASTER_POLL_DELAY_MS                200
#define VALVEMASTER_NODE_SETTLE_MS               1000
#define VALVEMASTER_RS485_COMMAND_GAP_MS         500
#define VALVEMASTER_COMMAND_RETRY_COUNT          3
#define VALVEMASTER_COMMAND_RETRY_DELAY_MS       150
#define VALVEMASTER_COMMAND_BUSY_TIMEOUT_MS      5000
#define VALVEMASTER_WHO_BUSY_TIMEOUT_MS          30000
#define VALVEMASTER_CLOSE_ALL_BUSY_TIMEOUT_MS    10000
#define VALVEMASTER_NODE_MAP_MAX                 16

// -----------------------------------------------------------------------------
// Local helpers
// -----------------------------------------------------------------------------

static bool VALVEMASTER_commandUsesRS485(uint8_t command)
{
    switch (command) {
        case VALVEMASTER_CMD_WHO:
        case VALVEMASTER_CMD_PING:
        case VALVEMASTER_CMD_SET_CHANNEL:
        case VALVEMASTER_CMD_GET_CHANNEL_STATUS:
        case VALVEMASTER_CMD_GET_NODE_VERSION:
        case VALVEMASTER_CMD_IDENTIFY:
        case VALVEMASTER_CMD_CANCEL:
        case VALVEMASTER_CMD_CONFIG:
        case VALVEMASTER_CMD_ASSIGN:
        case VALVEMASTER_CMD_CLOSE_ALL:
            return true;

        default:
            return false;
    }
}

static uint32_t VALVEMASTER_commandBusyTimeoutMs(uint8_t command)
{
    switch (command) {
        case VALVEMASTER_CMD_WHO:
            return VALVEMASTER_WHO_BUSY_TIMEOUT_MS;

        case VALVEMASTER_CMD_CLOSE_ALL:
            return VALVEMASTER_CLOSE_ALL_BUSY_TIMEOUT_MS;

        default:
            return VALVEMASTER_COMMAND_BUSY_TIMEOUT_MS;
    }
}

static bool VALVEMASTER_parseValveState(uint8_t value, bool &open)
{
    switch (value) {
        case 'O':
        case 'o':
        case '1':
        case 1:
            open = true;
            return true;

        case 'C':
        case 'c':
        case '0':
        case 0:
            open = false;
            return true;

        default:
            return false;
    }
}


// -----------------------------------------------------------------------------
// Lifecycle / setup
// -----------------------------------------------------------------------------

VALVEMASTER::VALVEMASTER()
{
    _isSetup = false;

    _running = false;
    _stateChanged = false;
    _commandRunning = false;

    _lastResult = VALVEMASTER_RESULT_OK;
    _lastStatus = 0;
    _lastCommandSuccess = false;
    _lastCommandComplete = false;
}


VALVEMASTER::~VALVEMASTER()
{
    stop();
}


bool VALVEMASTER::begin(uint8_t deviceAddress)
{
    int error = 0;

    return begin(deviceAddress, error);
}


bool VALVEMASTER::begin(uint8_t deviceAddress, int &error)
{
    bool success = false;
    uint8_t status = 0;

    _isSetup = _i2cPort.begin(deviceAddress, error);

    LOGT_DEBUG("VALVEMASTER(%02x) begin: %s",
               deviceAddress, _isSetup ? "OK" : "FAIL");

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) begin failed: %s",
                   deviceAddress, strerror(error));
        return false;
    }

    success = _i2cPort.readByte(VALVEMASTER_REG_STATUS, status);

    if (!success) {
        LOGT_ERROR("VALVEMASTER(%02X) status read failed during begin: %s",
                   deviceAddress, strerror(errno));

        _i2cPort.stop();
        _isSetup = false;
        return false;
    }

    LOGT_DEBUG("VALVEMASTER(%02x) status: 0x%02X",
               deviceAddress, status);

    LOGT_DEBUG("VALVEMASTER(%02x) starting action thread",
               deviceAddress);

    _running = true;
    _stateChanged = false;
    _thread = std::thread(&VALVEMASTER::actionThread, this);

    LOGT_DEBUG("VALVEMASTER(%02x) action thread started",
               deviceAddress);

    return true;
}


void VALVEMASTER::stop()
{
    std::deque<queuedCommand_t> stoppedCommands;

    if (_isSetup) {
        LOGT_DEBUG("VALVEMASTER(%02x) stop", _i2cPort.getDevAddr());
    }

    {
        std::lock_guard<std::mutex> lock(_mtx);

        _running = false;
        _stateChanged = true;

        stoppedCommands.swap(_commandQueue);
    }

    _cv.notify_one();

    for (auto &item : stoppedCommands) {
        if (item.completion) {
            item.completion(VALVEMASTER_OP_STOPPED, 0, 0);
        }

        if (item.valveStatusCompletion) {
            item.valveStatusCompletion(VALVEMASTER_OP_STOPPED,
                                       0,
                                       0,
                                       item.arg0,
                                       item.arg1,
                                       false);
        }

        if (item.nodeVersionCompletion) {
            item.nodeVersionCompletion(VALVEMASTER_OP_STOPPED,
                                       0,
                                       0,
                                       item.arg0,
                                       0,
                                       0);
        }
    }

    if (_thread.joinable()) {
        LOGT_DEBUG("VALVEMASTER(%02x) joining action thread",
                   _i2cPort.getDevAddr());

        _thread.join();

        LOGT_DEBUG("VALVEMASTER(%02x) action thread joined",
                   _i2cPort.getDevAddr());
    }

    if (_isSetup) {
        _isSetup = false;
        _i2cPort.stop();
    }
}


bool VALVEMASTER::preflight()
{
    return isOpen();
}


uint8_t VALVEMASTER::getDevAddr()
{
    return _i2cPort.getDevAddr();
}


bool VALVEMASTER::isOpen()
{
    return _isSetup;
}


// -----------------------------------------------------------------------------
// Synchronous/direct register API
// -----------------------------------------------------------------------------

bool VALVEMASTER::getVersion(std::string& version)
{
    bool success = false;
    uint8_t versionHi = 0;
    uint8_t versionLo = 0;
    char str[16] = {0};

    version.clear();

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) getVersion failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    LOGT_DEBUG("VALVEMASTER(%02x) getVersion", _i2cPort.getDevAddr());

    success = _i2cPort.readByte(VALVEMASTER_REG_VERSION_HI, versionHi);

    if (!success) {
        LOGT_ERROR("VALVEMASTER(%02X) getVersion failed reading VERSION_HI: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    success = _i2cPort.readByte(VALVEMASTER_REG_VERSION_LO, versionLo);

    if (!success) {
        LOGT_ERROR("VALVEMASTER(%02X) getVersion failed reading VERSION_LO: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    snprintf(str, sizeof(str), "%u.%02u", versionHi, versionLo);
    version = str;

    LOGT_DEBUG("VALVEMASTER(%02x) version: %s",
               _i2cPort.getDevAddr(), version.c_str());

    return true;
}


bool VALVEMASTER::readFirmwareStatus(uint8_t &status)
{
    status = 0;

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareStatus failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_STATUS, status)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareStatus failed: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    return true;
}


bool VALVEMASTER::readFirmwareResult(uint8_t &result)
{
    result = 0;

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareResult failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_RESULT, result)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareResult failed: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    return true;
}


bool VALVEMASTER::readFirmwarePowerState(uint8_t &powerState)
{
    powerState = 0;

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwarePowerState failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_POWER_STATE, powerState)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwarePowerState failed: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    return true;
}


bool VALVEMASTER::readFirmwareReply(valvemaster_reply_t &reply)
{
    reply.node = 0;
    reply.cmd = 0;
    reply.arg0 = 0;
    reply.arg1 = 0;

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareReply failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_REPLY_NODE, reply.node)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareReply failed reading REPLY_NODE: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_REPLY_CMD, reply.cmd)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareReply failed reading REPLY_CMD: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_REPLY_ARG0, reply.arg0)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareReply failed reading REPLY_ARG0: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_REPLY_ARG1, reply.arg1)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareReply failed reading REPLY_ARG1: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    return true;
}


bool VALVEMASTER::readFirmwareNodeMap(std::vector<uint8_t> &nodes)
{
    uint8_t count = 0;

    nodes.clear();

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareNodeMap failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    if (!_i2cPort.readByte(VALVEMASTER_REG_NODE_COUNT, count)) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareNodeMap failed reading NODE_COUNT: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    if (count > VALVEMASTER_NODE_MAP_MAX) {
        LOGT_ERROR("VALVEMASTER(%02X) readFirmwareNodeMap count %u exceeds max %u; clamping",
                   _i2cPort.getDevAddr(),
                   count,
                   VALVEMASTER_NODE_MAP_MAX);

        count = VALVEMASTER_NODE_MAP_MAX;
    }

    for (uint8_t i = 0; i < count; i++) {
        uint8_t node = 0;

        if (!_i2cPort.readByte(VALVEMASTER_REG_NODE_MAP_BASE + i, node)) {
            LOGT_ERROR("VALVEMASTER(%02X) readFirmwareNodeMap failed reading map index %u: %s",
                       _i2cPort.getDevAddr(),
                       i,
                       strerror(errno));
            return false;
        }

        nodes.push_back(node);
    }

    return true;
}


// -----------------------------------------------------------------------------
// Asynchronous queued command API
// -----------------------------------------------------------------------------

bool VALVEMASTER::powerOn(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_POWER_ON,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::powerOff(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_POWER_OFF,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::clearError(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_CLEAR_ERROR,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::setError(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_SET_ERROR,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}

bool VALVEMASTER::allOff(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_CLOSE_ALL,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::pingNode(uint8_t node, VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_PING,
                        node,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::who(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_WHO,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}

bool VALVEMASTER::identifyNode(uint8_t node, VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_IDENTIFY,
                        node,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}

bool VALVEMASTER::configNode(uint8_t node, VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_CONFIG,
                        node,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::assignNode(uint8_t node, VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_ASSIGN,
                        node,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}

bool VALVEMASTER::cancel(VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_CANCEL,
                        0,
                        0,
                        0,
                        completion,
                        nullptr,
                        nullptr);
}

bool VALVEMASTER::getNodeVersion(uint8_t node,
                                 VALVEMASTERNodeVersionCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_GET_NODE_VERSION,
                        node,
                        0,
                        0,
                        nullptr,
                        nullptr,
                        completion);
}


bool VALVEMASTER::setValve(uint8_t node,
                           uint8_t channel,
                           bool open,
                           VALVEMASTERCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_SET_CHANNEL,
                        node,
                        channel,
                        open ? 1 : 0,
                        completion,
                        nullptr,
                        nullptr);
}


bool VALVEMASTER::getValve(uint8_t node,
                           uint8_t channel,
                           VALVEMASTERValveStatusCompletion completion)
{
    return queueCommand(VALVEMASTER_CMD_GET_CHANNEL_STATUS,
                        node,
                        channel,
                        0,
                        nullptr,
                        completion,
                        nullptr);
}


// -----------------------------------------------------------------------------
// Queue control / synchronization helpers
// -----------------------------------------------------------------------------

bool VALVEMASTER::flushQueue()
{
    std::deque<queuedCommand_t> flushedCommands;

    LOGT_DEBUG("VALVEMASTER(%02x) flushQueue", _i2cPort.getDevAddr());

    {
        std::lock_guard<std::mutex> lock(_mtx);

        flushedCommands.swap(_commandQueue);

        if (!flushedCommands.empty() && !_commandRunning) {
            _lastCommandComplete = true;
            _lastCommandSuccess = false;
            _lastResult = VALVEMASTER_RESULT_BUSY;
            _lastStatus = 0;
        }
    }

    for (auto &item : flushedCommands) {
        if (item.completion) {
            item.completion(VALVEMASTER_OP_FLUSHED, 0, 0);
        }

        if (item.valveStatusCompletion) {
            item.valveStatusCompletion(VALVEMASTER_OP_FLUSHED,
                                       0,
                                       0,
                                       item.arg0,
                                       item.arg1,
                                       false);
        }

        if (item.nodeVersionCompletion) {
            item.nodeVersionCompletion(VALVEMASTER_OP_FLUSHED,
                                       0,
                                       0,
                                       item.arg0,
                                       0,
                                       0);
        }
    }

    if (!flushedCommands.empty()) {
        LOGT_DEBUG("VALVEMASTER(%02x) flushed %zu queued command(s)",
                   _i2cPort.getDevAddr(),
                   flushedCommands.size());
    }

    return true;
}


bool VALVEMASTER::isBusy()
{
    std::lock_guard<std::mutex> lock(_mtx);

    return _commandRunning || !_commandQueue.empty();
}


bool VALVEMASTER::waitForIdle(uint32_t timeoutMs)
{
    auto start = std::chrono::steady_clock::now();

    for (;;) {
        if (!isBusy()) {
            return true;
        }

        if (timeoutMs > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

            if (elapsed >= timeoutMs) {
                LOGT_ERROR("VALVEMASTER(%02X) waitForIdle timed out after %u ms",
                           _i2cPort.getDevAddr(),
                           timeoutMs);
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}


// -----------------------------------------------------------------------------
// Cached last-command state
// -----------------------------------------------------------------------------

bool VALVEMASTER::commandSucceeded()
{
    std::lock_guard<std::mutex> lock(_mtx);

    return _lastCommandComplete && _lastCommandSuccess;
}


bool VALVEMASTER::commandFailed()
{
    std::lock_guard<std::mutex> lock(_mtx);

    return _lastCommandComplete && !_lastCommandSuccess;
}


uint8_t VALVEMASTER::lastResult()
{
    std::lock_guard<std::mutex> lock(_mtx);

    return _lastResult;
}


uint8_t VALVEMASTER::lastStatus()
{
    std::lock_guard<std::mutex> lock(_mtx);

    return _lastStatus;
}


// -----------------------------------------------------------------------------
// Private direct register helper
// -----------------------------------------------------------------------------

bool VALVEMASTER::readStatus(uint8_t &status)
{
    bool success = false;

    status = 0;

    LOGT_DEBUG("VALVEMASTER(%02x) readStatus", _i2cPort.getDevAddr());

    success = _i2cPort.readByte(VALVEMASTER_REG_STATUS, status);

    if (!success) {
        LOGT_ERROR("VALVEMASTER(%02X) readStatus failed: %s",
                   _i2cPort.getDevAddr(), strerror(errno));
        return false;
    }

    return success;
}


// -----------------------------------------------------------------------------
// Private queue implementation
// -----------------------------------------------------------------------------

bool VALVEMASTER::queueCommand(uint8_t command,
                               uint8_t arg0,
                               uint8_t arg1,
                               uint8_t arg2,
                               VALVEMASTERCompletion completion,
                               VALVEMASTERValveStatusCompletion valveStatusCompletion,
                               VALVEMASTERNodeVersionCompletion nodeVersionCompletion)
{
    LOGT_DEBUG("VALVEMASTER(%02x) queueCommand cmd=0x%02X arg0=0x%02X arg1=0x%02X arg2=0x%02X",
               _i2cPort.getDevAddr(),
               command,
               arg0,
               arg1,
               arg2);

    if (!_isSetup) {
        LOGT_ERROR("VALVEMASTER(%02X) queueCommand failed: device is not setup",
                   _i2cPort.getDevAddr());
        return false;
    }

    queuedCommand_t item = {
        command,
        arg0,
        arg1,
        arg2,
        completion,
        valveStatusCompletion,
        nodeVersionCompletion
    };

    {
        std::lock_guard<std::mutex> lock(_mtx);

        _commandQueue.push_back(item);

        _lastCommandComplete = false;
        _lastCommandSuccess = false;
        _lastResult = VALVEMASTER_RESULT_BUSY;
        _lastStatus = 0;

        _stateChanged = true;
    }

    _cv.notify_one();

    return true;
}


bool VALVEMASTER::isRetryableResult(uint8_t result)
{
    switch (result) {
        case VALVEMASTER_RESULT_BUSY:
        case VALVEMASTER_RESULT_RS485_TIMEOUT:
        case VALVEMASTER_RESULT_RS485_BAD_CHECKSUM:
        case VALVEMASTER_RESULT_RS485_BAD_REPLY:
            return true;

        default:
            return false;
    }
}


bool VALVEMASTER::runCommandOnce(queuedCommand_t item,
                                 bool &commandOk,
                                 uint8_t &result,
                                 uint8_t &status,
                                 uint8_t &completionNode,
                                 uint8_t &completionChannel,
                                 bool &completionOpen,
                                 uint8_t &completionVersionNode,
                                 uint8_t &completionVersionHi,
                                 uint8_t &completionVersionLo)
{
    bool success = false;
    uint32_t busyTimeoutMs = VALVEMASTER_commandBusyTimeoutMs(item.command);
    auto busyStart = std::chrono::steady_clock::now();

    uint8_t replyNode = 0;
    uint8_t replyCmd = 0;
    uint8_t replyArg0 = 0;
    uint8_t replyArg1 = 0;

    commandOk = false;
    result = VALVEMASTER_RESULT_OK;
    status = 0;

    completionNode = item.arg0;
    completionChannel = item.arg1;
    completionOpen = false;

    completionVersionNode = item.arg0;
    completionVersionHi = 0;
    completionVersionLo = 0;

    /*
     * Read one command-related firmware register with local retry pacing.
     *
     * This is deliberately used only after the command byte has been written.
     * Once the command write succeeds, the VALVEMASTER may already be acting on
     * the request. A transient I2C read failure while fetching STATUS, RESULT,
     * or reply registers does not prove the command did not happen.
     *
     * Therefore:
     *
     *   - command write failure may retry the whole command
     *   - post-command read failure retries the read first
     *
     * This prevents the action thread from immediately re-sending commands such
     * as POWER_ON, CLOSE_ALL, POWER_OFF, or SET_CHANNEL just because the first
     * confirmation read missed on the isolated I2C bus.
     */
    auto readCommandRegisterWithRetries =
        [this, &item](uint8_t reg,
                      uint8_t &value,
                      const char *label) -> bool {

            for (uint8_t attempt = 1;
                 attempt <= VALVEMASTER_COMMAND_RETRY_COUNT;
                 attempt++) {

                if (_i2cPort.readByte(reg, value)) {
                    if (attempt > 1) {
                        LOGT_DEBUG("VALVEMASTER(%02X) command 0x%02X %s read succeeded on attempt %u/%u",
                                   _i2cPort.getDevAddr(),
                                   item.command,
                                   label,
                                   attempt,
                                   VALVEMASTER_COMMAND_RETRY_COUNT);
                    }

                    return true;
                }

                LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X %s read failed attempt %u/%u: %s",
                           _i2cPort.getDevAddr(),
                           item.command,
                           label,
                           attempt,
                           VALVEMASTER_COMMAND_RETRY_COUNT,
                           strerror(errno));

                if (attempt < VALVEMASTER_COMMAND_RETRY_COUNT) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(VALVEMASTER_COMMAND_RETRY_DELAY_MS));
                }
            }

            return false;
        };

    /*
     * Command submission path.
     *
     * If one of these writes fails, the command may not have reached the
     * controller. In that case it is valid for the outer command-attempt loop
     * to retry the whole command.
     */
    success = _i2cPort.writeByte(VALVEMASTER_REG_ARG0, item.arg0);

    if (success) {
        success = _i2cPort.writeByte(VALVEMASTER_REG_ARG1, item.arg1);
    }

    if (success) {
        success = _i2cPort.writeByte(VALVEMASTER_REG_ARG2, item.arg2);
    }

    if (success) {
        success = _i2cPort.writeByte(VALVEMASTER_REG_COMMAND, item.command);
    }

    if (!success) {
        LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X I2C write failed: %s",
                   _i2cPort.getDevAddr(),
                   item.command,
                   strerror(errno));

        result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        status = 0;
        commandOk = false;
        return false;
    }

    /*
     * Command was submitted.
     *
     * From this point on, do not immediately re-send the command because of a
     * missed STATUS or RESULT read. The action may already have occurred.
     * Retry confirmation reads locally first.
     */
    for (;;) {
        if (!readCommandRegisterWithRetries(VALVEMASTER_REG_STATUS,
                                            status,
                                            "status")) {
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
            status = 0;
            commandOk = false;
            return false;
        }

        if ((status & VALVEMASTER_STATUS_BUSY) == 0) {
            /*
             * The controller has reported command completion. Give the AVR/I2C
             * register interface a conservative settle window before reading
             * RESULT and, for commands that return data, reply registers.
             *
             * This delay is intentionally host-side/I2C-side. It is not an
             * RS-485 valve-node delay. The RS-485 command already completed
             * before BUSY was cleared by the controller firmware.
             */
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (!readCommandRegisterWithRetries(VALVEMASTER_REG_RESULT,
                                                result,
                                                "result")) {
                result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
                commandOk = false;
                return false;
            }

            break;
        }

        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - busyStart).count();

            if (elapsed >= busyTimeoutMs) {
                LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X timed out waiting for BUSY to clear after %u ms",
                           _i2cPort.getDevAddr(),
                           item.command,
                           busyTimeoutMs);

                result = VALVEMASTER_RESULT_BUSY;
                commandOk = false;
                return false;
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(VALVEMASTER_POLL_DELAY_MS));
    }

    commandOk = (result == VALVEMASTER_RESULT_OK);

    if (commandOk && item.command == VALVEMASTER_CMD_POWER_ON) {
        commandOk = (status & VALVEMASTER_STATUS_POWER_ON) != 0;

        if (commandOk) {
            LOGT_DEBUG("VALVEMASTER(%02x) waiting %u ms for valve nodes to settle",
                       _i2cPort.getDevAddr(),
                       VALVEMASTER_NODE_SETTLE_MS);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(VALVEMASTER_NODE_SETTLE_MS));
        } else {
            result = VALVEMASTER_RESULT_POWER_OFF;
        }
    }

    if (commandOk && item.command == VALVEMASTER_CMD_POWER_OFF) {
        commandOk = (status & VALVEMASTER_STATUS_POWER_ON) == 0;

        if (!commandOk) {
            result = VALVEMASTER_RESULT_BUSY;
        }
    }

    if (commandOk && item.command == VALVEMASTER_CMD_GET_CHANNEL_STATUS) {
        bool replySuccess = true;
        bool parsedOpen = false;

        replySuccess =
            readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_NODE,
                                           replyNode,
                                           "reply-node");

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_CMD,
                                               replyCmd,
                                               "reply-cmd");
        }

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_ARG0,
                                               replyArg0,
                                               "reply-arg0");
        }

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_ARG1,
                                               replyArg1,
                                               "reply-arg1");
        }

        if (!replySuccess) {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X reply read failed after retries",
                       _i2cPort.getDevAddr(),
                       item.command);

            commandOk = false;
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        } else if (replyCmd != 'R') {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X bad reply cmd 0x%02X",
                       _i2cPort.getDevAddr(),
                       item.command,
                       replyCmd);

            commandOk = false;
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        } else if (!VALVEMASTER_parseValveState(replyArg1, parsedOpen)) {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X bad valve state reply arg1=0x%02X",
                       _i2cPort.getDevAddr(),
                       item.command,
                       replyArg1);

            commandOk = false;
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        } else {
            completionNode = replyNode;
            completionChannel = replyArg0;
            completionOpen = parsedOpen;

            LOGT_DEBUG("VALVEMASTER(%02x) channel status reply node=0x%02X channel=0x%02X state=0x%02X open=%u",
                       _i2cPort.getDevAddr(),
                       replyNode,
                       replyArg0,
                       replyArg1,
                       completionOpen ? 1 : 0);
        }
    }

    if (commandOk && item.command == VALVEMASTER_CMD_GET_NODE_VERSION) {
        bool replySuccess = true;

        replySuccess =
            readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_NODE,
                                           replyNode,
                                           "version-reply-node");

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_CMD,
                                               replyCmd,
                                               "version-reply-cmd");
        }

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_ARG0,
                                               replyArg0,
                                               "version-reply-arg0");
        }

        if (replySuccess) {
            replySuccess =
                readCommandRegisterWithRetries(VALVEMASTER_REG_REPLY_ARG1,
                                               replyArg1,
                                               "version-reply-arg1");
        }

        if (!replySuccess) {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X version reply read failed after retries",
                       _i2cPort.getDevAddr(),
                       item.command);

            commandOk = false;
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        } else if (replyCmd != 'V') {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X bad version reply cmd 0x%02X",
                       _i2cPort.getDevAddr(),
                       item.command,
                       replyCmd);

            commandOk = false;
            result = VALVEMASTER_RESULT_RS485_BAD_REPLY;
        } else {
            completionVersionNode = replyNode;
            completionVersionHi = replyArg0;
            completionVersionLo = replyArg1;
        }
    }

    return commandOk;
}

void VALVEMASTER::actionThread()
{
    LOGT_DEBUG("VALVEMASTER(%02x) actionThread entered",
               _i2cPort.getDevAddr());

    for (;;) {
        queuedCommand_t item = {
            VALVEMASTER_CMD_NONE,
            0,
            0,
            0,
            nullptr,
            nullptr,
            nullptr
        };

        {
            std::unique_lock<std::mutex> lock(_mtx);

            _cv.wait(lock, [this] {
                return !_running || _stateChanged || !_commandQueue.empty();
            });

            _stateChanged = false;

            if (!_running && _commandQueue.empty()) {
                break;
            }

            if (!_running) {
                break;
            }

            if (_commandQueue.empty()) {
                continue;
            }

            item = _commandQueue.front();
            _commandQueue.pop_front();
            _commandRunning = true;
        }

        bool commandOk = false;
        uint8_t result = VALVEMASTER_RESULT_OK;
        uint8_t status = 0;

        uint8_t completionNode = item.arg0;
        uint8_t completionChannel = item.arg1;
        bool completionOpen = false;

        uint8_t completionVersionNode = item.arg0;
        uint8_t completionVersionHi = 0;
        uint8_t completionVersionLo = 0;

        LOGT_DEBUG("VALVEMASTER(%02x) running queued command 0x%02X",
                   _i2cPort.getDevAddr(),
                   item.command);

        for (uint8_t attempt = 1; attempt <= VALVEMASTER_COMMAND_RETRY_COUNT; attempt++) {
            bool runOk = false;

            commandOk = false;
            result = VALVEMASTER_RESULT_OK;
            status = 0;

            runOk = runCommandOnce(item,
                                   commandOk,
                                   result,
                                   status,
                                   completionNode,
                                   completionChannel,
                                   completionOpen,
                                   completionVersionNode,
                                   completionVersionHi,
                                   completionVersionLo);

            if (runOk && commandOk) {
                break;
            }

            if (!isRetryableResult(result)) {
                break;
            }

            if (attempt >= VALVEMASTER_COMMAND_RETRY_COUNT) {
                break;
            }

            LOGT_DEBUG("VALVEMASTER(%02x) command 0x%02X retry %u/%u result=0x%02X status=0x%02X",
                       _i2cPort.getDevAddr(),
                       item.command,
                       attempt + 1,
                       VALVEMASTER_COMMAND_RETRY_COUNT,
                       result,
                       status);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(VALVEMASTER_COMMAND_RETRY_DELAY_MS));
        }

        {
            std::lock_guard<std::mutex> lock(_mtx);

            _lastResult = result;
            _lastStatus = status;
            _lastCommandSuccess = commandOk;
            _lastCommandComplete = true;
            _commandRunning = false;
        }

        if (item.completion) {
            item.completion(commandOk ? VALVEMASTER_OP_OK : VALVEMASTER_OP_FAILED,
                            result,
                            status);
        }

        if (item.valveStatusCompletion) {
            item.valveStatusCompletion(commandOk ? VALVEMASTER_OP_OK : VALVEMASTER_OP_FAILED,
                                       result,
                                       status,
                                       completionNode,
                                       completionChannel,
                                       completionOpen);
        }

        if (item.nodeVersionCompletion) {
            item.nodeVersionCompletion(commandOk ? VALVEMASTER_OP_OK : VALVEMASTER_OP_FAILED,
                                       result,
                                       status,
                                       completionVersionNode,
                                       completionVersionHi,
                                       completionVersionLo);
        }

        if (commandOk) {
            LOGT_DEBUG("VALVEMASTER(%02x) command 0x%02X complete OK",
                       _i2cPort.getDevAddr(),
                       item.command);
        } else {
            LOGT_ERROR("VALVEMASTER(%02X) command 0x%02X failed result=0x%02X status=0x%02X",
                       _i2cPort.getDevAddr(),
                       item.command,
                       result,
                       status);
        }

        if (VALVEMASTER_commandUsesRS485(item.command)) {
            LOGT_DEBUG("VALVEMASTER(%02x) waiting %u ms after RS485 command",
                       _i2cPort.getDevAddr(),
                       VALVEMASTER_RS485_COMMAND_GAP_MS);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(VALVEMASTER_RS485_COMMAND_GAP_MS));
        }
    }

    LOGT_DEBUG("VALVEMASTER(%02x) actionThread exiting",
               _i2cPort.getDevAddr());
}
