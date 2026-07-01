//
// ModuleRegistry.cpp
// Registry for dynamically loaded piottool modules.
//

#include "ModuleRegistry.hpp"

#include <dlfcn.h>

#include <iomanip>
#include <iostream>


ModuleRegistry::ModuleRegistry()
{
}


ModuleRegistry::~ModuleRegistry()
{
    for(auto it = _modules.rbegin(); it != _modules.rend(); ++it) {
        LoadedModule& loaded = *it;

        if(loaded.module && loaded.destroyFn) {
            loaded.destroyFn(loaded.module);
            loaded.module = nullptr;
        }

        if(loaded.handle) {
            dlclose(loaded.handle);
            loaded.handle = nullptr;
        }
    }
}


bool ModuleRegistry::addLoadedModule(const std::string& path,
                                     void* handle,
                                     PToolModule* module,
                                     PToolModuleDestroyFn destroyFn,
                                     std::string& errorOut)
{
    if(handle == nullptr) {
        errorOut = "null dlopen handle";
        return false;
    }

    if(module == nullptr) {
        errorOut = "null module instance";
        return false;
    }

    if(destroyFn == nullptr) {
        errorOut = "null destroy function";
        return false;
    }

    const char* moduleName = module->name();

    if(moduleName == nullptr || *moduleName == '\0') {
        errorOut = "module has empty name";
        return false;
    }

    if(find(moduleName) != nullptr) {
        errorOut = std::string("duplicate module name: ") + moduleName;
        return false;
    }

    LoadedModule loaded;
    loaded.path = path;
    loaded.handle = handle;
    loaded.module = module;
    loaded.destroyFn = destroyFn;

    _modules.push_back(loaded);

    return true;
}


PToolModule* ModuleRegistry::find(const std::string& name) const
{
    for(const auto& loaded : _modules) {
        if(loaded.module && name == loaded.module->name()) {
            return loaded.module;
        }
    }

    return nullptr;
}


void ModuleRegistry::printModules(std::ostream& out) const
{
    out << "Available modules:\n";

    if(_modules.empty()) {
        out << "  <none>\n";
        return;
    }

    for(const auto& loaded : _modules) {
        if(!loaded.module) {
            continue;
        }

        out << "  "
            << std::left
            << std::setw(14)
            << loaded.module->name()
            << loaded.module->description()
            << "\n";
    }
}


size_t ModuleRegistry::size() const
{
    return _modules.size();
}
