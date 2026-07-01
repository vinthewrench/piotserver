//
// PToolModuleAPI.hpp
// Dynamic module ABI for piottool.
//

#pragma once

#include <cstdint>

#include "PToolModule.hpp"


#define PTOOL_MODULE_API_VERSION 1


extern "C" {

typedef uint32_t (*PToolModuleAPIVersionFn)();
typedef PToolModule* (*PToolModuleCreateFn)();
typedef void (*PToolModuleDestroyFn)(PToolModule* module);

}
