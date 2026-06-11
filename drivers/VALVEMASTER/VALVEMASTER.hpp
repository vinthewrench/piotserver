//
//  VALVEMASTER.hpp
//
//  Low-level wrapper for the VALVEMASTER I2C-to-RS485 irrigation valve
//  controller.
//
//  This class talks to the VALVEMASTER firmware at I2C address 0x09.
//
//  The API is split into two layers:
//
//    1. Synchronous/direct firmware register API
//       These methods perform immediate I2C register reads. They do not queue
//       work on the action thread and they do not talk to RS-485 valve nodes.
//       They are useful for CLI diagnostics, status display, and reading
//       firmware-maintained registers such as status, result, reply, and map.
//
//    2. Asynchronous queued command API
//       These methods queue VALVEMASTER firmware commands. The worker thread
//       executes them one at a time, polls firmware BUSY state, applies retry
//       policy for transient failures, parses reply registers for selected
//       commands, and reports completion through callbacks.
//
//  This is intentionally a low-level hardware/protocol wrapper. It does not
//  maintain long-term valve state cache, policy, schedules, REST state, or UI
//  state. Those belong in the higher-level pIoTServer VALVEMASTER_Device
//  driver.
//

#ifndef VALVEMASTER_hpp
#define VALVEMASTER_hpp

#include <stdio.h>
#include <vector>
#include <tuple>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>

#include "I2C.hpp"

using namespace std;


/**
 * @brief High-level outcome of a queued VALVEMASTER operation.
 *
 * This describes what happened to the queued operation from the host wrapper's
 * point of view. The VALVEMASTER firmware result byte is reported separately
 * in completion callbacks.
 */
typedef enum {
    VALVEMASTER_OP_OK = 0,      //!< Command ran and completed successfully.
    VALVEMASTER_OP_FAILED,      //!< Command ran but failed.
    VALVEMASTER_OP_FLUSHED,     //!< Command was flushed before running.
    VALVEMASTER_OP_STOPPED      //!< Command was dropped because device stopped.
} valvemaster_op_status_t;


/**
 * @brief Decoded VALVEMASTER reply register block.
 *
 * These fields mirror the firmware reply registers:
 *
 *   REG_REPLY_NODE
 *   REG_REPLY_CMD
 *   REG_REPLY_ARG0
 *   REG_REPLY_ARG1
 *
 * The meaning of arg0/arg1 depends on the command reply. For example:
 *
 *   cmd 'R': arg0 = channel, arg1 = 'O' or 'C'
 *   cmd 'V': arg0 = version major, arg1 = version minor
 */
typedef struct {
    uint8_t node;       //!< Reply node address.
    uint8_t cmd;        //!< Reply command/status character or raw byte.
    uint8_t arg0;       //!< Reply argument 0.
    uint8_t arg1;       //!< Reply argument 1.
} valvemaster_reply_t;


/**
 * @brief Generic asynchronous command completion callback.
 *
 * Used by queued commands that only need operation status, firmware result,
 * and firmware status.
 *
 * @param opStatus       Host-side queued operation status.
 * @param firmwareResult VALVEMASTER firmware result register value.
 * @param firmwareStatus VALVEMASTER firmware status register value.
 */
using VALVEMASTERCompletion =
    std::function<void(valvemaster_op_status_t opStatus,
                       uint8_t firmwareResult,
                       uint8_t firmwareStatus)>;


/**
 * @brief Asynchronous valve status completion callback.
 *
 * Used by getValve(). The node/channel returned are parsed from the
 * VALVEMASTER reply registers, not merely echoed from the request.
 *
 * @param opStatus       Host-side queued operation status.
 * @param firmwareResult VALVEMASTER firmware result register value.
 * @param firmwareStatus VALVEMASTER firmware status register value.
 * @param node           Reply node address.
 * @param channel        Reply valve channel.
 * @param open           True if the valve is reported open, false if closed.
 */
using VALVEMASTERValveStatusCompletion =
    std::function<void(valvemaster_op_status_t opStatus,
                       uint8_t firmwareResult,
                       uint8_t firmwareStatus,
                       uint8_t node,
                       uint8_t channel,
                       bool open)>;


