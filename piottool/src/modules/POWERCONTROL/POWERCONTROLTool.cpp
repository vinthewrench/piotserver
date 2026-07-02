//
// POWERCONTROLTool.cpp
// piottool module for the POWERCONTROL I2C power controller.
//

#include "modules/POWERCONTROL/POWERCONTROLTool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "PToolModuleAPI.hpp"


#define POWERCONTROL_STATUS_RED_LED       0x02
#define POWERCONTROL_STATUS_GREEN_LED     0x04
#define POWERCONTROL_STATUS_AC_OK         0x08
#define POWERCONTROL_STATUS_WAKE_MASK     0x70

#define POWERCONTROL_COMMAND_CANCEL       'C'

#define POWERCONTROL_COMMAND_RED_ON       'R'
#define POWERCONTROL_COMMAND_RED_OFF      'r'

#define POWERCONTROL_COMMAND_GREEN_ON     'G'
#define POWERCONTROL_COMMAND_GREEN_OFF    'g'

#define POWERCONTROL_COMMAND_WAKE_CLEAR   '0'
#define POWERCONTROL_COMMAND_WAKE_1_MIN   '1'
#define POWERCONTROL_COMMAND_WAKE_5_MIN   '5'
#define POWERCONTROL_COMMAND_WAKE_15_MIN  'F'
#define POWERCONTROL_COMMAND_WAKE_60_MIN  'H'
#define POWERCONTROL_COMMAND_WAKE_8_HOUR  '8'
#define POWERCONTROL_COMMAND_WAKE_24_HOUR 'D'


namespace {

static bool commandForWakeArg(const std::string& arg,
                              uint8_t& command,
                              uint16_t& minutes)
{
    if(arg == "clear" || arg == "off" || arg == "0") {
        command = POWERCONTROL_COMMAND_WAKE_CLEAR;
        minutes = 0;
        return true;
    }

    if(arg == "1m" || arg == "1min" || arg == "1") {
        command = POWERCONTROL_COMMAND_WAKE_1_MIN;
        minutes = 1;
        return true;
    }

    if(arg == "5m" || arg == "5min" || arg == "5") {
        command = POWERCONTROL_COMMAND_WAKE_5_MIN;
        minutes = 5;
        return true;
    }

    if(arg == "15m" || arg == "15min" || arg == "15") {
        command = POWERCONTROL_COMMAND_WAKE_15_MIN;
        minutes = 15;
        return true;
    }

    if(arg == "60m" || arg == "60min" || arg == "1h" || arg == "60") {
        command = POWERCONTROL_COMMAND_WAKE_60_MIN;
        minutes = 60;
        return true;
    }

    if(arg == "8h" || arg == "8hour" || arg == "8hours") {
        command = POWERCONTROL_COMMAND_WAKE_8_HOUR;
        minutes = 480;
        return true;
    }

    if(arg == "24h" || arg == "24hour" || arg == "24hours" || arg == "1d") {
        command = POWERCONTROL_COMMAND_WAKE_24_HOUR;
        minutes = 1440;
        return true;
    }

    return false;
}


static void printStatus(uint8_t address,
                        uint32_t bus,
                        const POWERCONTROL::POWERCONTROL_data& data)
{
    const bool redOn = (data.statusByte & POWERCONTROL_STATUS_RED_LED) != 0;
    const bool greenOn = (data.statusByte & POWERCONTROL_STATUS_GREEN_LED) != 0;

    std::cout << "power @ bus "
              << bus
              << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "status: 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(data.statusByte)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "  ac_ok:          "
              << (data.acOK ? "yes" : "no")
              << "\n";

    std::cout << "  red_led:        "
              << (redOn ? "on" : "off")
              << "\n";

    std::cout << "  green_led:      "
              << (greenOn ? "on" : "off")
              << "\n";

    std::cout << "  wake_timer_min: "
              << data.wakeTimerMin
              << "\n";
}


static void printRead(uint8_t address,
                      uint32_t bus,
                      const POWERCONTROL::POWERCONTROL_data& data)
{
    std::cout << "power @ bus "
              << bus
              << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "ac_ok: "
              << (data.acOK ? "yes" : "no")
              << "\n";

    std::cout << "wake_timer_min: "
              << data.wakeTimerMin
              << "\n";
}


static bool readAndPrintStatus(POWERCONTROL& device,
                               uint8_t address,
                               uint32_t bus,
                               bool verbose)
{
    POWERCONTROL::POWERCONTROL_data data = {};

    if(!device.readStatus(data)) {
        std::cerr << "power: readStatus failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(errno)
                  << "\n";
        return false;
    }

    if(verbose) {
        printStatus(address, bus, data);
    }
    else {
        printRead(address, bus, data);
    }

    return true;
}


static int sendSimpleCommand(POWERCONTROL& device,
                             const char* successText,
                             uint8_t command)
{
    if(!device.sendCommand(command)) {
        std::cerr << "power: command failed: "
                  << successText
                  << "\n";
        return EXIT_FAILURE;
    }

    std::cout << successText << "\n";
    return EXIT_SUCCESS;
}

} // namespace


