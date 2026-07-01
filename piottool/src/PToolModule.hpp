//
// PToolModule.hpp
// piottool module interface.
//

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "PToolContext.hpp"


class PToolModule
{
public:
    virtual ~PToolModule() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;

    virtual bool hasDefaultAddress() const = 0;
    virtual uint32_t defaultAddress() const = 0;

    virtual void printHelp(std::ostream& out) const = 0;

    virtual void printCommandHelp(const std::string& command,
                                  std::ostream& out) const = 0;

    virtual int run(const std::string& command,
                    const std::vector<std::string>& args,
                    PToolContext& ctx) = 0;
};
