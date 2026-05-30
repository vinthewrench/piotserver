//
//  POWERCONTROL.hpp
//  POWERCONTROL
//
//  Created by vinnie on 5/30/26.
//

#ifndef POWERCONTROL_hpp
#define POWERCONTROL_hpp

#include <stdio.h>
#include <stdint.h>

#include "I2C.hpp"

/**
 * @class POWERCONTROL
 *
 * @brief Low-level I2C interface for the ATmega88PB power controller.
 *
 * This class only performs read-only status access for the first driver pass.
 * Shutdown / relay / wake-timer write operations should be added later as
 * explicit, deliberate operations.
 */
class POWERCONTROL
{
public:

    /**
     * @brief Decoded power controller status block.
     */
    struct POWERCONTROL_data
    {
        uint8_t firmwareVersion;
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

    bool readStatus(POWERCONTROL_data &data);

private:

    static constexpr uint8_t REG_EXT_STATUS = 0xF0;

    I2C     _i2cPort;
    bool    _isSetup;
};

#endif /* POWERCONTROL_hpp */