const char* POWERCONTROLTool::name() const
{
    return "power";
}


const char* POWERCONTROLTool::description() const
{
    return "POWERCONTROL I2C power controller";
}


bool POWERCONTROLTool::hasDefaultAddress() const
{
    return true;
}


uint32_t POWERCONTROLTool::defaultAddress() const
{
    return POWERCONTROL::POWERCONTROL_DEFAULT_ADDR;
}


void POWERCONTROLTool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "POWERCONTROL module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool power <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x08\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show POWERCONTROL help\n";
    out << "  read                  Read compact power status\n";
    out << "  status                Read full decoded status byte\n";
    out << "  shutdown              Request delayed shutdown from POWERCONTROL\n";
    out << "  cancel                Cancel delayed shutdown\n";
    out << "  wake clear            Clear wake timer preset\n";
    out << "  wake 1m               Set wake timer preset to 1 minute\n";
    out << "  wake 5m               Set wake timer preset to 5 minutes\n";
    out << "  wake 15m              Set wake timer preset to 15 minutes\n";
    out << "  wake 60m              Set wake timer preset to 60 minutes\n";
    out << "  wake 8h               Set wake timer preset to 8 hours\n";
    out << "  wake 24h              Set wake timer preset to 24 hours\n";
    out << "  red on                Turn red LED on\n";
    out << "  red off               Turn red LED off\n";
    out << "  green on              Turn green LED on\n";
    out << "  green off             Turn green LED off\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool power read\n";
    out << "  piottool power status\n";
    out << "  piottool power shutdown\n";
    out << "  piottool power cancel\n";
    out << "  piottool power wake 8h\n";
    out << "  piottool --addr 0x08 power status\n";
    out << "\n";
    out << "Notes:\n";
    out << "  POWERCONTROL firmware v26 uses a one-byte protocol.\n";
    out << "  Reads are bare one-byte I2C reads, not register-pointer reads.\n";
    out << "  Writes are explicit one-byte command writes.\n";
    out << "\n";
}


void POWERCONTROLTool::printCommandHelp(const std::string& command,
                                        std::ostream& out) const
{
    if(command == "read" || command == "status") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power read\n";
        out << "  piottool power status\n";
        out << "\n";
        out << "Reads POWERCONTROL using the existing lower POWERCONTROL driver.\n";
        out << "\n";
        return;
    }

    if(command == "shutdown") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power shutdown\n";
        out << "\n";
        out << "Requests delayed shutdown from POWERCONTROL using command byte 'S'.\n";
        out << "\n";
        return;
    }

    if(command == "cancel") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power cancel\n";
        out << "\n";
        out << "Cancels delayed shutdown using command byte 'C'.\n";
        out << "\n";
        return;
    }

    if(command == "wake") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power wake clear\n";
        out << "  piottool power wake 1m\n";
        out << "  piottool power wake 5m\n";
        out << "  piottool power wake 15m\n";
        out << "  piottool power wake 60m\n";
        out << "  piottool power wake 8h\n";
        out << "  piottool power wake 24h\n";
        out << "\n";
        out << "Sets the stored POWERCONTROL wake preset.\n";
        out << "\n";
        return;
    }

    if(command == "red") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power red on\n";
        out << "  piottool power red off\n";
        out << "\n";
        return;
    }

    if(command == "green") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool power green on\n";
        out << "  piottool power green off\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for POWERCONTROL command: "
        << command
        << "\n";
}


