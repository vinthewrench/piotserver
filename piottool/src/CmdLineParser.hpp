//
// CmdLineParser.hpp
// Shared command-line parser for piottool / pIoTServer.
//

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>


enum class CmdLineOptionType
{
    Boolean = 0,
    Count,
    String,
    UInt,
    HexUInt
};


struct CmdLineOption
{
    CmdLineOptionType type;

    /*
     * Target points to:
     *
     *   Boolean -> bool*
     *   Count   -> int*
     *   String  -> std::string*
     *   UInt    -> uint32_t*
     *   HexUInt -> uint32_t*
     */
    void* target;

    const char* longName;     // Example: "verbose" for --verbose
    char        shortName;    // Example: 'v' for -v, or '\0' for none

    const char* valueName;    // Example: "file", "n", "hex"; null for flags
    const char* help;
};


struct CmdLineParseResult
{
    bool ok = true;
    bool helpRequested = false;

    std::string error;
    std::vector<std::string> positionals;
};


class CmdLineParser
{
public:
    static CmdLineParseResult parse(int argc,
                                    const char* const argv[],
                                    const CmdLineOption* options,
                                    size_t optionCount,
                                    bool allowPositionals);

    static void printUsage(std::ostream& out,
                           const char* programName,
                           const char* usageLine,
                           const CmdLineOption* options,
                           size_t optionCount);

private:
    static const CmdLineOption* findLongOption(const std::string& name,
                                               const CmdLineOption* options,
                                               size_t optionCount);

    static const CmdLineOption* findShortOption(char name,
                                                const CmdLineOption* options,
                                                size_t optionCount);

    static bool optionNeedsValue(const CmdLineOption& option);

    static bool applyOption(const CmdLineOption& option,
                            const std::string* value,
                            std::string& error);

    static bool parseUInt(const std::string& text,
                          uint32_t& valueOut);

    static bool parseHexUInt(const std::string& text,
                             uint32_t& valueOut);

    static std::string optionDisplayName(const CmdLineOption& option);
};
