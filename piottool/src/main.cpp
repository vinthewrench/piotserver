//
// main.cpp
// piottool
//
// Command-line hardware/tool utility for pIoTServer devices.
//

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "CmdLineParser.hpp"
#include "LogMgr.hpp"
#include "ModuleLoader.hpp"
#include "ModuleRegistry.hpp"
#include "PToolContext.hpp"


namespace fs = std::filesystem;


struct PToolOptions
{
    uint32_t bus = 1;
    uint32_t addr = 0;

    std::string moduleDir;

    bool json = false;
    int verbose = 0;
};


static PToolOptions gOptions;


static CmdLineOption sOptions[] =
{
    { CmdLineOptionType::UInt,    &gOptions.bus,       "bus",        'b', "n",   "I2C bus number, default 1" },
    { CmdLineOptionType::HexUInt, &gOptions.addr,      "addr",       'a', "hex", "Override module default I2C address" },
    { CmdLineOptionType::String,  &gOptions.moduleDir, "module-dir", 'm', "dir", "Override module directory" },
    { CmdLineOptionType::Boolean, &gOptions.json,      "json",       'j', nullptr, "Print JSON where supported" },
    { CmdLineOptionType::Count,   &gOptions.verbose,   "verbose",    'v', nullptr, "Increase verbose output" },
};

#define OPTION_COUNT ((size_t)(sizeof(sOptions) / sizeof(sOptions[0])))


static std::string sDefaultModuleDir(const char* argv0)
{
    fs::path exePath = argv0 && *argv0 ? fs::path(argv0) : fs::path("piottool");

    fs::path exeDir = exePath.parent_path();

    if(exeDir.empty()) {
        exeDir = ".";
    }

    return (exeDir / "plugins").string();
}


static void sStartToolLogging()
{
    /*
     * piottool is a foreground command-line utility.
     *
     * The lower drivers already use LOGT_ERROR(), LOGT_DEBUG(), etc.
     * In piotserver, printing is enabled by the server's -p / gPrint_flag path.
     * piottool has no -d/-p mode, so enable print logging here.
     *
     * Default:
     *   show LOGT_ERROR and normal tool output.
     *
     * -v:
     *   show verbose lower-driver logging.
     */
    START_LOGPRINT;

    if(gOptions.verbose > 0) {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelVerbose;
    }
    else {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelError;
    }
}


static void sPrintUsage(std::ostream& out,
                        const ModuleRegistry* registry)
{
    CmdLineParser::printUsage(out,
                              "piottool",
                              "piottool [options] <module> <command> [args]",
                              sOptions,
                              OPTION_COUNT);

    if(registry) {
        registry->printModules(out);
        out << "\n";
    }

    out << "Examples:\n";
    out << "  piottool pca9536 help\n";
    out << "  piottool pca9536 status\n";
    out << "  piottool tmp10x read\n";
    out << "  piottool --bus 1 --addr 0x41 pca9536 status\n";
    out << "  piottool --module-dir ./piottool/plugins pca9536 status\n";
    out << "\n";
}


static PToolContext sBuildContext()
{
    PToolContext ctx;

    ctx.bus = gOptions.bus;
    ctx.hasAddressOverride = (gOptions.addr != 0);
    ctx.addressOverride = gOptions.addr;
    ctx.json = gOptions.json;
    ctx.verbose = gOptions.verbose;

    return ctx;
}


int main(int argc, const char* argv[])
{
    CmdLineParseResult parsed = CmdLineParser::parse(argc,
                                                     argv,
                                                     sOptions,
                                                     OPTION_COUNT,
                                                     true);

    if(gOptions.moduleDir.empty()) {
        gOptions.moduleDir = sDefaultModuleDir(argc > 0 ? argv[0] : nullptr);
    }

    sStartToolLogging();

    ModuleRegistry registry;

    std::string loadError;

    if(!ModuleLoader::loadModulesFromDirectory(gOptions.moduleDir,
                                               registry,
                                               gOptions.verbose > 0,
                                               loadError)) {
        std::cerr << "piottool: " << loadError << "\n";
        return EXIT_FAILURE;
    }

    if(!parsed.ok) {
        std::cerr << "error: " << parsed.error << "\n";
        sPrintUsage(std::cerr, &registry);
        return EXIT_FAILURE;
    }

    if(parsed.helpRequested || parsed.positionals.empty()) {
        sPrintUsage(std::cout, &registry);
        return EXIT_SUCCESS;
    }

    if(parsed.positionals[0] == "help") {
        sPrintUsage(std::cout, &registry);
        return EXIT_SUCCESS;
    }

    const std::string moduleName = parsed.positionals[0];

    PToolModule* module = registry.find(moduleName);

    if(module == nullptr) {
        std::cerr << "unknown module: " << moduleName << "\n";
        registry.printModules(std::cerr);
        return EXIT_FAILURE;
    }

    std::string command = "help";

    if(parsed.positionals.size() >= 2) {
        command = parsed.positionals[1];
    }

    std::vector<std::string> commandArgs;

    if(parsed.positionals.size() > 2) {
        commandArgs.assign(parsed.positionals.begin() + 2,
                           parsed.positionals.end());
    }

    PToolContext ctx = sBuildContext();

    return module->run(command, commandArgs, ctx);
}
