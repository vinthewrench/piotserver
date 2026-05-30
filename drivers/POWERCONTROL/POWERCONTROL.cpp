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
 * AVR register block:
 *
 *   0xF0 = firmware version
 *   0xF1 = status byte
 *   0xF2 = wake timer MSB
 *   0xF3 = wake timer LSB
 *
 * Status byte:
 *
 *   bit 0 = relay state, 1 = relay ON
 *   bit 1 = red LED state, 1 = ON
 *   bit 2 = green LED state, 1 = ON
 *   bit 3 = AC_OK input, raw high = 1
 *   bit 4 = wake timer active
 */

static constexpr uint8_t STATUS_BIT_AC_OK = 0x08;

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
    return _isSetup;
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

bool POWERCONTROL::readStatus(POWERCONTROL_data &data)
{
    uint8_t cmd[1] = { REG_EXT_STATUS };
    uint8_t block[4] = { 0, 0, 0, 0 };

    if(!_i2cPort.isAvailable()) {
        return false;
    }

    if(!_i2cPort.stdWriteBytes(1, cmd)) {
        LOGT_ERROR("POWERCONTROL(%02X) WRITE status register failed: %s",
                   _i2cPort.getDevAddr(),
                   strerror(errno));
        return false;
    }

    if(!_i2cPort.stdReadBytes(4, block)) {
        LOGT_ERROR("POWERCONTROL(%02X) READ status block failed: %s",
                   _i2cPort.getDevAddr(),
                   strerror(errno));
        return false;
    }

    data.firmwareVersion = block[0];
    data.statusByte = block[1];
    data.wakeTimerMin = ((uint16_t)block[2] << 8) | block[3];
    data.acOK = (data.statusByte & STATUS_BIT_AC_OK) != 0;

    return true;
}
