//
// CmdLineParser.cpp
// Shared command-line parser for piottool / pIoTServer.
//

#include "CmdLineParser.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>


static bool sStartsWith(const std::string& text, const char* prefix)
{
    if(prefix == nullptr) {
        return false;
    }

    const size_t prefixLen = std::strlen(prefix);

    if(text.size() < prefixLen) {
        return false;
    }

    return text.compare(0, prefixLen, prefix) == 0;
}


CmdLineParseResult CmdLineParser::parse(int argc,
                                         const char* const argv[],
                                         const CmdLineOption* options,
                                         size_t optionCount,
                                         bool allowPositionals)
{
    CmdLineParseResult result;

    bool stopOptionParsing = false;

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i] ? argv[i] : "";

        if(stopOptionParsing) {
            if(!allowPositionals) {
                result.ok = false;
                result.error = "unexpected positional argument: " + arg;
                return result;
            }

            result.positionals.push_back(arg);
            continue;
        }

        if(arg == "--") {
            stopOptionParsing = true;
            continue;
        }

        if(arg == "-h" || arg == "--help") {
            result.helpRequested = true;
            continue;
        }

        /*
         * Long option:
         *
         *   --name
         *   --name=value
         */
        if(sStartsWith(arg, "--")) {
            std::string nameAndMaybeValue = arg.substr(2);
            std::string name;
            std::string value;
            const std::string* valuePtr = nullptr;

            const size_t eqPos = nameAndMaybeValue.find('=');

            if(eqPos == std::string::npos) {
                name = nameAndMaybeValue;
            }
            else {
                name = nameAndMaybeValue.substr(0, eqPos);
                value = nameAndMaybeValue.substr(eqPos + 1);
                valuePtr = &value;
            }

            if(name.empty()) {
                result.ok = false;
                result.error = "invalid empty long option";
                return result;
            }

            const CmdLineOption* option = findLongOption(name, options, optionCount);

            if(option == nullptr) {
                result.ok = false;
                result.error = "unknown option: --" + name;
                return result;
            }

            if(optionNeedsValue(*option)) {
                if(valuePtr == nullptr) {
                    if(i + 1 >= argc) {
                        result.ok = false;
                        result.error = "missing value for option: --" + name;
                        return result;
                    }

                    value = argv[++i] ? argv[i] : "";
                    valuePtr = &value;
                }

                if(valuePtr->empty()) {
                    result.ok = false;
                    result.error = "empty value for option: --" + name;
                    return result;
                }
            }
            else {
                if(valuePtr != nullptr) {
                    result.ok = false;
                    result.error = "option does not take a value: --" + name;
                    return result;
                }
            }

            std::string applyError;
            if(!applyOption(*option, valuePtr, applyError)) {
                result.ok = false;
                result.error = applyError;
                return result;
            }

            continue;
        }

        /*
         * Short option:
         *
         *   -v
         *   -b 1
         *
         * Intentionally strict:
         *
         *   -verbose    invalid
         *   -b1         invalid for now
         *   -abc        invalid for now
         */
        if(arg.size() >= 2 && arg[0] == '-') {
            if(arg.size() != 2) {
                result.ok = false;
                result.error = "invalid short option syntax: " + arg;
                return result;
            }

            const char shortName = arg[1];

            const CmdLineOption* option = findShortOption(shortName, options, optionCount);

            if(option == nullptr) {
                result.ok = false;
                result.error = std::string("unknown option: -") + shortName;
                return result;
            }

            std::string value;
            const std::string* valuePtr = nullptr;

            if(optionNeedsValue(*option)) {
                if(i + 1 >= argc) {
                    result.ok = false;
                    result.error = std::string("missing value for option: -") + shortName;
                    return result;
                }

                value = argv[++i] ? argv[i] : "";

                if(value.empty()) {
                    result.ok = false;
                    result.error = std::string("empty value for option: -") + shortName;
                    return result;
                }

                valuePtr = &value;
            }

            std::string applyError;
            if(!applyOption(*option, valuePtr, applyError)) {
                result.ok = false;
                result.error = applyError;
                return result;
            }

            continue;
        }

        /*
         * Positional argument.
         */
        if(!allowPositionals) {
            result.ok = false;
            result.error = "unexpected positional argument: " + arg;
            return result;
        }

        result.positionals.push_back(arg);
    }

    return result;
}


void CmdLineParser::printUsage(std::ostream& out,
                               const char* programName,
                               const char* usageLine,
                               const CmdLineOption* options,
                               size_t optionCount)
{
    out << "\n";

    if(programName && *programName) {
        out << programName << "\n";
    }

    if(usageLine && *usageLine) {
        out << "\nUsage:\n  " << usageLine << "\n";
    }

    out << "\nOptions:\n";

    out << "  " << std::left << std::setw(32) << "-h, --help"
        << "Show help\n";

    for(size_t i = 0; i < optionCount; i++) {
        const CmdLineOption& option = options[i];

        std::ostringstream left;

        if(option.shortName != '\0') {
            left << "-" << option.shortName;
        }
        else {
            left << "  ";
        }

        if(option.longName && *option.longName) {
            if(option.shortName != '\0') {
                left << ", ";
            }
            else {
                left << "  ";
            }

            left << "--" << option.longName;
        }

        if(optionNeedsValue(option)) {
            left << " <" << (option.valueName ? option.valueName : "value") << ">";
        }

        out << "  " << std::left << std::setw(32) << left.str();

        if(option.help && *option.help) {
            out << option.help;
        }

        out << "\n";
    }

    out << "\n";
}


