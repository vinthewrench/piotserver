//
// DS2482Tool.hpp
// piottool module for the DS2482 I2C-to-1Wire bridge.
//
// First-pass support is intentionally DS18B20-only.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "DS2482.hpp"


class DS2482Tool : public PToolModule
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
};