/**
 * @brief Asynchronous node firmware version completion callback.
 *
 * Used by getNodeVersion(). The returned node/version bytes are parsed from
 * the VALVEMASTER reply registers.
 *
 * @param opStatus       Host-side queued operation status.
 * @param firmwareResult VALVEMASTER firmware result register value.
 * @param firmwareStatus VALVEMASTER firmware status register value.
 * @param node           Reply node address.
 * @param versionHi      Node firmware major version byte.
 * @param versionLo      Node firmware minor version byte.
 */
using VALVEMASTERNodeVersionCompletion =
    std::function<void(valvemaster_op_status_t opStatus,
                       uint8_t firmwareResult,
                       uint8_t firmwareStatus,
                       uint8_t node,
                       uint8_t versionHi,
                       uint8_t versionLo)>;


/**
 * @brief Low-level VALVEMASTER device wrapper.
 *
 * The VALVEMASTER firmware exposes a small register protocol over I2C.
 * Commands are written by setting ARG registers and then writing COMMAND.
 * The firmware reports command progress through STATUS and RESULT registers.
 *
 * Synchronous/direct register methods read firmware registers immediately.
 *
 * Asynchronous command methods enqueue work and return immediately. Completion
 * is reported later through optional callbacks.
 */
class VALVEMASTER
{

public:

    // ---------------------------------------------------------------------
    // Lifecycle / setup
    // ---------------------------------------------------------------------

    /**
     * @brief Construct an unopened VALVEMASTER wrapper.
     */
    VALVEMASTER();

    /**
     * @brief Stop the worker thread and close the I2C device.
     */
    ~VALVEMASTER();

    /**
     * @brief Open the VALVEMASTER at the default or specified I2C address.
     *
     * This opens the I2C device, reads initial status, initializes internal
     * state, and starts the worker thread used by asynchronous queued commands.
     *
     * @param deviceAddress I2C device address. Default is 0x09.
     * @return true if the device opened and the worker thread started.
     */
    bool begin(uint8_t deviceAddress = 0x09);

    /**
     * @brief Open the VALVEMASTER and return an OS error code on failure.
     *
     * This is the same as begin(deviceAddress), but returns the underlying
     * I2C open error through the error reference.
     *
     * @param deviceAddress I2C device address.
     * @param error         Receives errno-style error code from I2C open.
     * @return true if the device opened and the worker thread started.
     */
    bool begin(uint8_t deviceAddress, int &error);

    /**
     * @brief Stop the worker thread, flush pending queued commands, and close I2C.
     *
     * Pending queued commands receive VALVEMASTER_OP_STOPPED. The currently
     * running command, if any, is allowed to finish before the worker thread is
     * joined.
     */
    void stop();

    /**
     * @brief Basic readiness check.
     *
     * This currently returns isOpen().
     *
     * @return true if the wrapper is open/setup.
     */
    bool preflight();

    /**
     * @brief Return whether the I2C device is open/setup.
     *
     * @return true if begin() succeeded and stop() has not closed the device.
     */
    bool isOpen();

    /**
     * @brief Return the active I2C device address.
     *
     * @return I2C address from the underlying I2C wrapper.
     */
    uint8_t getDevAddr();


    // ---------------------------------------------------------------------
    // Synchronous/direct firmware register API
    //
    // These methods perform immediate I2C register reads. They are not queued
    // on the worker thread. They are intended for status, diagnostics, CLI
    // display, and reading firmware-maintained state.
    //
    // Naming rule:
    //
    //   readFirmwareXxx() = live firmware register read
    //   lastXxx()         = cached wrapper state from last queued command
    // ---------------------------------------------------------------------

    /**
     * @brief Read VALVEMASTER firmware version.
     *
     * This is a direct synchronous I2C register read, not a queued command.
     *
     * @param version Receives formatted version string, such as "1.06".
     * @return true if the version registers were read successfully.
     */
    bool getVersion(std::string& version);

    /**
     * @brief Read the live VALVEMASTER STATUS register.
     *
     * This reads the firmware STATUS register directly. It does not return the
     * wrapper's cached _lastStatus.
     *
     * @param status Receives firmware status byte.
     * @return true if the register was read successfully.
     */
    bool readFirmwareStatus(uint8_t &status);

