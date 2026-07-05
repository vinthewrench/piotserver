//
// DS2482Tool.cpp
// piottool module for the DS2482 I2C-to-1Wire bridge.
//
// First-pass support is intentionally DS18B20-only.
//

#include "modules/DS2482/DS2482Tool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "PToolModuleAPI.hpp"


namespace {

static constexpr uint32_t DS2482_DEFAULT_ADDRESS = 0x18;

static void printAddress(uint8_t address, uint32_t bus)
{
    std::cout << "ds2482 @ bus "
              << bus
              << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";
}


static void printStatusBits(uint8_t status)
{
    std::cout << "status: 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(status)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "  1wb: " << ((status & 0x01) ? "yes" : "no") << "  1-Wire busy\n";
    std::cout << "  ppd: " << ((status & 0x02) ? "yes" : "no") << "  presence pulse detected\n";
    std::cout << "  sd:  " << ((status & 0x04) ? "yes" : "no") << "  short detected\n";
    std::cout << "  ll:  " << ((status & 0x08) ? "yes" : "no") << "  logic level\n";
    std::cout << "  rst: " << ((status & 0x10) ? "yes" : "no") << "  device reset\n";
    std::cout << "  sbr: " << ((status & 0x20) ? "yes" : "no") << "  single bit result\n";
    std::cout << "  tsb: " << ((status & 0x40) ? "yes" : "no") << "  triplet second bit\n";
    std::cout << "  dir: " << ((status & 0x80) ? "yes" : "no") << "  branch direction taken\n";
}


static int commandStatus(DS2482& device,
                         uint8_t address,
                         uint32_t bus)
{
    uint8_t status = 0;
    int error = 0;

    printAddress(address, bus);

    if(!device.readBridgeStatus(status, error)) {
        std::cerr << "ds2482: status failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    printStatusBits(status);
    return EXIT_SUCCESS;
}


static int commandProbe(DS2482& device,
                        uint8_t address,
                        uint32_t bus)
{
    std::vector<DS2482::Sensor> sensors;
    int error = 0;

    printAddress(address, bus);

    if(!device.probeDS18B20(sensors, error)) {
        std::cerr << "ds2482: probe failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    if(sensors.empty()) {
        std::cout << "DS18B20 sensors found: 0\n";
        return EXIT_SUCCESS;
    }

    std::cout << "DS18B20 sensors found: "
              << sensors.size()
              << "\n";

    for(const auto& sensor : sensors) {
        std::cout << "  "
                  << DS2482::romToString(sensor.rom)
                  << "\n";
    }

    return EXIT_SUCCESS;
}


static int commandTempAll(DS2482& device,
                          uint8_t address,
                          uint32_t bus)
{
    std::vector<DS2482::Temperature> temperatures;
    int error = 0;

    printAddress(address, bus);

    if(!device.readTemps(temperatures, error)) {
        std::cerr << "ds2482: temperature read failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    if(temperatures.empty()) {
        std::cout << "DS18B20 sensors found: 0\n";
        return EXIT_SUCCESS;
    }

    std::cout << "DS18B20 temperatures:\n";

    int exitCode = EXIT_SUCCESS;

    for(const auto& reading : temperatures) {
        std::cout << "  "
                  << DS2482::romToString(reading.rom)
                  << "   ";

        if(!reading.success) {
            std::cout << "error";

            if(!reading.errorText.empty()) {
                std::cout << ": " << reading.errorText;
            }

            std::cout << "\n";
            exitCode = EXIT_FAILURE;
            continue;
        }

        std::cout << std::fixed << std::setprecision(2)
                  << reading.tempC
                  << " C   "
                  << reading.tempF
                  << " F";

        if(reading.tempC == 85.0f) {
            std::cout << "   warning: 85.00 C can mean conversion did not complete";
            exitCode = EXIT_FAILURE;
        }

        std::cout << "\n";
    }

    return exitCode;
}


static int commandTempOne(DS2482& device,
                          uint8_t address,
                          uint32_t bus,
                          const std::string& romText)
{
    std::array<uint8_t, 8> rom = {};

    printAddress(address, bus);

    if(!DS2482::stringToRom(romText, rom)) {
        std::cerr << "ds2482: invalid ROM: "
                  << romText
                  << "\n";
        std::cerr << "expected format like: 28-ff-64-1d-62-15-03-a2\n";
        return EXIT_FAILURE;
    }

    DS2482::Temperature reading;
    int error = 0;

    if(!device.readTemp(rom, reading, error)) {
        std::cerr << "ds2482: temperature read failed for "
                  << DS2482::romToString(rom);

        if(error != 0 || errno != 0) {
            std::cerr << ": "
                      << std::strerror(error ? error : errno);
        }

        if(!reading.errorText.empty()) {
            std::cerr << ": "
                      << reading.errorText;
        }

        std::cerr << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "DS18B20 temperature:\n";
    std::cout << "  "
              << DS2482::romToString(reading.rom)
              << "   ";

    if(!reading.success) {
        std::cout << "error";

        if(!reading.errorText.empty()) {
            std::cout << ": " << reading.errorText;
        }

        std::cout << "\n";
        return EXIT_FAILURE;
    }

    std::cout << std::fixed << std::setprecision(2)
              << reading.tempC
              << " C   "
              << reading.tempF
              << " F";

    if(reading.tempC == 85.0f) {
        std::cout << "   warning: 85.00 C can mean conversion did not complete";
        std::cout << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "\n";
    return EXIT_SUCCESS;
}

} // namespace


const char* DS2482Tool::name() const
{
    return "ds2482";
}


const char* DS2482Tool::description() const
{
    return "DS2482 I2C-to-1Wire bridge, DS18B20 temperature support";
}


bool DS2482Tool::hasDefaultAddress() const
{
    return true;
}


uint32_t DS2482Tool::defaultAddress() const
{
    return DS2482_DEFAULT_ADDRESS;
}


void DS2482Tool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "DS2482 module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool ds2482 <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x18\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show DS2482 help\n";
    out << "  status                Read DS2482 bridge status byte\n";
    out << "  probe                 Scan 1-Wire bus and list DS18B20 ROMs\n";
    out << "  temp                  Read all DS18B20 temperatures\n";
    out << "  temp <rom>            Read one DS18B20 by ROM\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool ds2482 status\n";
    out << "  piottool ds2482 probe\n";
    out << "  piottool ds2482 temp\n";
    out << "  piottool ds2482 temp 28-9d-74-46-d4-6c-0c-4d\n";
    out << "  piottool --addr 0x18 ds2482 probe\n";
    out << "\n";
    out << "Notes:\n";
    out << "  This module only supports DS18B20 temperature sensors.\n";
    out << "  DS18B20 ROM CRC and scratchpad CRC are checked.\n";
    out << "  Temperature conversion uses a fixed 750 ms wait for 12-bit conversion.\n";
    out << "  Externally powered DS18B20 sensors are assumed.\n";
    out << "\n";
}


void DS2482Tool::printCommandHelp(const std::string& command,
                                  std::ostream& out) const
{
    if(command == "status") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool ds2482 status\n";
        out << "\n";
        out << "Reads and decodes the DS2482 status byte.\n";
        out << "\n";
        return;
    }

    if(command == "probe") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool ds2482 probe\n";
        out << "\n";
        out << "Scans the 1-Wire bus and prints DS18B20 ROM IDs only.\n";
        out << "\n";
        return;
    }

