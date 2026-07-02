//
// MCP23008Tool.cpp
// piottool module for MCP23008.
//

#include "modules/MCP23008/MCP23008Tool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "PToolModuleAPI.hpp"


const char* MCP23008Tool::name() const
{
    return "mcp23008";
}


const char* MCP23008Tool::description() const
{
    return "MCP23008 8-bit I/O expander";
}


bool MCP23008Tool::hasDefaultAddress() const
{
    return true;
}


uint32_t MCP23008Tool::defaultAddress() const
{
    /*
     * Current farm MCP23008 board is at 0x27.
     * Override with --addr when testing another address strap.
     */
    return 0x27;
}


void MCP23008Tool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "MCP23008 module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool mcp23008 <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x27\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show MCP23008 help\n";
    out << "  status                Read and print bit states\n";
    out << "  read                  Same as status\n";
    out << "  bit <n> on            Turn bit n on, n = 0..7\n";
    out << "  bit <n> off           Turn bit n off, n = 0..7\n";
    out << "  write <hex>           Write output mask 0x00..0xFF\n";
    out << "  all-on                Turn all MCP23008 outputs on\n";
    out << "  all-off               Turn all MCP23008 outputs off\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool mcp23008 status\n";
    out << "  piottool mcp23008 bit 0 on\n";
    out << "  piottool mcp23008 bit 0 off\n";
    out << "  piottool mcp23008 bit 7 on\n";
    out << "  piottool mcp23008 write 0x00\n";
    out << "  piottool mcp23008 write 0xFF\n";
    out << "  piottool mcp23008 all-on\n";
    out << "  piottool mcp23008 all-off\n";
    out << "  piottool --addr 0x27 mcp23008 status\n";
    out << "\n";
    out << "Notes:\n";
    out << "  On open, this tool sets MCP23008 direction mask to 0x00 so bits 0..7 are outputs.\n";
    out << "  The tool uses the existing lower MCP23008 driver class.\n";
    out << "\n";
}


void MCP23008Tool::printCommandHelp(const std::string& command,
                                    std::ostream& out) const
{
    if(command == "bit") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool mcp23008 bit <n> <on|off>\n";
        out << "\n";
        out << "Bit numbers:\n";
        out << "  0..7\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool mcp23008 bit 0 on\n";
        out << "  piottool mcp23008 bit 0 off\n";
        out << "  piottool mcp23008 bit 7 on\n";
        out << "\n";
        return;
    }

    if(command == "write") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool mcp23008 write <hex>\n";
        out << "\n";
        out << "Writes all 8 output bits. Valid mask range is 0x00..0xFF.\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool mcp23008 write 0x00\n";
        out << "  piottool mcp23008 write 0x01\n";
        out << "  piottool mcp23008 write 0x80\n";
        out << "  piottool mcp23008 write 0xFF\n";
        out << "\n";
        return;
    }

    if(command == "status" || command == "read") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool mcp23008 status\n";
        out << "  piottool mcp23008 read\n";
        out << "\n";
        out << "Reads MCP23008 state and prints bits 0..7.\n";
        out << "\n";
        return;
    }

    if(command == "all-on" || command == "all-off") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool mcp23008 all-on\n";
        out << "  piottool mcp23008 all-off\n";
        out << "\n";
        out << "Writes all 8 MCP23008 output bits on or off.\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for MCP23008 command: " << command << "\n";
}


