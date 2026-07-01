//
// ModuleLoader.hpp
// Dynamic loader for piottool modules.
//

#pragma once

#include <string>

class ModuleRegistry;


class ModuleLoader
{
public:
    static bool loadModulesFromDirectory(const std::string& moduleDir,
                                         ModuleRegistry& registry,
                                         bool verbose,
                                         std::string& errorOut);

private:
    static bool loadOneModule(const std::string& path,
                              ModuleRegistry& registry,
                              bool verbose,
                              std::string& errorOut);
};
