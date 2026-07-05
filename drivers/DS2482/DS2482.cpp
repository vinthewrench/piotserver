//
//  DS2482.cpp
//  DS2482Test
//
//  Minimal DS2482 I2C-to-1Wire bridge driver.
//  DS18B20-only support for first-pass testing.
//

#include "DS2482.hpp"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

static constexpr uint8_t DS2482_CMD_DEVICE_RESET       = 0xF0;
static constexpr uint8_t DS2482_CMD_SET_READ_POINTER   = 0xE1;
static constexpr uint8_t DS2482_CMD_1WIRE_RESET        = 0xB4;
static constexpr uint8_t DS2482_CMD_1WIRE_WRITE_BYTE   = 0xA5;
static constexpr uint8_t DS2482_CMD_1WIRE_READ_BYTE    = 0x96;
static constexpr uint8_t DS2482_CMD_1WIRE_TRIPLET      = 0x78;

static constexpr uint8_t DS2482_PTR_STATUS             = 0xF0;
static constexpr uint8_t DS2482_PTR_DATA               = 0xE1;

static constexpr uint8_t DS2482_STATUS_1WB             = 0x01;
static constexpr uint8_t DS2482_STATUS_PPD             = 0x02;
static constexpr uint8_t DS2482_STATUS_SD              = 0x04;
static constexpr uint8_t DS2482_STATUS_RST             = 0x10;
static constexpr uint8_t DS2482_STATUS_SBR             = 0x20;
static constexpr uint8_t DS2482_STATUS_TSB             = 0x40;
static constexpr uint8_t DS2482_STATUS_DIR             = 0x80;

static constexpr uint8_t ONEWIRE_CMD_SEARCH_ROM        = 0xF0;
static constexpr uint8_t ONEWIRE_CMD_SKIP_ROM          = 0xCC;
static constexpr uint8_t ONEWIRE_CMD_MATCH_ROM         = 0x55;

static constexpr uint8_t DS18B20_FAMILY_CODE           = 0x28;
static constexpr uint8_t DS18B20_CMD_CONVERT_T         = 0x44;
static constexpr uint8_t DS18B20_CMD_READ_SCRATCHPAD   = 0xBE;

static constexpr useconds_t DS18B20_CONVERSION_US      = 750000;
static constexpr int DS2482_READY_RETRY_COUNT          = 1000;
static constexpr useconds_t DS2482_READY_DELAY_US      = 1000;

DS2482::DS2482()
: _i2cPort(),
  _isSetup(false),
  _deviceAddress(0x18)
{
}

DS2482::~DS2482()
{
    stop();
}

bool DS2482::begin(uint8_t deviceAddress)
{
    int error = 0;
    return begin(deviceAddress, error);
}

bool DS2482::begin(uint8_t deviceAddress, int &error)
{
    error = 0;

    _deviceAddress = deviceAddress;
    _isSetup = false;

    if (!_i2cPort.begin(deviceAddress, error)) {
        return false;
    }

    if (!resetBridge(error)) {
        _i2cPort.stop();
        return false;
    }

    _isSetup = true;
    return true;
}

void DS2482::stop()
{
    if (_isSetup) {
        _i2cPort.stop();
    }

    _isSetup = false;
}

bool DS2482::isOpen()
{
    return _isSetup;
}

uint8_t DS2482::getDevAddr()
{
    return _deviceAddress;
}

bool DS2482::readBridgeStatus(uint8_t &status, int &error)
{
    return readStatus(status, error);
}

bool DS2482::probeDS18B20(std::vector<Sensor> &sensors)
{
    int error = 0;
    return probeDS18B20(sensors, error);
}

bool DS2482::probeDS18B20(std::vector<Sensor> &sensors, int &error)
{
    sensors.clear();
    error = 0;

    std::vector<std::array<uint8_t, 8>> roms;

    if (!searchRoms(roms, error)) {
        return false;
    }

    for (const auto &rom : roms) {
        if (rom[0] != DS18B20_FAMILY_CODE) {
            continue;
        }

        if (!validRomCRC(rom)) {
            continue;
        }

        Sensor sensor;
        sensor.rom = rom;
        sensors.push_back(sensor);
    }

    return true;
}

bool DS2482::readTemps(std::vector<Temperature> &temperatures)
{
    int error = 0;
    return readTemps(temperatures, error);
}