int MCP23008Tool::run(const std::string& command,
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
        std::cerr << "mcp23008: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    MCP23008 device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "mcp23008: begin failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    /*
     * MCP23008 IODIR semantics:
     *
     *   1 = input
     *   0 = output
     *
     * For bit flipping, make all 8 lines outputs.
     */
    if(!device.setGPIOdirection(MCP23008_OUTPUT_DIRECTION_MASK)) {
        std::cerr << "mcp23008: setGPIOdirection(0x00) failed: "
                  << std::strerror(errno)
                  << "\n";
        device.stop();
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_SUCCESS;

    if(command == "status" || command == "read") {
        MCP23008::pinStates_t states;

        if(device.getRelayStates(states)) {
            printStates(address, states);
        }
        else {
            std::cerr << "mcp23008: read states failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
    }
    else if(command == "bit") {
        if(args.size() != 2) {
            std::cerr << "mcp23008: usage: piottool mcp23008 bit <0..7> <on|off>\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            uint8_t bit = 0;
            bool state = false;

            if(!parseBitNumber(args[0], bit)) {
                std::cerr << "mcp23008: bit number must be 0..7\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!parseOnOff(args[1], state)) {
                std::cerr << "mcp23008: bit state must be on or off\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!setOneBit(device, bit, state)) {
                std::cerr << "mcp23008: bit " << static_cast<unsigned>(bit)
                          << " " << (state ? "on" : "off")
                          << " failed: "
                          << std::strerror(errno)
                          << "\n";
                exitCode = EXIT_FAILURE;
            }
            else {
                MCP23008::pinStates_t states;
                if(device.getRelayStates(states)) {
                    printStates(address, states);
                }
            }
        }
    }
    else if(command == "write") {
        if(args.size() != 1) {
            std::cerr << "mcp23008: usage: piottool mcp23008 write <0x00..0xFF>\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            uint8_t mask = 0;

            if(!parseHexByte(args[0], mask)) {
                std::cerr << "mcp23008: write mask must be hex 0x00..0xFF\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!writeMask(device, mask)) {
                std::cerr << "mcp23008: write 0x"
                          << std::hex << std::uppercase << std::setw(2)
                          << std::setfill('0') << static_cast<unsigned>(mask)
                          << std::dec << std::nouppercase << std::setfill(' ')
                          << " failed: "
                          << std::strerror(errno)
                          << "\n";
                exitCode = EXIT_FAILURE;
            }
            else {
                MCP23008::pinStates_t states;
                if(device.getRelayStates(states)) {
                    printStates(address, states);
                }
            }
        }
    }
    else if(command == "all-on") {
        if(!writeMask(device, MCP23008_ALL_BITS_MASK)) {
            std::cerr << "mcp23008: all-on failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            MCP23008::pinStates_t states;
            if(device.getRelayStates(states)) {
                printStates(address, states);
            }
        }
    }
    else if(command == "all-off") {
        if(!writeMask(device, 0x00)) {
            std::cerr << "mcp23008: all-off failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            MCP23008::pinStates_t states;
            if(device.getRelayStates(states)) {
                printStates(address, states);
            }
        }
    }
    else {
        std::cerr << "mcp23008: unknown command: " << command << "\n";
        std::cerr << "try: piottool mcp23008 help\n";
        exitCode = EXIT_FAILURE;
    }

    device.stop();

    return exitCode;
}


bool MCP23008Tool::parseBitNumber(const std::string& text,
                                  uint8_t& bitOut)
{
    if(text.size() != 1) {
        return false;
    }

    const char c = text[0];

    if(c < '0' || c > '7') {
        return false;
    }

    bitOut = static_cast<uint8_t>(c - '0');
    return true;
}


bool MCP23008Tool::parseOnOff(const std::string& text,
                              bool& stateOut)
{
    if(text == "on" || text == "ON" || text == "1" || text == "true" || text == "TRUE") {
        stateOut = true;
        return true;
    }

    if(text == "off" || text == "OFF" || text == "0" || text == "false" || text == "FALSE") {
        stateOut = false;
        return true;
    }

    return false;
}


bool MCP23008Tool::parseHexByte(const std::string& text,
                                uint8_t& valueOut)
{
    if(text.empty()) {
        return false;
    }

    std::string hexText = text;

    if(hexText.size() >= 2
       && hexText[0] == '0'
       && (hexText[1] == 'x' || hexText[1] == 'X')) {
        hexText = hexText.substr(2);
    }

    if(hexText.empty() || hexText.size() > 2) {
        return false;
    }

    unsigned value = 0;

    for(char c : hexText) {
        value <<= 4;

        if(c >= '0' && c <= '9') {
            value |= static_cast<unsigned>(c - '0');
        }
        else if(c >= 'a' && c <= 'f') {
            value |= static_cast<unsigned>(10 + (c - 'a'));
        }
        else if(c >= 'A' && c <= 'F') {
            value |= static_cast<unsigned>(10 + (c - 'A'));
        }
        else {
            return false;
        }
    }

    if(value > 0xFF) {
        return false;
    }

    valueOut = static_cast<uint8_t>(value);
    return true;
}


MCP23008::pinStates_t MCP23008Tool::statesFromMask(uint8_t mask)
{
    MCP23008::pinStates_t states;

    for(uint8_t bit = 0; bit < 8; bit++) {
        const bool state = ((mask >> bit) & 0x01) != 0;
        states.push_back(std::make_pair(bit, state));
    }

    return states;
}


bool MCP23008Tool::setOneBit(MCP23008& device,
                             uint8_t bit,
                             bool state)
{
    if(bit > 7) {
        return false;
    }

    MCP23008::pinStates_t states;
    states.push_back(std::make_pair(bit, state));

    return device.setRelayStates(states);
}


bool MCP23008Tool::writeMask(MCP23008& device,
                             uint8_t mask)
{
    return device.setRelayStates(statesFromMask(mask));
}


void MCP23008Tool::printStates(uint8_t address,
                               const MCP23008::pinStates_t& states)
{
    uint8_t mask = 0;

    for(const auto& state : states) {
        const uint8_t bit = state.first;
        const bool on = state.second;

        if(bit < 8 && on) {
            mask |= static_cast<uint8_t>(1U << bit);
        }
    }

    std::cout << "mcp23008 @ 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "mask: 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(mask)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    for(uint8_t bit = 0; bit < 8; bit++) {
        bool on = false;

        for(const auto& state : states) {
            if(state.first == bit) {
                on = state.second;
                break;
            }
        }

        std::cout << "bit " << static_cast<unsigned>(bit)
                  << ": " << (on ? "on" : "off")
                  << "\n";
    }
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new MCP23008Tool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
