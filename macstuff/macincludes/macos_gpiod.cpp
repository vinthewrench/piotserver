/*
 * macos_gpiod.cpp
 * Dummy libgpiod shim for macOS so the project links.
 *
 * This implements the subset of libgpiod v3 API that our code calls.
 * No real GPIO access happens here.
 */

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>

#if defined(__APPLE__)

#include <sstream>
#include "LogMgr.hpp"

// minimal stand-ins for libgpiod types
extern "C" {

struct gpiod_chip {
    char name[32];
    char label[32];
};

struct gpiod_line_settings {
    int direction;
    int active_low;
    int bias;
};

struct gpiod_line_config {
    // in real libgpiod this maps offsets -> settings
    // here we don't care
    int dummy;
};

struct gpiod_request_config {
    char consumer[32];
};

struct gpiod_line_request {
    unsigned int fake_offset;
    int last_value;
};

enum gpiod_line_value {
    GPIOD_LINE_VALUE_INACTIVE = 0,
    GPIOD_LINE_VALUE_ACTIVE   = 1,
    GPIOD_LINE_VALUE_ERROR    = 2
};

/* Directions, bias etc. Your headers already define these on Linux;
   we mirror enough to compile on macOS. Adjust if your code needs more. */
#ifndef GPIOD_LINE_DIRECTION_INPUT
#define GPIOD_LINE_DIRECTION_INPUT   0
#endif

#ifndef GPIOD_LINE_DIRECTION_OUTPUT
#define GPIOD_LINE_DIRECTION_OUTPUT  1
#endif

#ifndef GPIOD_LINE_BIAS_PULL_UP
#define GPIOD_LINE_BIAS_PULL_UP      1
#endif

/* ---------- chip open/close ---------- */

struct gpiod_chip *gpiod_chip_open(const char *path)
{
    // pretend success
    gpiod_chip *chip = (gpiod_chip*) std::calloc(1, sizeof(gpiod_chip));
    if (chip && path)
        std::strncpy(chip->name, path, sizeof(chip->name)-1);
    return chip;
}

void gpiod_chip_close(struct gpiod_chip *chip)
{
    if (chip)
        std::free(chip);
}

/* ---------- line_settings ---------- */

struct gpiod_line_settings *gpiod_line_settings_new(void)
{
    gpiod_line_settings *s =
        (gpiod_line_settings*) std::calloc(1, sizeof(gpiod_line_settings));
    return s;
}

void gpiod_line_settings_free(struct gpiod_line_settings *settings)
{
    if (settings)
        std::free(settings);
}

void gpiod_line_settings_set_direction(struct gpiod_line_settings *settings,
                                       int direction)
{
    if (settings)
        settings->direction = direction;
}

void gpiod_line_settings_set_active_low(struct gpiod_line_settings *settings,
                                        int active_low)
{
    if (settings)
        settings->active_low = active_low;
}

void gpiod_line_settings_set_bias(struct gpiod_line_settings *settings,
                                  int bias)
{
    if (settings)
        settings->bias = bias;
}

/* ---------- line_config ---------- */

struct gpiod_line_config *gpiod_line_config_new(void)
{
    gpiod_line_config *cfg =
        (gpiod_line_config*) std::calloc(1, sizeof(gpiod_line_config));
    return cfg;
}

void gpiod_line_config_free(struct gpiod_line_config *config)
{
    if (config)
        std::free(config);
}

/* match libgpiod v3 prototype:
   int gpiod_line_config_add_line_settings(struct gpiod_line_config *config,
                                           const unsigned int *offsets,
                                           size_t num_offsets,
                                           const struct gpiod_line_settings *settings);
*/
int gpiod_line_config_add_line_settings(struct gpiod_line_config *config,
                                        const unsigned int *offsets,
                                        size_t num_offsets,
                                        const struct gpiod_line_settings *settings)
{
    // mac stub: accept anything, pretend success
    (void)config;
    (void)offsets;
    (void)num_offsets;
    (void)settings;
    return 0;
}

/* ---------- request_config ---------- */

struct gpiod_request_config *gpiod_request_config_new(void)
{
    gpiod_request_config *rcfg =
        (gpiod_request_config*) std::calloc(1, sizeof(gpiod_request_config));
    return rcfg;
}

void gpiod_request_config_free(struct gpiod_request_config *config)
{
    if (config)
        std::free(config);
}

void gpiod_request_config_set_consumer(struct gpiod_request_config *config,
                                       const char *consumer)
{
    if (config && consumer)
        std::strncpy(config->consumer, consumer,
                     sizeof(config->consumer)-1);
}

/* ---------- chip_request_lines ---------- */

/* real sig:
   struct gpiod_line_request *gpiod_chip_request_lines(
        struct gpiod_chip *chip,
        const struct gpiod_request_config *req_cfg,
        const struct gpiod_line_config *line_cfg);
*/
struct gpiod_line_request *gpiod_chip_request_lines(
        struct gpiod_chip *chip,
        const struct gpiod_request_config *req_cfg,
        const struct gpiod_line_config *line_cfg)
{
    // create a fake request handle
    (void)chip;
    (void)req_cfg;
    (void)line_cfg;

    gpiod_line_request *req =
        (gpiod_line_request*) std::calloc(1, sizeof(gpiod_line_request));
    if (req) {
        req->fake_offset = 0;
        req->last_value  = 0;
    }
    return req;
}

/* ---------- line_request I/O ---------- */

/* v3 style:
   int gpiod_line_request_set_value(struct gpiod_line_request *request,
                                    unsigned int offset,
                                    enum gpiod_line_value value);
*/
int gpiod_line_request_set_value(struct gpiod_line_request *request,
                                 unsigned int offset,
                                 enum gpiod_line_value value)
{
    (void)offset;
    if (!request) {
        errno = EINVAL;
        return -1;
    }
    request->last_value = (value == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
    return 0;
}

/* v3 style:
   enum gpiod_line_value
   gpiod_line_request_get_value(struct gpiod_line_request *request,
                                unsigned int offset);
*/
enum gpiod_line_value
gpiod_line_request_get_value(struct gpiod_line_request *request,
                             unsigned int offset)
{
    (void)offset;
    if (!request) {
        errno = EINVAL;
        return GPIOD_LINE_VALUE_ERROR;
    }
    return request->last_value ? GPIOD_LINE_VALUE_ACTIVE
                               : GPIOD_LINE_VALUE_INACTIVE;
}

/* cleanup */
void gpiod_line_request_release(struct gpiod_line_request *request)
{
    if (request)
        std::free(request);
}

} // extern "C"

#endif // __APPLE__
