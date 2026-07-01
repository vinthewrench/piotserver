//
// TMP10XTool.cpp
// piottool module for TMP10X temperature sensors.
//

#include "modules/TMP10X/TMP10XTool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "PToolModuleAPI.hpp"


const char* TMP10XTool::name() const
{
    return "tmp10x";
}


const char* TMP10XTool::description() const
{
    return "TMP10X I2C temperature sensor";
}


bool TMP10XTool::hasDefaultAddress() const
{
    return true;
}


uint32_t TMP10XTool::defaultAddress() const
{
    /*
     * Current farm board has TMP10X at 0x4F.
     * Override with --addr when testing another address strap.
     */
    return 0x4F;
}


void TMP10XTool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "TMP10X module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool tmp10x <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x4F\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show TMP10X help\n";
    out << "  read                  Read temperature\n";
    out << "  status                Same as read\n";
    out << "  temp                  Same as read\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool tmp10x read\n";
    out << "  piottool tmp10x status\n";
    out << "  piottool --bus 1 --addr 0x4F tmp10x read\n";
    out << "  piottool --json tmp10x read\n";
    out << "\n";
    out << "Notes:\n";
    out << "  This module uses the existing lower TMP10X driver class.\n";
    out << "  It does not read /dev/i2c directly from the tool module.\n";
    out << "\n";
}


void TMP10XTool::printCommandHelp(const std::string& command,
                                  std::ostream& out) const
{
    if(command == "read" || command == "status" || command == "temp") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool tmp10x read\n";
        out << "  piottool tmp10x status\n";
        out << "  piottool tmp10x temp\n";
        out << "\n";
        out << "Reads temperature using the existing TMP10X lower driver.\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool tmp10x read\n";
        out << "  piottool --addr 0x4F tmp10x read\n";
        out << "  piottool --json tmp10x read\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for TMP10X command: " << command << "\n";
}


int TMP10XTool::run(const std::string& command,
                    const std::vector<std::string>& args,
                    PToolContext& ctx)
{
    if(command == "help") {
        if(args.empty()) {
            printHelp(std::cout);
        }
        else {
            printCommandHelp(args[0], std::cout);
        }

        return EXIT_SUCCESS;
    }

    if(!args.empty()) {
        std::cerr << "tmp10x: command does not take extra arguments: "
                  << command << "\n";
        return EXIT_FAILURE;
    }

    if(command != "read" && command != "status" && command != "temp") {
        std::cerr << "tmp10x: unknown command: " << command << "\n";
        std::cerr << "try: piottool tmp10x help\n";
        return EXIT_FAILURE;
    }

    const uint32_t address32 = ctx.hasAddressOverride ? ctx.addressOverride : defaultAddress();

    if(address32 > 0x7F) {
        std::cerr << "tmp10x: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    TMP10X device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "tmp10x: begin failed at bus "
                  << ctx.bus
                  << " address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    float tempC = 0.0F;
    float tempF = 0.0F;

    if(!device.readTempC(tempC)) {
        std::cerr << "tmp10x: readTempC failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(errno)
                  << "\n";
        device.stop();
        return EXIT_FAILURE;
    }

    if(!device.readTempF(tempF)) {
        std::cerr << "tmp10x: readTempF failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(errno)
                  << "\n";
        device.stop();
        return EXIT_FAILURE;
    }

    printTemperature(address, ctx.bus, tempC, tempF, ctx.json);

    device.stop();

    return EXIT_SUCCESS;
}


void TMP10XTool::printTemperature(uint8_t address,
                                  uint32_t bus,
                                  float tempC,
                                  float tempF,
                                  bool json)
{
    if(json) {
        std::cout << "{";
        std::cout << "\"module\":\"tmp10x\",";
        std::cout << "\"bus\":" << bus << ",";
        std::cout << "\"address\":\"0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ') << "\",";
        std::cout << "\"temperature_c\":" << std::fixed << std::setprecision(4) << tempC << ",";
        std::cout << "\"temperature_f\":" << std::fixed << std::setprecision(4) << tempF;
        std::cout << "}\n";
        return;
    }

    std::cout << "tmp10x @ bus " << bus << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "temperature: "
              << std::fixed << std::setprecision(2) << tempC << " C"
              << " / "
              << std::fixed << std::setprecision(2) << tempF << " F"
              << "\n";
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new TMP10XTool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
