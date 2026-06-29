//
//  POWERCONTROL.cpp
//  POWERCONTROL
//
//  Created by vinnie on 5/30/26.
//

#include "POWERCONTROL.hpp"

#include <errno.h>
#include <string.h>

#include "LogMgr.hpp"

/*
 * AVR POWERCONTROL one-byte protocol
 *
 * Normal production state read:
 *
 *   Bare one-byte read from I2C address 0x08.
 *
 * Command-line equivalent:
 *
 *   i2ctransfer -y 1 r1@0x08
 *
 * The normal read does not write a register pointer first.
 * The normal read does not use a repeated-start transaction.
 * The normal read is stateless and idempotent.
 *
 * Returned status byte:
 *
 *   bit 1 / 0x02 = red LED logical state, 1 = ON
 *   bit 2 / 0x04 = green LED logical state, 1 = ON
 *   bit 3 / 0x08 = logical AC_OK, 1 = AC OK
 *   bits 4-6 / 0x70 = stored wake preset
 *
 * Wake preset encoding:
 *
 *   0x00 = wake timer clear / disabled
 *   0x10 = wake after 1 minute
 *   0x20 = wake after 5 minutes
 *   0x30 = wake after 15 minutes
 *   0x40 = wake after 60 minutes
 *   0x50 = wake after 8 hours
 *   0x60 = wake after 24 hours
 *
 * Important safety rule:
 *
 *   The POWERCONTROL driver must not issue shutdown, cancel, LED,
 *   relay, or wake-timer writes during begin() or normal polling.
 *
 * One-byte write commands exist on the AVR firmware for manual/admin use,
 * but this low-level production read path is read-only.
 *
 * Old protocol warning:
 *
 *   The previous driver used register-pointer reads such as:
 *
 *     i2ctransfer -y 1 w1@0x08 0xF1 r1
 *
 *   That old register-pointer / repeated-start path is not used by the
 *   current AVR protocol and must not be used for normal polling.
 */


/* =========================================================
 * STATUS BYTE DECODING
 * ========================================================= */

static constexpr uint8_t STATUS_BIT_AC_OK     = 0x08;
static constexpr uint8_t STATUS_WAKE_MASK     = 0x70;

static constexpr uint8_t STATUS_WAKE_CLEAR    = 0x00;
static constexpr uint8_t STATUS_WAKE_1_MIN    = 0x10;
static constexpr uint8_t STATUS_WAKE_5_MIN    = 0x20;
static constexpr uint8_t STATUS_WAKE_15_MIN   = 0x30;
static constexpr uint8_t STATUS_WAKE_60_MIN   = 0x40;
static constexpr uint8_t STATUS_WAKE_8_HOURS  = 0x50;
static constexpr uint8_t STATUS_WAKE_24_HOURS = 0x60;


/* =========================================================
 * LOCAL HELPERS
 * ========================================================= */

static uint16_t wakeMinutesFromStatus(uint8_t status)
{
    switch(status & STATUS_WAKE_MASK) {
        case STATUS_WAKE_CLEAR:
            return 0;

        case STATUS_WAKE_1_MIN:
            return 1;

        case STATUS_WAKE_5_MIN:
            return 5;

        case STATUS_WAKE_15_MIN:
            return 15;

        case STATUS_WAKE_60_MIN:
            return 60;

        case STATUS_WAKE_8_HOURS:
            return 8 * 60;

        case STATUS_WAKE_24_HOURS:
            return 24 * 60;

        default:
            return 0;
    }
}


/* =========================================================
 * POWERCONTROL
 * ========================================================= */

POWERCONTROL::POWERCONTROL()
{
    _isSetup = false;
}

POWERCONTROL::~POWERCONTROL()
{
    stop();
}

bool POWERCONTROL::begin(uint8_t deviceAddress)
{
    int error = 0;
    return begin(deviceAddress, error);
}

bool POWERCONTROL::begin(uint8_t deviceAddress, int &error)
{
    if(!_i2cPort.begin(deviceAddress, error)) {
        return false;
    }

    _isSetup = true;
    return true;
}

void POWERCONTROL::stop()
{
    if(_isSetup) {
        _isSetup = false;
        _i2cPort.stop();
    }
}

bool POWERCONTROL::isOpen()
{
    return _i2cPort.isAvailable() && _isSetup;
}

uint8_t POWERCONTROL::getDevAddr()
{
    return _i2cPort.getDevAddr();
}

