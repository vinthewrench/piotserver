//
// POWERCONTROLTool.hpp
// piottool module for the POWERCONTROL I2C power controller.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModule.hpp"
#include "POWERCONTROL.hpp"


class POWERCONTROLTool : public PToolModule
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