bool DS2482::readTemps(std::vector<Temperature> &temperatures, int &error)
{
    temperatures.clear();
    error = 0;

    std::vector<Sensor> sensors;

    if (!probeDS18B20(sensors, error)) {
        return false;
    }

    if (sensors.empty()) {
        return true;
    }

    if (!startConversionAll(error)) {
        return false;
    }

    usleep(DS18B20_CONVERSION_US);

    for (const auto &sensor : sensors) {
        Temperature reading;
        int sensorError = 0;

        if (!readOneTemperature(sensor.rom, reading, sensorError)) {
            reading.success = false;

            if (reading.errorText.empty()) {
                if (sensorError != 0) {
                    reading.errorText = strerror(sensorError);
                }
                else {
                    reading.errorText = "temperature read failed";
                }
            }
        }

        temperatures.push_back(reading);
    }

    return true;
}

bool DS2482::readTemp(const std::array<uint8_t, 8> &rom, Temperature &temperature)
{
    int error = 0;
    return readTemp(rom, temperature, error);
}

bool DS2482::readTemp(const std::array<uint8_t, 8> &rom, Temperature &temperature, int &error)
{
    error = 0;

    if (rom[0] != DS18B20_FAMILY_CODE) {
        temperature = Temperature();
        temperature.rom = rom;
        temperature.success = false;
        temperature.errorText = "unsupported ROM family";
        return false;
    }

    if (!validRomCRC(rom)) {
        temperature = Temperature();
        temperature.rom = rom;
        temperature.success = false;
        temperature.errorText = "ROM CRC failed";
        return false;
    }

    if (!startConversionOne(rom, error)) {
        temperature = Temperature();
        temperature.rom = rom;
        temperature.success = false;
        temperature.errorText = "failed to start conversion";
        return false;
    }

    usleep(DS18B20_CONVERSION_US);

    return readOneTemperature(rom, temperature, error);
}

std::string DS2482::romToString(const std::array<uint8_t, 8> &rom)
{
    std::ostringstream oss;

    for (size_t i = 0; i < rom.size(); i++) {
        if (i != 0) {
            oss << "-";
        }

        oss << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(rom[i]);
    }

    return oss.str();
}

bool DS2482::stringToRom(const std::string &text, std::array<uint8_t, 8> &rom)
{
    std::string cleaned;

    for (char ch : text) {
        if (ch == '-' || ch == ':' || ch == ' ') {
            continue;
        }

        cleaned.push_back(ch);
    }

    if (cleaned.size() != 16) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        std::string byteText = cleaned.substr(i * 2, 2);

        char *end = nullptr;
        errno = 0;
        unsigned long value = strtoul(byteText.c_str(), &end, 16);

        if (errno != 0 || end == byteText.c_str() || *end != '\0' || value > 0xFF) {
            return false;
        }

        rom[i] = static_cast<uint8_t>(value);
    }

    return true;
}

bool DS2482::resetBridge(int &error)
{
    error = 0;

    uint8_t command[1] = {
        DS2482_CMD_DEVICE_RESET
    };

    if (!_i2cPort.stdWriteBytes(sizeof(command), command)) {
        error = EIO;
        return false;
    }

    usleep(1000);

    uint8_t status = 0;

    if (!readStatus(status, error)) {
        return false;
    }

    if ((status & DS2482_STATUS_RST) == 0) {
        error = EIO;
        return false;
    }

    return true;
}

bool DS2482::readStatus(uint8_t &status, int &error)
{
    error = 0;

    uint8_t command[2] = {
        DS2482_CMD_SET_READ_POINTER,
        DS2482_PTR_STATUS
    };

    if (!_i2cPort.stdWriteBytes(sizeof(command), command)) {
        error = EIO;
        return false;
    }

    if (!_i2cPort.stdReadBytes(1, &status)) {
        error = EIO;
        return false;
    }

    return true;
}

bool DS2482::waitReady(int &error)
{
    error = 0;

    for (int i = 0; i < DS2482_READY_RETRY_COUNT; i++) {
        uint8_t status = 0;

        if (!readStatus(status, error)) {
            return false;
        }

        if ((status & DS2482_STATUS_1WB) == 0) {
            return true;
        }

        usleep(DS2482_READY_DELAY_US);
    }

    error = ETIMEDOUT;
    return false;
}