    if(command == "temp") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool ds2482 temp\n";
        out << "  piottool ds2482 temp <rom>\n";
        out << "\n";
        out << "Reads all DS18B20 sensors, or one specific DS18B20 by ROM.\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for DS2482 command: "
        << command
        << "\n";
}


int DS2482Tool::run(const std::string& command,
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

    const uint32_t address32 = ctx.hasAddressOverride ? ctx.addressOverride : defaultAddress();

    if(address32 > 0x7F) {
        std::cerr << "ds2482: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase
                  << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    DS2482 device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "ds2482: begin failed at bus "
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

    if(command == "status") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = commandStatus(device, address, ctx.bus);
        device.stop();
        return exitCode;
    }

    if(command == "probe") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = commandProbe(device, address, ctx.bus);
        device.stop();
        return exitCode;
    }

    if(command == "temp") {
        if(args.empty()) {
            exitCode = commandTempAll(device, address, ctx.bus);
            device.stop();
            return exitCode;
        }

        if(args.size() == 1) {
            exitCode = commandTempOne(device, address, ctx.bus, args[0]);
            device.stop();
            return exitCode;
        }

        printCommandHelp(command, std::cerr);
        device.stop();
        return EXIT_FAILURE;
    }

    std::cerr << "ds2482: unknown command: "
              << command
              << "\n";
    std::cerr << "try: piottool ds2482 help\n";

    device.stop();
    return EXIT_FAILURE;
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new DS2482Tool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
