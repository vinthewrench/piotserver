//
// SHT25Tool.cpp
// piottool module for SHT25.
//

#include "modules/SHT25/SHT25Tool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "PToolModuleAPI.hpp"


const char* SHT25Tool::name() const
{
    return "sht25";
}


const char* SHT25Tool::description() const
{
    return "SHT25 I2C temperature/humidity sensor";
}


bool SHT25Tool::hasDefaultAddress() const
{
    return true;
}


uint32_t SHT25Tool::defaultAddress() const
{
    /*
     * SHT25 fixed/default address.
     * Current farm cooler sensor is at 0x40.
     */
    return SHT25::SHT25_DEFAULT_ADDR;
}


void SHT25Tool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "SHT25 module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool sht25 <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x40\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show SHT25 help\n";
    out << "  read                  Read temperature and humidity\n";
    out << "  status                Same as read\n";
    out << "  serial                Read serial number\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool sht25 read\n";
    out << "  piottool sht25 status\n";
    out << "  piottool sht25 serial\n";
    out << "  piottool --bus 1 --addr 0x40 sht25 read\n";
    out << "  piottool --json sht25 read\n";
    out << "  piottool --json sht25 serial\n";
    out << "\n";
    out << "Notes:\n";
    out << "  This module uses the existing lower SHT25 driver class.\n";
    out << "  It does not read /dev/i2c directly from the tool module.\n";
    out << "\n";
}


void SHT25Tool::printCommandHelp(const std::string& command,
                                 std::ostream& out) const
{
    if(command == "read" || command == "status") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool sht25 read\n";
        out << "  piottool sht25 status\n";
        out << "\n";
        out << "Reads temperature and humidity using the existing SHT25 lower driver.\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool sht25 read\n";
        out << "  piottool --addr 0x40 sht25 read\n";
        out << "  piottool --json sht25 read\n";
        out << "\n";
        return;
    }

    if(command == "serial") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool sht25 serial\n";
        out << "\n";
        out << "Reads the SHT25 8-byte serial number using the existing SHT25 lower driver.\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool sht25 serial\n";
        out << "  piottool --addr 0x40 sht25 serial\n";
        out << "  piottool --json sht25 serial\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for SHT25 command: " << command << "\n";
}


int SHT25Tool::run(const std::string& command,
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
        std::cerr << "sht25: command does not take extra arguments: "
                  << command << "\n";
        return EXIT_FAILURE;
    }

    if(command != "read" && command != "status" && command != "serial") {
        std::cerr << "sht25: unknown command: " << command << "\n";
        std::cerr << "try: piottool sht25 help\n";
        return EXIT_FAILURE;
    }

    const uint32_t address32 = ctx.hasAddressOverride ? ctx.addressOverride : defaultAddress();

    if(address32 > 0x7F) {
        std::cerr << "sht25: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    SHT25 device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "sht25: begin failed at bus "
                  << ctx.bus
                  << " address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_SUCCESS;

    if(command == "read" || command == "status") {
        SHT25::SHT25_data data = {};

        if(!device.readSensor(data)) {
            std::cerr << "sht25: readSensor failed at address 0x"
                      << std::hex << std::uppercase << std::setw(2)
                      << std::setfill('0') << static_cast<unsigned>(address)
                      << std::dec << std::nouppercase << std::setfill(' ')
                      << ": " << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            printSensor(address, ctx.bus, data, ctx.json);
        }
    }
    else if(command == "serial") {
        uint8_t serialNo[8] = {};

        if(!device.readSerialNumber(serialNo)) {
            std::cerr << "sht25: readSerialNumber failed at address 0x"
                      << std::hex << std::uppercase << std::setw(2)
                      << std::setfill('0') << static_cast<unsigned>(address)
                      << std::dec << std::nouppercase << std::setfill(' ')
                      << ": " << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            printSerial(address, ctx.bus, serialNo, ctx.json);
        }
    }

    device.stop();

    return exitCode;
}


void SHT25Tool::printSensor(uint8_t address,
                            uint32_t bus,
                            const SHT25::SHT25_data& data,
                            bool json)
{
    const double tempF = (data.temperature * 9.0 / 5.0) + 32.0;

    if(json) {
        std::cout << "{";
        std::cout << "\"module\":\"sht25\",";
        std::cout << "\"bus\":" << bus << ",";
        std::cout << "\"address\":\"0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ') << "\",";
        std::cout << "\"temperature_c\":" << std::fixed << std::setprecision(4) << data.temperature << ",";
        std::cout << "\"temperature_f\":" << std::fixed << std::setprecision(4) << tempF << ",";
        std::cout << "\"humidity_percent\":" << std::fixed << std::setprecision(4) << data.humidity;
        std::cout << "}\n";
        return;
    }

    std::cout << "sht25 @ bus " << bus << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "temperature: "
              << std::fixed << std::setprecision(2) << data.temperature << " C"
              << " / "
              << std::fixed << std::setprecision(2) << tempF << " F"
              << "\n";

    std::cout << "humidity: "
              << std::fixed << std::setprecision(2) << data.humidity << " %"
              << "\n";
}


void SHT25Tool::printSerial(uint8_t address,
                            uint32_t bus,
                            const uint8_t serialNo[8],
                            bool json)
{
    if(json) {
        std::cout << "{";
        std::cout << "\"module\":\"sht25\",";
        std::cout << "\"bus\":" << bus << ",";
        std::cout << "\"address\":\"0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ') << "\",";
        std::cout << "\"serial\":\"";
        printSerialHex(std::cout, serialNo);
        std::cout << "\"";
        std::cout << "}\n";
        return;
    }

    std::cout << "sht25 @ bus " << bus << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "serial: ";
    printSerialHex(std::cout, serialNo);
    std::cout << "\n";
}


void SHT25Tool::printSerialHex(std::ostream& out,
                               const uint8_t serialNo[8])
{
    std::ios_base::fmtflags flags = out.flags();
    char fill = out.fill();

    for(size_t i = 0; i < 8; i++) {
        out << std::hex << std::uppercase << std::setw(2)
            << std::setfill('0') << static_cast<unsigned>(serialNo[i]);
    }

    out.flags(flags);
    out.fill(fill);
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new SHT25Tool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