    /**
     * @brief Read the live VALVEMASTER RESULT register.
     *
     * This reads the firmware RESULT register directly. It does not return the
     * wrapper's cached _lastResult.
     *
     * @param result Receives firmware result byte.
     * @return true if the register was read successfully.
     */
    bool readFirmwareResult(uint8_t &result);

    /**
     * @brief Read the VALVEMASTER POWER_STATE register.
     *
     * This is a firmware-maintained register and may be redundant with the
     * STATUS_POWER_ON bit, but it is useful for diagnostics and CLI display.
     *
     * @param powerState Receives firmware power-state byte.
     * @return true if the register was read successfully.
     */
    bool readFirmwarePowerState(uint8_t &powerState);

    /**
     * @brief Read the VALVEMASTER reply registers.
     *
     * This returns the most recent reply block maintained by the firmware.
     * It does not issue a new RS-485 command.
     *
     * @param reply Receives decoded reply register values.
     * @return true if all reply registers were read successfully.
     */
    bool readFirmwareReply(valvemaster_reply_t &reply);

    /**
     * @brief Read the VALVEMASTER firmware node map.
     *
     * This reads the firmware's node-count and node-map registers. The map
     * contents depend on how the firmware populates them, usually after WHO
     * discovery or previous node activity.
     *
     * @param nodes Receives node addresses from the firmware node map.
     * @return true if the node map was read successfully.
     */
    bool readFirmwareNodeMap(std::vector<uint8_t> &nodes);


    // ---------------------------------------------------------------------
    // Asynchronous queued command API
    //
    // These methods queue VALVEMASTER commands and return immediately. The
    // worker thread serializes execution, waits for firmware BUSY to clear,
    // retries selected transient failures, parses replies when needed, and
    // calls the optional completion callback.
    // ---------------------------------------------------------------------

    /**
     * @brief Queue command to turn field-bus power on.
     *
     * On success the firmware status should include STATUS_POWER_ON. The worker
     * also waits a small settle delay after successful power-on before moving
     * to the next queued command.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool powerOn(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to turn field-bus power off.
     *
     * On success the firmware status should no longer include STATUS_POWER_ON.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool powerOff(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to clear the VALVEMASTER firmware error latch.
     *
     * This is local to the VALVEMASTER firmware and is not an RS-485 field-bus
     * command. It is useful after expected failure tests or recoverable
     * transient bus failures.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool clearError(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to deliberately set the VALVEMASTER firmware error latch.
     *
     * This is a local VALVEMASTER diagnostic/test command. It is not an RS-485
     * field-bus command and it does not talk to valve nodes.
     *
     * Normal operation should use clearError(). This method is useful for
     * testing status reporting, UI fault display, and recovery paths.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool setError(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to close all valve channels.
     *
     * This is the low-level panic/safety close command. It requires field-bus
     * power to be on.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool allOff(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to ping a valve node.
     *
     * @param node       Valve node address.
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool pingNode(uint8_t node, VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to discover valve nodes on the field bus.
     *
     * On success, the firmware node map can be read with
     * readFirmwareNodeMap().
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool who(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to identify a valve node.
     *
     * This sends the VALVEMASTER identify command to the specified node. The
     * node firmware is expected to perform its identify behavior, usually a
     * visible blink or similar field indication.
     *
     * This is an RS-485 field-bus command and requires field-bus power to be
     * on. If the node identify behavior is persistent, call cancel() to stop it.
     *
     * @param node       Valve node address.
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool identifyNode(uint8_t node, VALVEMASTERCompletion completion = nullptr);


    /**
     * @brief Queue command to put a valve node into config mode.
     *
     * This sends the VALVEMASTER config command to the specified node. The node
     * firmware enters address/configuration mode and can then accept an address
     * assignment command.
     *
     * This is an RS-485 field-bus command and requires field-bus power to be on.
     * Config mode should be cancelled with cancel() if assignment is not going
     * to be completed.
     *
     * @param node       Current valve node address, or 0 for an unassigned node
     *                   if supported by the firmware/protocol.
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool configNode(uint8_t node, VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to assign a node address.
     *
     * This sends the VALVEMASTER assign command. The slave firmware writes
     * EEPROM on the node that is currently in config mode.
     *
     * This is an RS-485 field-bus command and requires field-bus power to be on.
     * Use with care: this changes persistent node identity.
     *
     * @param node       New valve node address to assign.
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool assignNode(uint8_t node, VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to cancel active node field behavior.
     *
     * This sends the VALVEMASTER cancel command on the field bus. It is intended
     * to stop transient node-side behavior such as identify blinking.
     *
     * This is an RS-485 field-bus command and requires field-bus power to be on.
     *
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool cancel(VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to read a valve node firmware version.
     *
     * The version is returned asynchronously through completion.
     *
     * @param node       Valve node address.
     * @param completion Optional node version completion callback.
     * @return true if the command was queued.
     */
    bool getNodeVersion(uint8_t node,
                        VALVEMASTERNodeVersionCompletion completion = nullptr);

