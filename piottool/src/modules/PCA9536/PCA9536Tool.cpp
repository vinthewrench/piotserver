//
// PCA9536Tool.cpp
// piottool module for PCA9536.
//

#include "modules/PCA9536/PCA9536Tool.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "PToolModuleAPI.hpp"


const char* PCA9536Tool::name() const
{
    return "pca9536";
}


const char* PCA9536Tool::description() const
{
    return "PCA9536 4-bit I/O expander / relay module";
}


bool PCA9536Tool::hasDefaultAddress() const
{
    return true;
}


uint32_t PCA9536Tool::defaultAddress() const
{
    return 0x41;
}


void PCA9536Tool::printHelp(std::ostream& out) const
{
    out << "\n";
    out << "PCA9536 module\n";
    out << "\n";
    out << "Usage:\n";
    out << "  piottool pca9536 <command> [args]\n";
    out << "\n";
    out << "Default I2C address:\n";
    out << "  0x41\n";
    out << "\n";
    out << "Current hardware:\n";
    out << "  NCD PCA9536 relay module\n";
    out << "\n";
    out << "Bit / relay mapping:\n";
    out << "  bit 0 -> available on 4-bit/4-output PCA9536 hardware\n";
    out << "  bit 1 -> relay 1 on current NCD board\n";
    out << "  bit 2 -> relay 2 on current NCD board\n";
    out << "  bit 3 -> available on 4-bit/4-output PCA9536 hardware\n";
    out << "\n";
    out << "Commands:\n";
    out << "  help                  Show PCA9536 help\n";
    out << "  status                Read and print bit states\n";
    out << "  read                  Same as status\n";
    out << "  bit <n> on            Turn bit n on, n = 0..3\n";
    out << "  bit <n> off           Turn bit n off, n = 0..3\n";
    out << "  relay <n> on          Turn relay n on, n = 1 or 2\n";
    out << "  relay <n> off         Turn relay n off, n = 1 or 2\n";
    out << "  write <hex>           Write output mask 0x0..0xF\n";
    out << "  all-on                Turn all 4 PCA9536 bits on\n";
    out << "  all-off               Turn all 4 PCA9536 bits off\n";
    out << "\n";
    out << "Examples:\n";
    out << "  piottool pca9536 status\n";
    out << "  piottool pca9536 bit 1 on\n";
    out << "  piottool pca9536 bit 1 off\n";
    out << "  piottool pca9536 relay 1 on\n";
    out << "  piottool pca9536 relay 2 off\n";
    out << "  piottool pca9536 write 0x06\n";
    out << "  piottool pca9536 write 0x0F\n";
    out << "  piottool pca9536 all-off\n";
    out << "  piottool --addr 0x41 pca9536 status\n";
    out << "\n";
    out << "Notes:\n";
    out << "  On open, this tool sets PCA9536 direction mask to 0x00 so bits 0..3 are outputs.\n";
    out << "  The tool uses the existing lower PCA9536 driver class.\n";
    out << "\n";
}


void PCA9536Tool::printCommandHelp(const std::string& command,
                                   std::ostream& out) const
{
    if(command == "bit") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool pca9536 bit <n> <on|off>\n";
        out << "\n";
        out << "Bit numbers:\n";
        out << "  0..3\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool pca9536 bit 0 on\n";
        out << "  piottool pca9536 bit 0 off\n";
        out << "  piottool pca9536 bit 3 on\n";
        out << "\n";
        return;
    }

    if(command == "relay") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool pca9536 relay <n> <on|off>\n";
        out << "\n";
        out << "Relay numbers:\n";
        out << "  1..2\n";
        out << "\n";
        out << "Current NCD board:\n";
        out << "  relay 1 -> bit 1\n";
        out << "  relay 2 -> bit 2\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool pca9536 relay 1 on\n";
        out << "  piottool pca9536 relay 1 off\n";
        out << "  piottool pca9536 relay 2 on\n";
        out << "\n";
        return;
    }

    if(command == "write") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool pca9536 write <hex>\n";
        out << "\n";
        out << "Writes all 4 output bits. Valid mask range is 0x0..0xF.\n";
        out << "\n";
        out << "Examples:\n";
        out << "  piottool pca9536 write 0x00\n";
        out << "  piottool pca9536 write 0x02\n";
        out << "  piottool pca9536 write 0x04\n";
        out << "  piottool pca9536 write 0x06\n";
        out << "  piottool pca9536 write 0x0F\n";
        out << "\n";
        return;
    }

    if(command == "status" || command == "read") {
        out << "\n";
        out << "Usage:\n";
        out << "  piottool pca9536 status\n";
        out << "  piottool pca9536 read\n";
        out << "\n";
        out << "Reads PCA9536 input state and prints bits 0..3.\n";
        out << "\n";
        return;
    }

    out << "No command-specific help for PCA9536 command: " << command << "\n";
}