const CmdLineOption* CmdLineParser::findLongOption(const std::string& name,
                                                   const CmdLineOption* options,
                                                   size_t optionCount)
{
    if(options == nullptr) {
        return nullptr;
    }

    for(size_t i = 0; i < optionCount; i++) {
        if(options[i].longName == nullptr) {
            continue;
        }

        if(name == options[i].longName) {
            return &options[i];
        }
    }

    return nullptr;
}


const CmdLineOption* CmdLineParser::findShortOption(char name,
                                                    const CmdLineOption* options,
                                                    size_t optionCount)
{
    if(options == nullptr || name == '\0') {
        return nullptr;
    }

    for(size_t i = 0; i < optionCount; i++) {
        if(options[i].shortName == name) {
            return &options[i];
        }
    }

    return nullptr;
}


bool CmdLineParser::optionNeedsValue(const CmdLineOption& option)
{
    switch(option.type) {
        case CmdLineOptionType::Boolean:
        case CmdLineOptionType::Count:
            return false;

        case CmdLineOptionType::String:
        case CmdLineOptionType::UInt:
        case CmdLineOptionType::HexUInt:
            return true;

        default:
            return false;
    }
}


bool CmdLineParser::applyOption(const CmdLineOption& option,
                                const std::string* value,
                                std::string& error)
{
    if(option.target == nullptr) {
        error = "internal error: option has null target: " + optionDisplayName(option);
        return false;
    }

    switch(option.type) {
        case CmdLineOptionType::Boolean:
            if(value != nullptr) {
                error = "option does not take a value: " + optionDisplayName(option);
                return false;
            }

            *static_cast<bool*>(option.target) = true;
            return true;

        case CmdLineOptionType::Count:
            if(value != nullptr) {
                error = "option does not take a value: " + optionDisplayName(option);
                return false;
            }

            (*static_cast<int*>(option.target))++;
            return true;

        case CmdLineOptionType::String:
            if(value == nullptr) {
                error = "missing value for option: " + optionDisplayName(option);
                return false;
            }

            *static_cast<std::string*>(option.target) = *value;
            return true;

        case CmdLineOptionType::UInt:
        {
            if(value == nullptr) {
                error = "missing value for option: " + optionDisplayName(option);
                return false;
            }

            uint32_t parsed = 0;
            if(!parseUInt(*value, parsed)) {
                error = "invalid unsigned integer for option " + optionDisplayName(option) + ": " + *value;
                return false;
            }

            *static_cast<uint32_t*>(option.target) = parsed;
            return true;
        }

        case CmdLineOptionType::HexUInt:
        {
            if(value == nullptr) {
                error = "missing value for option: " + optionDisplayName(option);
                return false;
            }

            uint32_t parsed = 0;
            if(!parseHexUInt(*value, parsed)) {
                error = "invalid hex value for option " + optionDisplayName(option) + ": " + *value;
                return false;
            }

            *static_cast<uint32_t*>(option.target) = parsed;
            return true;
        }

        default:
            error = "internal error: unsupported option type: " + optionDisplayName(option);
            return false;
    }
}


bool CmdLineParser::parseUInt(const std::string& text,
                              uint32_t& valueOut)
{
    if(text.empty()) {
        return false;
    }

    for(char c : text) {
        if(c < '0' || c > '9') {
            return false;
        }
    }

    errno = 0;
    char* endPtr = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &endPtr, 10);

    if(errno != 0 || endPtr == nullptr || *endPtr != '\0') {
        return false;
    }

    if(value > UINT32_MAX) {
        return false;
    }

    valueOut = static_cast<uint32_t>(value);
    return true;
}


bool CmdLineParser::parseHexUInt(const std::string& text,
                                 uint32_t& valueOut)
{
    if(text.empty()) {
        return false;
    }

    std::string hexText = text;

    if(sStartsWith(hexText, "0x") || sStartsWith(hexText, "0X")) {
        hexText = hexText.substr(2);
    }

    if(hexText.empty()) {
        return false;
    }

    for(char c : hexText) {
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isLower = (c >= 'a' && c <= 'f');
        const bool isUpper = (c >= 'A' && c <= 'F');

        if(!isDigit && !isLower && !isUpper) {
            return false;
        }
    }

    errno = 0;
    char* endPtr = nullptr;
    unsigned long value = std::strtoul(hexText.c_str(), &endPtr, 16);

    if(errno != 0 || endPtr == nullptr || *endPtr != '\0') {
        return false;
    }

    if(value > UINT32_MAX) {
        return false;
    }

    valueOut = static_cast<uint32_t>(value);
    return true;
}


std::string CmdLineParser::optionDisplayName(const CmdLineOption& option)
{
    if(option.longName && *option.longName) {
        return std::string("--") + option.longName;
    }

    if(option.shortName != '\0') {
        return std::string("-") + option.shortName;
    }

    return "<unnamed option>";
}
