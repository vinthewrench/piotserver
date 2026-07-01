//
// TMP10XTool.hpp
// piottool module for TMP10X temperature sensors.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "TMP10X.hpp"


class TMP10XTool : public PToolModule
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
    static void printTemperature(uint8_t address,
                                 uint32_t bus,
                                 float tempC,
                                 float tempF,
                                 bool json);
};