    /**
     * @brief Queue command to set a valve channel open or closed.
     *
     * @param node       Valve node address.
     * @param channel    Valve channel number.
     * @param open       true to open, false to close.
     * @param completion Optional completion callback.
     * @return true if the command was queued.
     */
    bool setValve(uint8_t node,
                  uint8_t channel,
                  bool open,
                  VALVEMASTERCompletion completion = nullptr);

    /**
     * @brief Queue command to read live valve channel status.
     *
     * This sends an RS-485 status request through the VALVEMASTER firmware.
     * It is not a local cache read.
     *
     * @param node       Valve node address.
     * @param channel    Valve channel number.
     * @param completion Optional valve status completion callback.
     * @return true if the command was queued.
     */
    bool getValve(uint8_t node,
                  uint8_t channel,
                  VALVEMASTERValveStatusCompletion completion = nullptr);


    // ---------------------------------------------------------------------
    // Queue control / synchronization helpers
    // ---------------------------------------------------------------------

    /**
     * @brief Flush queued commands that have not started yet.
     *
     * Flushed commands receive VALVEMASTER_OP_FLUSHED. The command currently
     * running on the worker thread is not cancelled by this method.
     *
     * @return true.
     */
    bool flushQueue();

    /**
     * @brief Return whether a command is running or queued.
     *
     * @return true if the worker is running a command or the queue is not empty.
     */
    bool isBusy();

    /**
     * @brief Wait until the command queue and active command are idle.
     *
     * This waits for the wrapper command queue to drain. It does not directly
     * cancel or interrupt a worker command that is stuck in firmware BUSY; the
     * command execution path has its own busy timeout.
     *
     * @param timeoutMs Timeout in milliseconds. Zero means wait forever.
     * @return true if idle was reached, false on timeout.
     */
    bool waitForIdle(uint32_t timeoutMs);


    // ---------------------------------------------------------------------
    // Cached last-command state
    //
    // These methods return state captured by the wrapper when the most recent
    // queued command completed. They are not direct firmware register reads.
    // Use readFirmwareStatus()/readFirmwareResult() for live firmware reads.
    // ---------------------------------------------------------------------

    /**
     * @brief Return whether the most recently completed queued command succeeded.
     *
     * @return true if a queued command completed and succeeded.
     */
    bool commandSucceeded();

    /**
     * @brief Return whether the most recently completed queued command failed.
     *
     * @return true if a queued command completed and failed.
     */
    bool commandFailed();

    /**
     * @brief Return the cached result from the most recently completed queued command.
     *
     * This returns _lastResult from the wrapper, not a direct firmware register
     * read. Use readFirmwareResult() to read the firmware RESULT register.
     *
     * @return Last cached firmware result byte.
     */
    uint8_t lastResult();

    /**
     * @brief Return the cached status from the most recently completed queued command.
     *
     * This returns _lastStatus from the wrapper, not a direct firmware register
     * read. Use readFirmwareStatus() to read the firmware STATUS register.
     *
     * @return Last cached firmware status byte.
     */
    uint8_t lastStatus();

private:

    /**
     * @brief Internal queued command record.
     *
     * All public asynchronous command methods are converted into one of these
     * and pushed onto _commandQueue.
     */
    typedef struct {
        uint8_t command;        //!< VALVEMASTER command byte.
        uint8_t arg0;           //!< Command argument 0.
        uint8_t arg1;           //!< Command argument 1.
        uint8_t arg2;           //!< Command argument 2.

        VALVEMASTERCompletion completion;                       //!< Generic completion callback.
        VALVEMASTERValveStatusCompletion valveStatusCompletion; //!< Valve status completion callback.
        VALVEMASTERNodeVersionCompletion nodeVersionCompletion; //!< Node version completion callback.
    } queuedCommand_t;