int PCA9536Tool::run(const std::string& command,
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
        std::cerr << "pca9536: invalid I2C address: 0x"
                  << std::hex << std::uppercase << address32
                  << std::dec << std::nouppercase << "\n";
        return EXIT_FAILURE;
    }

    const uint8_t address = static_cast<uint8_t>(address32);

    PCA9536 device;
    int error = 0;

    if(!device.begin(address, error)) {
        std::cerr << "pca9536: begin failed at address 0x"
                  << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(address)
                  << std::dec << std::nouppercase << std::setfill(' ')
                  << ": " << std::strerror(error ? error : errno)
                  << "\n";
        return EXIT_FAILURE;
    }

    /*
     * PCA9536 config register semantics:
     *
     *   1 = input
     *   0 = output
     *
     * The lower driver softReset() sets 0x0F for all inputs.
     * For bit flipping, make all 4 lines outputs.
     */
    if(!device.setGPIOdirection(PCA9536_OUTPUT_DIRECTION_MASK)) {
        std::cerr << "pca9536: setGPIOdirection(0x00) failed: "
                  << std::strerror(errno)
                  << "\n";
        device.stop();
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_SUCCESS;

    if(command == "status" || command == "read") {
        PCA9536::pinStates_t states;

        if(device.getRelayStates(states)) {
            printStates(address, states);
        }
        else {
            std::cerr << "pca9536: read states failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
    }
    else if(command == "bit") {
        if(args.size() != 2) {
            std::cerr << "pca9536: usage: piottool pca9536 bit <0..3> <on|off>\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            uint8_t bit = 0;
            bool state = false;

            if(!parseBitNumber(args[0], bit)) {
                std::cerr << "pca9536: bit number must be 0, 1, 2, or 3\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!parseOnOff(args[1], state)) {
                std::cerr << "pca9536: bit state must be on or off\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!setOneBit(device, bit, state)) {
                std::cerr << "pca9536: bit " << static_cast<unsigned>(bit)
                          << " " << (state ? "on" : "off")
                          << " failed: "
                          << std::strerror(errno)
                          << "\n";
                exitCode = EXIT_FAILURE;
            }
            else {
                PCA9536::pinStates_t states;
                if(device.getRelayStates(states)) {
                    printStates(address, states);
                }
            }
        }
    }
    else if(command == "relay") {
        if(args.size() != 2) {
            std::cerr << "pca9536: usage: piottool pca9536 relay <1|2> <on|off>\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            uint8_t bit = 0;
            bool state = false;

            if(!parseRelayNumber(args[0], bit)) {
                std::cerr << "pca9536: relay number must be 1 or 2\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!parseOnOff(args[1], state)) {
                std::cerr << "pca9536: relay state must be on or off\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!setOneBit(device, bit, state)) {
                std::cerr << "pca9536: relay " << args[0]
                          << " " << (state ? "on" : "off")
                          << " failed: "
                          << std::strerror(errno)
                          << "\n";
                exitCode = EXIT_FAILURE;
            }
            else {
                PCA9536::pinStates_t states;
                if(device.getRelayStates(states)) {
                    printStates(address, states);
                }
            }
        }
    }
    else if(command == "write") {
        if(args.size() != 1) {
            std::cerr << "pca9536: usage: piottool pca9536 write <0x0..0xF>\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            uint8_t mask = 0;

            if(!parseHexNibble(args[0], mask)) {
                std::cerr << "pca9536: write mask must be hex 0x0..0xF\n";
                exitCode = EXIT_FAILURE;
            }
            else if(!writeMask(device, mask)) {
                std::cerr << "pca9536: write 0x"
                          << std::hex << std::uppercase << static_cast<unsigned>(mask)
                          << std::dec << std::nouppercase
                          << " failed: "
                          << std::strerror(errno)
                          << "\n";
                exitCode = EXIT_FAILURE;
            }
            else {
                PCA9536::pinStates_t states;
                if(device.getRelayStates(states)) {
                    printStates(address, states);
                }
            }
        }
    }
    else if(command == "all-on") {
        if(!writeMask(device, PCA9536_ALL_BITS_MASK)) {
            std::cerr << "pca9536: all-on failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            PCA9536::pinStates_t states;
            if(device.getRelayStates(states)) {
                printStates(address, states);
            }
        }
    }
    else if(command == "all-off") {
        if(!writeMask(device, 0x00)) {
            std::cerr << "pca9536: all-off failed: "
                      << std::strerror(errno)
                      << "\n";
            exitCode = EXIT_FAILURE;
        }
        else {
            PCA9536::pinStates_t states;
            if(device.getRelayStates(states)) {
                printStates(address, states);
            }
        }
    }
    else {
        std::cerr << "pca9536: unknown command: " << command << "\n";
        std::cerr << "try: piottool pca9536 help\n";
        exitCode = EXIT_FAILURE;
    }

    device.stop();

    return exitCode;
}


bool PCA9536Tool::parseBitNumber(const std::string& text,
                                 uint8_t& bitOut)
{
    if(text == "0") {
        bitOut = 0;
        return true;
    }

    if(text == "1") {
        bitOut = 1;
        return true;
    }

    if(text == "2") {
        bitOut = 2;
        return true;
    }

    if(text == "3") {
        bitOut = 3;
        return true;
    }

    return false;
}


bool PCA9536Tool::parseRelayNumber(const std::string& text,
                                   uint8_t& bitOut)
{
    if(text == "1") {
        bitOut = 1;
        return true;
    }

    if(text == "2") {
        bitOut = 2;
        return true;
    }

    return false;
}


bool PCA9536Tool::parseOnOff(const std::string& text,
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


bool PCA9536Tool::parseHexNibble(const std::string& text,
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

    if(hexText.empty() || hexText.size() > 1) {
        return false;
    }

    const char c = hexText[0];

    uint8_t value = 0;

    if(c >= '0' && c <= '9') {
        value = static_cast<uint8_t>(c - '0');
    }
    else if(c >= 'a' && c <= 'f') {
        value = static_cast<uint8_t>(10 + (c - 'a'));
    }
    else if(c >= 'A' && c <= 'F') {
        value = static_cast<uint8_t>(10 + (c - 'A'));
    }
    else {
        return false;
    }

    if(value > 0x0F) {
        return false;
    }

    valueOut = value;
    return true;
}


PCA9536::pinStates_t PCA9536Tool::statesFromMask(uint8_t mask)
{
    mask &= PCA9536_ALL_BITS_MASK;

    PCA9536::pinStates_t states;

    for(uint8_t bit = 0; bit < 4; bit++) {
        const bool state = ((mask >> bit) & 0x01) != 0;
        states.push_back(std::make_pair(bit, state));
    }

    return states;
}


bool PCA9536Tool::setOneBit(PCA9536& device,
                            uint8_t bit,
                            bool state)
{
    if(bit > 3) {
        return false;
    }

    PCA9536::pinStates_t states;
    states.push_back(std::make_pair(bit, state));

    return device.setRelayStates(states);
}


bool PCA9536Tool::writeMask(PCA9536& device,
                            uint8_t mask)
{
    return device.setRelayStates(statesFromMask(mask));
}


bool PCA9536Tool::readMask(PCA9536& device,
                           uint8_t& maskOut)
{
    PCA9536::pinStates_t states;

    if(!device.getRelayStates(states)) {
        return false;
    }

    uint8_t mask = 0;

    for(const auto& state : states) {
        const uint8_t bit = state.first;
        const bool on = state.second;

        if(bit < 4 && on) {
            mask |= static_cast<uint8_t>(1U << bit);
        }
    }

    maskOut = mask & PCA9536_ALL_BITS_MASK;
    return true;
}


void PCA9536Tool::printStates(uint8_t address,
                              const PCA9536::pinStates_t& states)
{
    uint8_t mask = 0;

    for(const auto& state : states) {
        const uint8_t bit = state.first;
        const bool on = state.second;

        if(bit < 4 && on) {
            mask |= static_cast<uint8_t>(1U << bit);
        }
    }

    std::cout << "pca9536 @ 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned>(address)
              << std::dec << std::nouppercase << std::setfill(' ')
              << "\n";

    std::cout << "mask: 0x"
              << std::hex << std::uppercase << static_cast<unsigned>(mask & PCA9536_ALL_BITS_MASK)
              << std::dec << std::nouppercase
              << "\n";

    for(const auto& state : states) {
        const uint8_t bit = state.first;
        const bool on = state.second;

        if(bit < 4) {
            std::cout << "bit " << static_cast<unsigned>(bit)
                      << ": " << (on ? "on" : "off");

            if(bit == 1) {
                std::cout << "  relay 1 on current NCD board";
            }
            else if(bit == 2) {
                std::cout << "  relay 2 on current NCD board";
            }

            std::cout << "\n";
        }
    }
}


extern "C" uint32_t ptoolModuleAPIVersion()
{
    return PTOOL_MODULE_API_VERSION;
}


extern "C" PToolModule* createPToolModule()
{
    return new PCA9536Tool();
}


extern "C" void destroyPToolModule(PToolModule* module)
{
    delete module;
}
