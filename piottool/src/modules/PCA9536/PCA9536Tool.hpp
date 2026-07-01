//
// PCA9536Tool.hpp
// piottool module for PCA9536.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "PCA9536.hpp"


class PCA9536Tool : public PToolModule
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
    static constexpr uint8_t PCA9536_OUTPUT_DIRECTION_MASK = 0x00;
    static constexpr uint8_t PCA9536_ALL_BITS_MASK = 0x0F;

    static bool parseBitNumber(const std::string& text,
                               uint8_t& bitOut);

    static bool parseRelayNumber(const std::string& text,
                                 uint8_t& bitOut);

    static bool parseOnOff(const std::string& text,
                           bool& stateOut);

    static bool parseHexNibble(const std::string& text,
                               uint8_t& valueOut);

    static PCA9536::pinStates_t statesFromMask(uint8_t mask);

    static bool setOneBit(PCA9536& device,
                          uint8_t bit,
                          bool state);

    static bool writeMask(PCA9536& device,
                          uint8_t mask);

    static bool readMask(PCA9536& device,
                         uint8_t& maskOut);

    static void printStates(uint8_t address,
                            const PCA9536::pinStates_t& states);
};
