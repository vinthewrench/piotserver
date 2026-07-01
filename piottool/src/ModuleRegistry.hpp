//
// ModuleRegistry.hpp
// Registry for dynamically loaded piottool modules.
//

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "PToolModuleAPI.hpp"


class ModuleRegistry
{
public:
    ModuleRegistry();
    ~ModuleRegistry();

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    bool addLoadedModule(const std::string& path,
                         void* handle,
                         PToolModule* module,
                         PToolModuleDestroyFn destroyFn,
                         std::string& errorOut);

    PToolModule* find(const std::string& name) const;

    void printModules(std::ostream& out) const;

    size_t size() const;

private:
    struct LoadedModule
    {
        std::string path;
        void* handle = nullptr;
        PToolModule* module = nullptr;
        PToolModuleDestroyFn destroyFn = nullptr;
    };

    std::vector<LoadedModule> _modules;
};
