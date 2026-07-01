//
// PToolContext.hpp
// Shared execution context for piottool modules.
//

#pragma once

#include <cstdint>


struct PToolContext
{
    uint32_t bus = 1;

    bool hasAddressOverride = false;
    uint32_t addressOverride = 0;

    bool json = false;
    int verbose = 0;
};