bool POWERCONTROL::readStatusByte(uint8_t &value)
{
    uint8_t data[1] = { 0 };

    if(!_i2cPort.isAvailable()) {
        LOGT_ERROR("POWERCONTROL(%02X) bare status read failed: I2C port not available",
                   _i2cPort.getDevAddr());
        return false;
    }

    /*
     * Production POWERCONTROL state read.
     *
     * This is a bare one-byte I2C read from the AVR slave address.
     * It intentionally does not write a register pointer first.
     *
     * Expected command-line equivalent:
     *
     *   i2ctransfer -y 1 r1@0x08
     *
     * Expected AVR behavior:
     *
     *   Any bare read returns the current cached status byte.
     *
     * The read must be safe to repeat forever. It must not clear flags,
     * advance a register pointer, change relay state, change LED state,
     * change wake-timer state, or depend on a previous write.
     */
    if(!_i2cPort.stdReadBytes(sizeof(data), data)) {
        LOGT_ERROR("POWERCONTROL(%02X) bare status read failed: %s",
                   _i2cPort.getDevAddr(),
                   strerror(errno));
        return false;
    }

    value = data[0];
    return true;
}

bool POWERCONTROL::readRegister8(uint8_t reg, uint8_t &value)
{
    (void)reg;
    value = 0;

    /*
     * Register-pointer reads are intentionally disabled.
     *
     * The AVR protocol is one-byte only:
     *
     *   bare read          -> status byte
     *   one-byte write CMD -> explicit command
     *
     * There is no register map, no register pointer, and no repeated-start
     * read model in the production protocol.
     */
    LOGT_ERROR("POWERCONTROL(%02X) register read rejected: register-pointer protocol is disabled",
               _i2cPort.getDevAddr());

    return false;
}

bool POWERCONTROL::readStatus(POWERCONTROL_data &data)
{
    uint8_t status = 0;

    /*
     * Normal production path.
     *
     * Read only the status byte with a bare one-byte I2C read.
     * Do not read firmware version.
     * Do not read timer MSB/LSB registers.
     * Do not use register-pointer/repeated-start reads.
     */
    if(!readStatusByte(status)) {
        return false;
    }

    data.statusByte = status;
    data.acOK = (status & STATUS_BIT_AC_OK) != 0;

    /*
     * The AVR reports the stored wake preset in the upper status bits.
     * This is not a live countdown. It is the stored preset value that will
     * be loaded after relay-off during shutdown sleep.
     */
    data.wakeTimerMin = wakeMinutesFromStatus(status);

    return true;
}

/**
 * @brief Send a one-byte command to the AVR power controller.
 *
 * This is the explicit command path.
 *
 * Normal status polling must remain read-only and must not call this function.
 *
 * Firmware v26 command protocol:
 *
 *   bare one-byte read       -> status byte
 *   one-byte write command   -> explicit command
 *
 * @param command One-byte AVR command.
 * @return true if the command byte was written successfully.
 */
bool POWERCONTROL::sendCommand(uint8_t command)
{
    uint8_t data[1] = { command };

    if(!_i2cPort.isAvailable()) {
        LOGT_ERROR("POWERCONTROL(%02X) command 0x%02X failed: I2C port not available",
                   _i2cPort.getDevAddr(),
                   command);
        return false;
    }

    if(!_isSetup) {
        LOGT_ERROR("POWERCONTROL(%02X) command 0x%02X failed: device not setup",
                   _i2cPort.getDevAddr(),
                   command);
        return false;
    }

    /*
     * Explicit one-byte command write.
     *
     * This intentionally does not write a register pointer.
     * The single byte is the command.
     */
    if(!_i2cPort.stdWriteBytes(sizeof(data), data)) {
        LOGT_ERROR("POWERCONTROL(%02X) command 0x%02X write failed: %s",
                   _i2cPort.getDevAddr(),
                   command,
                   strerror(errno));
        return false;
    }

    // LOGT_INFO("POWERCONTROL(%02X) command 0x%02X sent",
    //           _i2cPort.getDevAddr(),
    //           command);

    return true;
}

/**
 * @brief Request delayed power shutdown from the AVR.
 *
 * Sends firmware command:
 *
 *   'S' = delayed shutdown request
 *
 * @return true if the command byte was written successfully.
 */
bool POWERCONTROL::requestDelayedShutdown()
{
    return sendCommand(COMMAND_DELAYED_SHUTDOWN);
}