bool DS2482::oneWireReset(bool &presence, int &error)
{
    presence = false;
    error = 0;

    if (!waitReady(error)) {
        return false;
    }

    uint8_t command[1] = {
        DS2482_CMD_1WIRE_RESET
    };

    if (!_i2cPort.stdWriteBytes(sizeof(command), command)) {
        error = EIO;
        return false;
    }

    if (!waitReady(error)) {
        return false;
    }

    uint8_t status = 0;

    if (!readStatus(status, error)) {
        return false;
    }

    if ((status & DS2482_STATUS_SD) != 0) {
        error = EIO;
        return false;
    }

    presence = ((status & DS2482_STATUS_PPD) != 0);
    return true;
}

bool DS2482::oneWireWriteByte(uint8_t byte, int &error)
{
    error = 0;

    if (!waitReady(error)) {
        return false;
    }

    uint8_t command[2] = {
        DS2482_CMD_1WIRE_WRITE_BYTE,
        byte
    };

    if (!_i2cPort.stdWriteBytes(sizeof(command), command)) {
        error = EIO;
        return false;
    }

    return waitReady(error);
}

bool DS2482::oneWireReadByte(uint8_t &byte, int &error)
{
    error = 0;

    if (!waitReady(error)) {
        return false;
    }

    uint8_t readCommand[1] = {
        DS2482_CMD_1WIRE_READ_BYTE
    };

    if (!_i2cPort.stdWriteBytes(sizeof(readCommand), readCommand)) {
        error = EIO;
        return false;
    }

    if (!waitReady(error)) {
        return false;
    }

    uint8_t pointerCommand[2] = {
        DS2482_CMD_SET_READ_POINTER,
        DS2482_PTR_DATA
    };

    if (!_i2cPort.stdWriteBytes(sizeof(pointerCommand), pointerCommand)) {
        error = EIO;
        return false;
    }

    if (!_i2cPort.stdReadBytes(1, &byte)) {
        error = EIO;
        return false;
    }

    return true;
}

bool DS2482::oneWireTriplet(bool direction,
                            bool &singleBitResult,
                            bool &tripletSecondBit,
                            bool &directionTaken,
                            int &error)
{
    singleBitResult = false;
    tripletSecondBit = false;
    directionTaken = false;
    error = 0;

    if (!waitReady(error)) {
        return false;
    }

    uint8_t command[2] = {
        DS2482_CMD_1WIRE_TRIPLET,
        static_cast<uint8_t>(direction ? 0x80 : 0x00)
    };

    if (!_i2cPort.stdWriteBytes(sizeof(command), command)) {
        error = EIO;
        return false;
    }

    if (!waitReady(error)) {
        return false;
    }

    uint8_t status = 0;

    if (!readStatus(status, error)) {
        return false;
    }

    singleBitResult = ((status & DS2482_STATUS_SBR) != 0);
    tripletSecondBit = ((status & DS2482_STATUS_TSB) != 0);
    directionTaken = ((status & DS2482_STATUS_DIR) != 0);

    return true;
}

bool DS2482::searchRoms(std::vector<std::array<uint8_t, 8>> &roms, int &error)
{
    roms.clear();
    error = 0;

    bool presence = false;

    if (!oneWireReset(presence, error)) {
        return false;
    }

    if (!presence) {
        return true;
    }

    int lastDiscrepancy = 0;
    bool lastDeviceFlag = false;
    std::array<uint8_t, 8> rom = {};

    while (!lastDeviceFlag) {
        int idBitNumber = 1;
        int lastZero = 0;
        int romByteNumber = 0;
        uint8_t romByteMask = 1;

        if (!oneWireReset(presence, error)) {
            return false;
        }

        if (!presence) {
            break;
        }

        if (!oneWireWriteByte(ONEWIRE_CMD_SEARCH_ROM, error)) {
            return false;
        }

        while (romByteNumber < 8) {
            bool searchDirection = false;

            if (idBitNumber < lastDiscrepancy) {
                searchDirection = ((rom[romByteNumber] & romByteMask) != 0);
            }
            else {
                searchDirection = (idBitNumber == lastDiscrepancy);
            }

            bool sbr = false;
            bool tsb = false;
            bool dirTaken = false;

            if (!oneWireTriplet(searchDirection, sbr, tsb, dirTaken, error)) {
                return false;
            }

            if (sbr && tsb) {
                error = EIO;
                return false;
            }

            if (!sbr && !tsb && !dirTaken) {
                lastZero = idBitNumber;
            }

            if (dirTaken) {
                rom[romByteNumber] |= romByteMask;
            }
            else {
                rom[romByteNumber] &= static_cast<uint8_t>(~romByteMask);
            }

            idBitNumber++;
            romByteMask <<= 1;

            if (romByteMask == 0) {
                romByteNumber++;
                romByteMask = 1;
            }
        }

        lastDiscrepancy = lastZero;

        if (lastDiscrepancy == 0) {
            lastDeviceFlag = true;
        }

        bool allZero = std::all_of(rom.begin(), rom.end(), [](uint8_t b) {
            return b == 0;
        });

        if (!allZero) {
            roms.push_back(rom);
        }
    }

    return true;
}

