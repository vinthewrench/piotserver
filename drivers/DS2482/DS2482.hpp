//
//  DS2482.hpp
//  DS2482Test
//
//  Minimal DS2482 I2C-to-1Wire bridge driver.
//  First-pass support is intentionally limited to DS18B20 temperature sensors.
//

#ifndef DS2482_hpp
#define DS2482_hpp

#include <stdint.h>
#include <stddef.h>

#include <array>
#include <string>
#include <vector>

#include "I2C.hpp"

class DS2482
{
public:
    struct Sensor
    {
        std::array<uint8_t, 8> rom;
    };

    struct Temperature
    {
        std::array<uint8_t, 8> rom;
        bool success;
        float tempC;
        float tempF;
        std::string errorText;

        Temperature()
        : rom{},
          success(false),
          tempC(0.0f),
          tempF(0.0f),
          errorText()
        {
        }
    };

public:
    DS2482();
    ~DS2482();

    bool begin(uint8_t deviceAddress = 0x18);
    bool begin(uint8_t deviceAddress, int &error);

    void stop();
    bool isOpen();

    uint8_t getDevAddr();

    bool readBridgeStatus(uint8_t &status, int &error);

    bool probeDS18B20(std::vector<Sensor> &sensors);
    bool probeDS18B20(std::vector<Sensor> &sensors, int &error);

    bool readTemps(std::vector<Temperature> &temperatures);
    bool readTemps(std::vector<Temperature> &temperatures, int &error);

    bool readTemp(const std::array<uint8_t, 8> &rom, Temperature &temperature);
    bool readTemp(const std::array<uint8_t, 8> &rom, Temperature &temperature, int &error);

    static std::string romToString(const std::array<uint8_t, 8> &rom);
    static bool stringToRom(const std::string &text, std::array<uint8_t, 8> &rom);

private:
    bool resetBridge(int &error);
    bool readStatus(uint8_t &status, int &error);
    bool waitReady(int &error);

    bool oneWireReset(bool &presence, int &error);
    bool oneWireWriteByte(uint8_t byte, int &error);
    bool oneWireReadByte(uint8_t &byte, int &error);

    bool oneWireTriplet(bool direction,
                        bool &singleBitResult,
                        bool &tripletSecondBit,
                        bool &directionTaken,
                        int &error);

    bool searchRoms(std::vector<std::array<uint8_t, 8>> &roms, int &error);

    bool matchRom(const std::array<uint8_t, 8> &rom, int &error);
    bool skipRom(int &error);

    bool startConversionAll(int &error);
    bool startConversionOne(const std::array<uint8_t, 8> &rom, int &error);

    bool readScratchpad(const std::array<uint8_t, 8> &rom,
                        std::array<uint8_t, 9> &scratchpad,
                        int &error);

    bool readOneTemperature(const std::array<uint8_t, 8> &rom,
                            Temperature &temperature,
                            int &error);

    static uint8_t crc8(const uint8_t *data, size_t len);
    static bool validRomCRC(const std::array<uint8_t, 8> &rom);
    static bool validScratchpadCRC(const std::array<uint8_t, 9> &scratchpad);

private:
    I2C     _i2cPort;
    bool    _isSetup;
    uint8_t _deviceAddress;
};

#endif /* DS2482_hpp */
