//
// GPIO.hpp
// GPIO class using Linux GPIO v2 ioctl API
//

#pragma once

#include <string>
#include <vector>
#include <utility>

#include <linux/gpio.h>
#include <sys/ioctl.h>

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

        /*
         * Extra Linux GPIO v2 request flags.
         *
         * Do not put direction here. GPIO::begin() adds INPUT or OUTPUT from
         * gpio_pin_t::direction.
         *
         * Examples:
         *
         *   GPIO_V2_LINE_FLAG_BIAS_PULL_UP
         *   GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN
         *   GPIO_V2_LINE_FLAG_BIAS_DISABLED
         *   GPIO_V2_LINE_FLAG_ACTIVE_LOW
         */
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
