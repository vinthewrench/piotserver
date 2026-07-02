//
// SHT30Tool.hpp
// piottool module for SHT30.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "SHT30.hpp"


class SHT30Tool : public PToolModule
{
public:
    const char* name() const override;
    const char* description() const override;

    bool hasDefaultAddress() const override;
    uint32_t defaultAddress() const override;

    void printHelp(std::ostream& out) const override;

    void printCommandHelp(const std::string& command,
                          std::ostream& out) const override;

    int run(const std::string& command,
            const std::vector<std::string>& args,
            PToolContext& ctx) override;

private:
    static void printSensor(uint8_t address,
                            uint32_t bus,
                            const SHT30::SHT30_data& data,
                            bool json);

    static void printSerial(uint8_t address,
                            uint32_t bus,
                            const uint8_t serialNo[8],
                            bool json);

    static void printSerialHex(std::ostream& out,
                               const uint8_t serialNo[8]);
};
