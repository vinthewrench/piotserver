//
// GPIO.cpp
// Implementation for Raspberry Pi (Linux GPIO v2)
//

#include "GPIO.hpp"
#include <iostream>
#include <errno.h>
#include <cstring>

#include "LogMgr.hpp"

 
GPIO::GPIO() : _isSetup(false), _chipFd(-1) {}
GPIO::~GPIO() { stop(); }

bool GPIO::begin(std::vector<gpio_pin_t> pinsIn, int &error) {
    error = 0;

    _chipFd = open("/dev/gpiochip0", O_RDWR);
    if (_chipFd < 0) {
        error = errno;
        LOGT_ERROR("Error opening GPIO chip: %s", strerror(errno));
        return false;
    }

    // --- Check for GPIO v2 API support ---
    struct gpiochip_info cinfo = {};
    if (ioctl(_chipFd, GPIO_GET_CHIPINFO_IOCTL, &cinfo) < 0) {
        error = errno;
        LOGT_ERROR("Failed to query GPIO chip info: %s", strerror(errno));
        close(_chipFd);
        _chipFd = -1;
        return false;
    }

#ifndef GPIO_V2_GET_LINE_IOCTL
    LOGT_ERROR("Kernel headers do not define GPIO v2 API — rebuild required");
    close(_chipFd);
    _chipFd = -1;
    return false;
#endif
    
    for (auto &p : pinsIn) {
        struct gpio_v2_line_request req = {};
        struct gpio_v2_line_config config = {};
        struct gpio_v2_line_config_attribute attr = {};

        config.num_attrs = 0;

        // Apply direction flag
        if (p.direction == GPIO_DIRECTION_INPUT)
            config.flags |= GPIO_V2_LINE_FLAG_INPUT;
        else if (p.direction == GPIO_DIRECTION_OUTPUT)
            config.flags |= GPIO_V2_LINE_FLAG_OUTPUT;

        // Apply user-specified flags (bias, active-low, etc.)
        config.flags |= p.flags;

        // Set consumer label
        strncpy((char *)req.consumer, "GPIO_Class", sizeof(req.consumer) - 1);

        // Target line
        req.offsets[0] = p.lineNo;
        req.num_lines = 1;
        req.config = config;

        // Request the line
        if (ioctl(_chipFd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
            error = errno;
            LOGT_ERROR("Failed to request GPIO line %d: %s",  p.lineNo, strerror(errno));
            continue;
        }

        _lines.push_back({
            .lineNo = static_cast<int>(p.lineNo),
            .direction = p.direction,
            .flags = p.flags,
            .fd = req.fd
        });
    }

    if (_lines.empty()) {
        close(_chipFd);
        _chipFd = -1;
        return false;
    }

    _isSetup = true;
    return true;
}

void GPIO::stop() {
    if (!_isSetup)
        return;

    for (auto &l : _lines) {
        if (l.fd >= 0)
            close(l.fd);
    }
    _lines.clear();

    if (_chipFd >= 0)
        close(_chipFd);

    _chipFd = -1;
    _isSetup = false;
}

bool GPIO::isAvailable() {
    return _isSetup;
}

bool GPIO::get(gpioStates_t &statesOut) {
    if (!_isSetup)
        return false;

    for (auto &l : _lines) {
        if (l.direction != GPIO_DIRECTION_INPUT)
            continue;

        struct gpio_v2_line_values vals = {};
        vals.mask = 0x1;

        if (ioctl(l.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
            LOGT_ERROR("Failed to read GPIO line %d: %s",  l.lineNo, strerror(errno));
             continue;
        }

        bool value = (vals.bits & 0x1);
        statesOut.push_back({l.lineNo, value});
    }

    return true;
}

bool GPIO::set(gpioStates_t states) {
    if (!_isSetup)
        return false;

    for (auto &s : states) {
        for (auto &l : _lines) {
            if (l.lineNo == s.first && l.direction == GPIO_DIRECTION_OUTPUT) {
                struct gpio_v2_line_values vals = {};
                vals.mask = 0x1;
                vals.bits = s.second ? 0x1 : 0x0;

                if (ioctl(l.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
                    LOGT_ERROR("Failed to set GPIO line %d: %s",  l.lineNo, strerror(errno));
                    return false;
                }
            }
        }
    }

    return true;
}