bool DS2482::matchRom(const std::array<uint8_t, 8> &rom, int &error)
{
    error = 0;

    if (!oneWireWriteByte(ONEWIRE_CMD_MATCH_ROM, error)) {
        return false;
    }

    for (uint8_t b : rom) {
        if (!oneWireWriteByte(b, error)) {
            return false;
        }
    }

    return true;
}

bool DS2482::skipRom(int &error)
{
    return oneWireWriteByte(ONEWIRE_CMD_SKIP_ROM, error);
}

bool DS2482::startConversionAll(int &error)
{
    error = 0;

    bool presence = false;

    if (!oneWireReset(presence, error)) {
        return false;
    }

    if (!presence) {
        error = ENODEV;
        return false;
    }

    if (!skipRom(error)) {
        return false;
    }

    return oneWireWriteByte(DS18B20_CMD_CONVERT_T, error);
}

bool DS2482::startConversionOne(const std::array<uint8_t, 8> &rom, int &error)
{
    error = 0;

    bool presence = false;

    if (!oneWireReset(presence, error)) {
        return false;
    }

    if (!presence) {
        error = ENODEV;
        return false;
    }

    if (!matchRom(rom, error)) {
        return false;
    }

    return oneWireWriteByte(DS18B20_CMD_CONVERT_T, error);
}

bool DS2482::readScratchpad(const std::array<uint8_t, 8> &rom,
                            std::array<uint8_t, 9> &scratchpad,
                            int &error)
{
    scratchpad = {};
    error = 0;

    bool presence = false;

    if (!oneWireReset(presence, error)) {
        return false;
    }

    if (!presence) {
        error = ENODEV;
        return false;
    }

    if (!matchRom(rom, error)) {
        return false;
    }

    if (!oneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD, error)) {
        return false;
    }

    for (size_t i = 0; i < scratchpad.size(); i++) {
        if (!oneWireReadByte(scratchpad[i], error)) {
            return false;
        }
    }

    return true;
}

bool DS2482::readOneTemperature(const std::array<uint8_t, 8> &rom,
                                Temperature &temperature,
                                int &error)
{
    temperature = Temperature();
    temperature.rom = rom;
    error = 0;

    if (rom[0] != DS18B20_FAMILY_CODE) {
        temperature.success = false;
        temperature.errorText = "unsupported ROM family";
        error = EINVAL;
        return false;
    }

    if (!validRomCRC(rom)) {
        temperature.success = false;
        temperature.errorText = "ROM CRC failed";
        error = EIO;
        return false;
    }

    std::array<uint8_t, 9> scratchpad = {};

    if (!readScratchpad(rom, scratchpad, error)) {
        temperature.success = false;
        temperature.errorText = "failed to read scratchpad";
        return false;
    }

    if (!validScratchpadCRC(scratchpad)) {
        temperature.success = false;
        temperature.errorText = "scratchpad CRC failed";
        error = EIO;
        return false;
    }

    int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(scratchpad[1]) << 8) | scratchpad[0]);

    temperature.tempC = static_cast<float>(raw) / 16.0f;
    temperature.tempF = (temperature.tempC * 9.0f / 5.0f) + 32.0f;
    temperature.success = true;

    return true;
}

uint8_t DS2482::crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t inByte = data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t mix = (crc ^ inByte) & 0x01;
            crc >>= 1;

            if (mix) {
                crc ^= 0x8C;
            }

            inByte >>= 1;
        }
    }

    return crc;
}

bool DS2482::validRomCRC(const std::array<uint8_t, 8> &rom)
{
    return crc8(rom.data(), 8) == 0;
}

bool DS2482::validScratchpadCRC(const std::array<uint8_t, 9> &scratchpad)
{
    return crc8(scratchpad.data(), 9) == 0;
}
