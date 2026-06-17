//
// GPIO.hpp
// Cross-platform GPIO class using Linux GPIO v2 ioctl API
// Safe to compile on macOS (stubbed)
//

#pragma once

#include <string>
#include <vector>
#include <utility>

#if defined(__APPLE__)
// used for cross compile on osx
// Minimal stub of <linux/gpio.h> for macOS builds
// Provides GPIO v2 API definitions just for compilation.
// This file does not perform any actual I/O.

#pragma once

#include <stdint.h>

// ----------------------------------------------------------------------
// Basic ioctl macros (no-op stubs)
// ----------------------------------------------------------------------
// #define _IOWR(type, nr, size) (0)
// #define _IOW(type, nr, size)  (0)
// #define _IOR(type, nr, size)  (0)

// ----------------------------------------------------------------------
// v2 line flags — mirrors kernel 6.1+ definitions
// ----------------------------------------------------------------------
#define GPIO_V2_LINE_FLAG_ACTIVE_LOW        0x00000002
#define GPIO_V2_LINE_FLAG_INPUT             0x00000004
#define GPIO_V2_LINE_FLAG_OUTPUT            0x00000008
#define GPIO_V2_LINE_FLAG_EDGE_RISING       0x00000010
#define GPIO_V2_LINE_FLAG_EDGE_FALLING      0x00000020
#define GPIO_V2_LINE_FLAG_OPEN_DRAIN        0x00000040
#define GPIO_V2_LINE_FLAG_OPEN_SOURCE       0x00000080
#define GPIO_V2_LINE_FLAG_BIAS_PULL_UP      0x00000200
#define GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN    0x00000400
#define GPIO_V2_LINE_FLAG_BIAS_DISABLED     0x00000800

// ----------------------------------------------------------------------
// Structures (no real meaning here — just enough for class compilation)
// ----------------------------------------------------------------------
struct gpio_v2_line_config_attribute {
    uint32_t id;
    uint32_t padding;
    uint64_t values;
};

struct gpio_v2_line_config {
    uint64_t flags;
    uint32_t num_attrs;
    struct gpio_v2_line_config_attribute *attrs;
};

struct gpio_v2_line_request {
    uint32_t offsets[1];
    uint32_t num_lines;
    struct gpio_v2_line_config config;
    int fd;
    char consumer[32];
};

struct gpio_v2_line_values {
    uint64_t mask;
    uint64_t bits;
};

struct gpiochip_info {
    char name[32];
    char label[32];
    uint32_t lines;
};

// ----------------------------------------------------------------------
// Ioctl command constants (stub values)
// ----------------------------------------------------------------------
#define GPIO_GET_CHIPINFO_IOCTL        _IOR(0xB4, 0x01, struct gpiochip_info)
#define GPIO_V2_GET_LINE_IOCTL         _IOWR(0xB4, 0x0D, struct gpio_v2_line_request)
#define GPIO_V2_LINE_GET_VALUES_IOCTL  _IOWR(0xB4, 0x0E, struct gpio_v2_line_values)
#define GPIO_V2_LINE_SET_VALUES_IOCTL  _IOWR(0xB4, 0x0F, struct gpio_v2_line_values)

// ----------------------------------------------------------------------
// Error constants (harmless on macOS builds)
// ----------------------------------------------------------------------
#define ENOTTY 25

// ----------------------------------------------------------------------
// Done
// ----------------------------------------------------------------------

#else
#include <linux/gpio.h>
#endif


#include <sys/ioctl.h>                                                  // Serial Port IO Controls

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

class GPIO {
public:
    enum gpio_direction_t {
        GPIO_DIRECTION_INPUT  = 0,
        GPIO_DIRECTION_OUTPUT = 1
    };

    struct gpio_pin_t {
        unsigned int lineNo;
        gpio_direction_t direction;
        unsigned int flags;
    };

    using gpioStates_t = std::vector<std::pair<int, bool>>;

    GPIO();
    ~GPIO();

    bool begin(std::vector<gpio_pin_t> pinsIn, int &error);
    void stop();
    bool isAvailable();

    bool get(gpioStates_t &statesOut);
    bool set(gpioStates_t states);

private:
     struct line_request_t {
        int lineNo;
        gpio_direction_t direction;
        unsigned int flags;
        int fd;
    };
    std::vector<line_request_t> _lines;
    int _chipFd;

    bool _isSetup;
};

/*
  PI has these now

// GPIO v2 flag bits
#define GPIO_V2_LINE_FLAG_ACTIVE_LOW        0x00000002
#define GPIO_V2_LINE_FLAG_INPUT             0x00000004
#define GPIO_V2_LINE_FLAG_OUTPUT            0x00000008
#define GPIO_V2_LINE_FLAG_EDGE_RISING       0x00000010
#define GPIO_V2_LINE_FLAG_EDGE_FALLING      0x00000020
#define GPIO_V2_LINE_FLAG_OPEN_DRAIN        0x00000040
#define GPIO_V2_LINE_FLAG_OPEN_SOURCE       0x00000080
#define GPIO_V2_LINE_FLAG_BIAS_PULL_UP      0x00000200
#define GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN    0x00000400
#define GPIO_V2_LINE_FLAG_BIAS_DISABLED     0x00000800
 */