int POWERCONTROLTool::run(const std::string& command,
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
        std::cerr << "power: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase
                  << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    POWERCONTROL device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "power: begin failed at bus "
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

    if(command == "read") {
        exitCode = readAndPrintStatus(device,
                                      address,
                                      ctx.bus,
                                      false) ? EXIT_SUCCESS : EXIT_FAILURE;
        device.stop();
        return exitCode;
    }

    if(command == "status") {
        exitCode = readAndPrintStatus(device,
                                      address,
                                      ctx.bus,
                                      true) ? EXIT_SUCCESS : EXIT_FAILURE;
        device.stop();
        return exitCode;
    }

    std::cout << "power @ bus "
              << ctx.bus
              << " addr 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    if(command == "shutdown") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(!device.requestDelayedShutdown()) {
            std::cerr << "power: shutdown request failed\n";
            device.stop();
            return EXIT_FAILURE;
        }

        std::cout << "shutdown: requested\n";
        device.stop();
        return EXIT_SUCCESS;
    }

    if(command == "cancel") {
        if(!args.empty()) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        exitCode = sendSimpleCommand(device,
                                     "shutdown: canceled",
                                     POWERCONTROL_COMMAND_CANCEL);

        device.stop();
        return exitCode;
    }

    if(command == "wake") {
        uint8_t wakeCommand = 0;
        uint16_t wakeMinutes = 0;

        if(args.size() != 1 ||
           !commandForWakeArg(args[0], wakeCommand, wakeMinutes)) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(!device.sendCommand(wakeCommand)) {
            std::cerr << "power: wake command failed\n";
            device.stop();
            return EXIT_FAILURE;
        }

        std::cout << "wake_timer_min: "
                  << wakeMinutes
                  << "\n";

        device.stop();
        return EXIT_SUCCESS;
    }

    if(command == "red") {
        if(args.size() != 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[0] == "on") {
            exitCode = sendSimpleCommand(device,
                                         "red_led: on",
                                         POWERCONTROL_COMMAND_RED_ON);
            device.stop();
            return exitCode;
        }

        if(args[0] == "off") {
            exitCode = sendSimpleCommand(device,
                                         "red_led: off",
                                         POWERCONTROL_COMMAND_RED_OFF);
            device.stop();
            return exitCode;
        }

        printCommandHelp(command, std::cerr);
        device.stop();
        return EXIT_FAILURE;
    }

    if(command == "green") {
        if(args.size() != 1) {
            printCommandHelp(command, std::cerr);
            device.stop();
            return EXIT_FAILURE;
        }

        if(args[0] == "on") {
            exitCode = sendSimpleCommand(device,
                                         "green_led: on",
                                         POWERCONTROL_COMMAND_GREEN_ON);
            device.stop();
            return exitCode;
        }

        if(args[0] == "off") {
            exitCode = sendSimpleCommand(device,
                                         "green_led: off",
                                         POWERCONTROL_COMMAND_GREEN_OFF);
            device.stop();
            return exitCode;
        }

        printCommandHelp(command, std::cerr);
        device.stop();
        return EXIT_FAILURE;
    }

    std::cerr << "power: unknown command: "
              << command
              << "\n";
    std::cerr << "try: piottool power help\n";

    device.stop();
    return EXIT_FAILURE;
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new POWERCONTROLTool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
