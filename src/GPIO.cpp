//
// GPIO.cpp
// Implementation for Raspberry Pi (Linux GPIO v2)
//

#include "GPIO.hpp"
#include <iostream>
#include <errno.h>
#include <cstring>

#include "LogMgr.hpp"


GPIO::GPIO() : _chipFd(-1), _isSetup(false) {}

GPIO::~GPIO() { stop(); }

bool GPIO::begin(std::vector<gpio_pin_t> pinsIn, int &error) {
    error = 0;

    /*
     * Make begin() safe to call more than once on the same GPIO object.
     * If we already own any line requests, release them first.
     */
    stop();

    _chipFd = open("/dev/gpiochip0", O_RDWR);
    if (_chipFd < 0) {
        error = errno;
        LOGT_ERROR("Error opening GPIO chip: %s", strerror(errno));
        return false;
    }

    /*
     * Check for GPIO v2 API support.
     */
    struct gpiochip_info cinfo = {};
    if (ioctl(_chipFd, GPIO_GET_CHIPINFO_IOCTL, &cinfo) < 0) {
        error = errno;
        LOGT_ERROR("Failed to query GPIO chip info: %s", strerror(errno));
        stop();
        return false;
    }

#ifndef GPIO_V2_GET_LINE_IOCTL
    LOGT_ERROR("Kernel headers do not define GPIO v2 API — rebuild required");
    error = ENOSYS;
    stop();
    return false;
#endif

    for (auto &p : pinsIn) {
        struct gpio_v2_line_request req = {};
        struct gpio_v2_line_config config = {};

        config.num_attrs = 0;

        /*
         * Apply direction flag from the requested direction.
         */
        if (p.direction == GPIO_DIRECTION_INPUT) {
            config.flags |= GPIO_V2_LINE_FLAG_INPUT;
        }
        else if (p.direction == GPIO_DIRECTION_OUTPUT) {
            config.flags |= GPIO_V2_LINE_FLAG_OUTPUT;
        }
        else {
            error = EINVAL;
            LOGT_ERROR("Invalid GPIO direction for line %d", p.lineNo);
            continue;
        }

        /*
         * Apply user-specified line flags.
         *
         * Expected examples:
         *
         *   input + pull-up:
         *      p.flags      = 0x200
         *      config.flags = 0x204
         *
         *   input only:
         *      p.flags      = 0x000
         *      config.flags = 0x004
         *
         * ACTIVE_LOW is only applied if the caller explicitly puts it in
         * p.flags. GPIO does not add it by itself.
         */
        config.flags |= p.flags;

        /*
         * Set consumer label.
         */
        strncpy((char *)req.consumer, "GPIO_Class", sizeof(req.consumer) - 1);
        req.consumer[sizeof(req.consumer) - 1] = '\0';

        /*
         * Target one GPIO line.
         */
        req.offsets[0] = p.lineNo;
        req.num_lines = 1;
        req.config = config;

        // LOGT_ERROR("GPIO request line=%d direction=%d p.flags=0x%X config.flags=0x%llX",
        //            p.lineNo,
        //            p.direction,
        //            p.flags,
        //            static_cast<unsigned long long>(config.flags));

        /*
         * Request the line.
         */
        if (ioctl(_chipFd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
            error = errno;
            LOGT_ERROR("Failed to request GPIO line %d: %s",
                       p.lineNo,
                       strerror(errno));
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
        stop();

        if (error == 0) {
            error = ENODEV;
        }

        return false;
    }

    _isSetup = true;
    return true;
}

void GPIO::stop() {
    for (auto &l : _lines) {
        if (l.fd >= 0) {
            close(l.fd);
            l.fd = -1;
        }
    }

    _lines.clear();

    if (_chipFd >= 0) {
        close(_chipFd);
        _chipFd = -1;
    }

    _isSetup = false;
}


bool GPIO::isAvailable() {
    return _isSetup;
}

bool GPIO::get(gpioStates_t &statesOut) {
    statesOut.clear();

    if (!_isSetup) {
        return false;
    }

    bool readAny = false;

    for (auto &l : _lines) {
        if (l.direction != GPIO_DIRECTION_INPUT) {
            continue;
        }

        struct gpio_v2_line_values vals = {};
        vals.mask = 0x1;

        if (ioctl(l.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &vals) < 0) {
            LOGT_ERROR("Failed to read GPIO line %d: %s",
                       l.lineNo,
                       strerror(errno));
            continue;
        }

        /*
         * GPIO v2 returns the requested line value in bit 0 because each line
         * request here owns exactly one line.
         *
         * This value is the kernel-returned line value for the request. If the
         * caller requested GPIO_V2_LINE_FLAG_ACTIVE_LOW, the kernel may report
         * the logical active-low value. If the caller did not request
         * ACTIVE_LOW, this is the raw electrical high/low level.
         *
         * SHUTDOWN_SIG should not request ACTIVE_LOW at the GPIO layer. It
         * should read raw high/low here and apply BAT_LOW polarity itself.
         */
        bool value = (vals.bits & 0x1) != 0;

        statesOut.push_back({l.lineNo, value});
        readAny = true;

        // LOGT_DEBUG("GPIO read line=%d value=%d flags=0x%X",
        //            l.lineNo,
        //            value ? 1 : 0,
        //            l.flags);
    }

    return readAny;
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
