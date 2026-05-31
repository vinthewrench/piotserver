//
//  POWERCONTROL.hpp
//  POWERCONTROL
//
//  Created by vinnie on 5/30/26.
//

#ifndef POWERCONTROL_hpp
#define POWERCONTROL_hpp

#include <stdint.h>

#include "I2C.hpp"

/**
 * @class POWERCONTROL
 *
 * @brief Low-level I2C interface for the ATmega88PB power controller.
 *
 * AVR POWERCONTROL firmware v26 uses a one-byte I2C protocol:
 *
 * Normal production read:
 *
 *   i2ctransfer -y 1 r1@0x08
 *
 * This returns one status byte.
 *
 * There is no register map.
 * There is no register pointer.
 * There is no repeated-start read model.
 * There are no multi-byte I2C operations.
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
 * This class must not issue shutdown, cancel, LED, relay, or wake-timer
 * commands during begin() or normal polling.
 *
 * Normal driver polling is read-only.
 */
class POWERCONTROL
{
public:

    /**
     * @brief Decoded power controller status block.
     *
     * wakeTimerMin is decoded from the wake preset bits in the status byte.
     * It is the stored wake preset, not a live countdown.
     */
    struct POWERCONTROL_data
    {
        uint8_t statusByte;
        uint16_t wakeTimerMin;
        bool acOK;
    };

    POWERCONTROL();
    ~POWERCONTROL();

    static const uint8_t POWERCONTROL_DEFAULT_ADDR = 0x08;

    bool begin(uint8_t deviceAddress = POWERCONTROL_DEFAULT_ADDR);
    bool begin(uint8_t deviceAddress, int &error);

    void stop();
    bool isOpen();

    uint8_t getDevAddr();

    /**
     * @brief Read and decode the AVR status byte.
     *
     * Uses a bare one-byte I2C read. Does not write a register pointer first.
     *
     * @param data Decoded status structure.
     * @return true if the status byte was read successfully, false otherwise.
     */
    bool readStatus(POWERCONTROL_data &data);

private:

    /**
     * @brief Read the raw one-byte AVR status byte.
     *
     * This is the normal production read path:
     *
     *   i2ctransfer -y 1 r1@0x08
     *
     * @param value Raw status byte.
     * @return true if read succeeded, false otherwise.
     */
    bool readStatusByte(uint8_t &value);

    /**
     * @brief Disabled legacy register-pointer read helper.
     *
     * Firmware v26 does not use register-pointer reads. This declaration is
     * retained only so old callers fail in one controlled place instead of
     * accidentally reintroducing the old protocol.
     *
     * @param reg Ignored.
     * @param value Set to zero.
     * @return Always false.
     */
    bool readRegister8(uint8_t reg, uint8_t &value);

private:

    I2C     _i2cPort;
    bool    _isSetup;
};

#endif /* POWERCONTROL_hpp */
