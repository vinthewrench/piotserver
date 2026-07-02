//
// MCP23008Tool.hpp
// piottool module for MCP23008.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "MCP23008.hpp"


class MCP23008Tool : public PToolModule
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
    static constexpr uint8_t MCP23008_OUTPUT_DIRECTION_MASK = 0x00;
    static constexpr uint8_t MCP23008_ALL_BITS_MASK = 0xFF;

    static bool parseBitNumber(const std::string& text,
                               uint8_t& bitOut);

    static bool parseOnOff(const std::string& text,
                           bool& stateOut);

    static bool parseHexByte(const std::string& text,
                             uint8_t& valueOut);

    static MCP23008::pinStates_t statesFromMask(uint8_t mask);

    static bool setOneBit(MCP23008& device,
                          uint8_t bit,
                          bool state);

    static bool writeMask(MCP23008& device,
                          uint8_t mask);

    static void printStates(uint8_t address,
                            const MCP23008::pinStates_t& states);
};
