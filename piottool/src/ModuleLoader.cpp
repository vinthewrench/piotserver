//
// ModuleLoader.cpp
// Dynamic loader for piottool modules.
//

#include "ModuleLoader.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ModuleRegistry.hpp"
#include "PToolModuleAPI.hpp"


namespace fs = std::filesystem;


bool ModuleLoader::loadModulesFromDirectory(const std::string& moduleDir,
                                            ModuleRegistry& registry,
                                            bool verbose,
                                            std::string& errorOut)
{
    errorOut.clear();

    fs::path dirPath(moduleDir);

    if(!fs::exists(dirPath)) {
        errorOut = "module directory does not exist: " + moduleDir;
        return false;
    }

    if(!fs::is_directory(dirPath)) {
        errorOut = "module path is not a directory: " + moduleDir;
        return false;
    }

    std::vector<fs::path> modulePaths;

    for(const auto& entry : fs::directory_iterator(dirPath)) {
        if(!entry.is_regular_file()) {
            continue;
        }

        const fs::path path = entry.path();

#if defined(__APPLE__)
        if(path.extension() == ".dylib") {
            modulePaths.push_back(path);
        }
#else
        if(path.extension() == ".so") {
            modulePaths.push_back(path);
        }
#endif
    }

    std::sort(modulePaths.begin(), modulePaths.end());

    for(const fs::path& path : modulePaths) {
        std::string loadError;

        if(!loadOneModule(path.string(), registry, verbose, loadError)) {
            std::cerr << "piottool: failed to load module "
                      << path.string()
                      << ": "
                      << loadError
                      << "\n";
        }
    }

    return true;
}


bool ModuleLoader::loadOneModule(const std::string& path,
                                 ModuleRegistry& registry,
                                 bool verbose,
                                 std::string& errorOut)
{
    errorOut.clear();

    if(verbose) {
        std::cerr << "piottool: loading module: " << path << "\n";
    }

    dlerror();

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);

    if(handle == nullptr) {
        const char* err = dlerror();
        errorOut = err ? err : "dlopen failed";
        return false;
    }

    dlerror();
    auto apiVersionFn = reinterpret_cast<PToolModuleAPIVersionFn>(
        dlsym(handle, "ptoolModuleAPIVersion")
    );

    const char* err = dlerror();
    if(err != nullptr || apiVersionFn == nullptr) {
        errorOut = err ? err : "missing ptoolModuleAPIVersion";
        dlclose(handle);
        return false;
    }

    const uint32_t apiVersion = apiVersionFn();

    if(apiVersion != PTOOL_MODULE_API_VERSION) {
        errorOut = "module API version mismatch";
        dlclose(handle);
        return false;
    }

    dlerror();
    auto createFn = reinterpret_cast<PToolModuleCreateFn>(
        dlsym(handle, "createPToolModule")
    );

    err = dlerror();
    if(err != nullptr || createFn == nullptr) {
        errorOut = err ? err : "missing createPToolModule";
        dlclose(handle);
        return false;
    }

    dlerror();
    auto destroyFn = reinterpret_cast<PToolModuleDestroyFn>(
        dlsym(handle, "destroyPToolModule")
    );

    err = dlerror();
    if(err != nullptr || destroyFn == nullptr) {
        errorOut = err ? err : "missing destroyPToolModule";
        dlclose(handle);
        return false;
    }

    PToolModule* module = createFn();

    if(module == nullptr) {
        errorOut = "createPToolModule returned null";
        dlclose(handle);
        return false;
    }

    std::string registryError;

    if(!registry.addLoadedModule(path, handle, module, destroyFn, registryError)) {
        destroyFn(module);
        dlclose(handle);
        errorOut = registryError;
        return false;
    }

    if(verbose) {
        std::cerr << "piottool: loaded module: "
                  << module->name()
                  << " from "
                  << path
                  << "\n";
    }

    return true;
}