    /**
     * @brief Read the VALVEMASTER status register.
     *
     * Private helper used internally by command execution. Public callers
     * should use readFirmwareStatus().
     *
     * @param status Receives status byte.
     * @return true if the register was read successfully.
     */
    bool readStatus(uint8_t &status);

    /**
     * @brief Queue a raw VALVEMASTER command.
     *
     * This is the internal common implementation used by all public
     * asynchronous command methods.
     *
     * @param command                 VALVEMASTER command byte.
     * @param arg0                    Command argument 0.
     * @param arg1                    Command argument 1.
     * @param arg2                    Command argument 2.
     * @param completion              Optional generic completion callback.
     * @param valveStatusCompletion   Optional valve status callback.
     * @param nodeVersionCompletion   Optional node version callback.
     * @return true if the command was queued.
     */
    bool queueCommand(uint8_t command,
                      uint8_t arg0 = 0,
                      uint8_t arg1 = 0,
                      uint8_t arg2 = 0,
                      VALVEMASTERCompletion completion = nullptr,
                      VALVEMASTERValveStatusCompletion valveStatusCompletion = nullptr,
                      VALVEMASTERNodeVersionCompletion nodeVersionCompletion = nullptr);

    /**
     * @brief Execute one queued command once.
     *
     * This performs a single attempt: writes command arguments, writes the
     * command register, polls status until not busy, reads result, and parses
     * reply registers for commands that return reply data.
     *
     * Retry policy is intentionally not handled here. actionThread() decides
     * whether to call this again based on result.
     *
     * @param item                  Queued command to execute.
     * @param commandOk             Receives true when firmware result and reply parsing succeeded.
     * @param result                Receives firmware result byte.
     * @param status                Receives firmware status byte.
     * @param completionNode        Receives completion node for valve status callback.
     * @param completionChannel     Receives completion channel for valve status callback.
     * @param completionOpen        Receives valve open/closed state.
     * @param completionVersionNode Receives node address for version callback.
     * @param completionVersionHi   Receives firmware major version.
     * @param completionVersionLo   Receives firmware minor version.
     * @return true if this attempt completed successfully from the wrapper point of view.
     */
    bool runCommandOnce(queuedCommand_t item,
                        bool &commandOk,
                        uint8_t &result,
                        uint8_t &status,
                        uint8_t &completionNode,
                        uint8_t &completionChannel,
                        bool &completionOpen,
                        uint8_t &completionVersionNode,
                        uint8_t &completionVersionHi,
                        uint8_t &completionVersionLo);

    /**
     * @brief Decide whether a firmware result should be retried.
     *
     * Retryable results are transient transport/busy failures. Semantic errors
     * such as bad node, bad channel, unsupported channel, config required, or
     * address in use are not retried.
     *
     * @param result Firmware result byte.
     * @return true if actionThread() should retry the command.
     */
    bool isRetryableResult(uint8_t result);

    /**
     * @brief Worker thread that serializes queued commands.
     *
     * The thread waits for queued commands, executes them one at a time,
     * performs retry handling for transient failures, updates last-command
     * state, invokes completion callbacks, and applies RS-485 command pacing.
     */
    void actionThread();

    std::thread             _thread;          //!< Worker thread for queued command execution.
    bool                    _running;         //!< Worker thread run flag.

    std::condition_variable _cv;              //!< Queue/worker condition variable.
    std::mutex              _mtx;             //!< Protects queue and last-command state.
    bool                    _stateChanged;    //!< Signals queue/state change to worker.

    std::deque<queuedCommand_t> _commandQueue;   //!< Pending command queue.
    bool                       _commandRunning;  //!< True while worker is executing a command.

    uint8_t                 _lastResult;          //!< Last completed firmware result byte.
    uint8_t                 _lastStatus;          //!< Last completed firmware status byte.
    bool                    _lastCommandSuccess;  //!< Last completed command success flag.
    bool                    _lastCommandComplete; //!< True once at least one queued command completed.

    I2C                     _i2cPort; //!< Underlying I2C device wrapper.
    bool                    _isSetup; //!< True after successful begin(), false after stop().
};

#endif /* VALVEMASTER_hpp */
