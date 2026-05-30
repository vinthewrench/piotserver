/**
 * @file powercontrol.c
 * @brief Power Controller Firmware for ATmega88PB.
 *
 * This firmware controls a dual-coil latching power relay, active-low
 * red/green status LEDs, low-power sleep/wake behavior, and an I2C slave
 * command/status interface.
 *
 * The controller sits between a Raspberry Pi and the main power relay.
 * The Pi can request shutdown by commanding the relay OFF over I2C. After
 * a shutdown grace delay, the AVR pulses the relay OFF, enters power-down
 * sleep, and can wake from:
 *
 * - Wake button on INT0 / PD2
 * - AC_OK signal on INT1 / PD3
 * - Watchdog-based minute timer
 *
 * Relay coil pulses are always deferred to main-line code. ISRs only latch
 * events or buffer simple state changes.
 *
 * ============================================================
 * HARDWARE PIN MAP
 * ============================================================
 *
 * LEDs, active-low:
 *   - GREEN  -> PC2
 *   - RED    -> PC3
 *
 * Relay, dual-coil latching:
 *   - SET    -> PD6
 *   - RESET  -> PD5
 *
 * External interrupts:
 *   - INT0, Wake Button, active-low -> PD2
 *   - INT1, AC_OK, rising edge      -> PD3
 *
 * I2C:
 *   - SDA -> PC4
 *   - SCL -> PC5
 *   - Slave address: 0x08
 *
 * ============================================================
 * STATUS BYTE
 * ============================================================
 *
 * Practical bit layout returned by register 0x00 and 0xF1:
 *
 *   bit 1 / 0x02 = red LED logical state, 1 = ON
 *   bit 2 / 0x04 = green LED logical state, 1 = ON
 *   bit 3 / 0x08 = AC_OK input, raw PD3 high = 1
 *
 * Relay state is intentionally omitted. If the relay is off, the Pi is
 * unpowered and cannot read this status byte.
 *
 * Wake timer active is intentionally omitted from the status byte.
 *
 * ============================================================
 * I2C COMMANDS
 * ============================================================
 *
 * Single-byte commands:
 *
 *   0x00 = select status register
 *   0x01 = Pi shutdown requested, delayed relay OFF
 *   0x02 = relay ON / cancel pending shutdown request
 *   0x03 = red LED OFF
 *   0x04 = red LED ON
 *   0x05 = green LED OFF
 *   0x06 = green LED ON
 *   0x07 = set wake timer, followed by MSB and LSB
 *
 * Read registers:
 *
 *   0x00 = status byte
 *   0xF0 = firmware version
 *   0xF1 = status byte
 *   0xF2 = configured wake timer MSB
 *   0xF3 = configured wake timer LSB
 *   0xF4 = raw PIND debug register
 *
 * ============================================================
 * TEST COMMANDS
 * ============================================================
 *
 * Detect device:
 *   sudo i2cdetect -y 1
 *
 * Status byte:
 *   i2cget -y 1 0x08
 *
 * Explicit status byte:
 *   i2ctransfer -y 1 w1@0x08 0x00 r1
 *
 * Extended read, firmware, status, timer MSB, timer LSB:
 *   i2ctransfer -y 1 w1@0x08 0xF0 r4
 *
 * Raw PORTD debug:
 *   i2ctransfer -y 1 w1@0x08 0xF4 r1
 *
 * Raw PIND decoding:
 *   PD2 / Wake Button = 0x04
 *   PD3 / AC_OK       = 0x08
 *
 * LED tests:
 *   i2cset -y 1 0x08 0x04    # RED ON
 *   i2cset -y 1 0x08 0x06    # GREEN ON
 *   i2cset -y 1 0x08 0x05    # GREEN OFF
 *   i2cset -y 1 0x08 0x03    # RED OFF
 *
 * Relay tests:
 *   i2cset -y 1 0x08 0x01    # Pi shutdown request, delayed relay OFF
 *   i2cset -y 1 0x08 0x02    # Relay ON / cancel shutdown request
 *
 * Timer test, set 1 minute wake-up:
 *   i2ctransfer -y 1 w3@0x08 0x07 0x00 0x01
 *
 * ============================================================
 * BUILD & FLASH
 * ============================================================
 *
 * Compile:
 *   avr-gcc -Wall -Os -mmcu=atmega88pb -DF_CPU=8000000UL -o powercontrol.elf powercontrol.c
 *
 * Convert to hex:
 *   avr-objcopy -O ihex -R .eeprom powercontrol.elf powercontrol.hex
 *
 * Flash:
 *   avrdude -c atmelice_isp -P usb -p atmega88pb -U flash:w:powercontrol.hex:i
 *
 * Example fuse set, internal 8 MHz, BOD enabled:
 *   -U lfuse:w:0xE2:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/twi.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* =========================================================
 * MCU REGISTER COMPATIBILITY
 * =========================================================
 *
 * ATmega8 uses:
 *   MCUCR, GICR, GIFR, TIMSK, TIFR
 *
 * ATmega88/88PB uses:
 *   EICRA, EIMSK, EIFR, TIMSK1, TIFR1
 *
 * The interrupt bit names are the same, but the register names differ.
 */
#if defined(__AVR_ATmega8__)
#define EXTINT_CTRL_REG MCUCR
#define EXTINT_MASK_REG GICR
#define EXTINT_FLAG_REG GIFR
#define TIMER1_TIMSK_REG TIMSK
#define TIMER1_TIFR_REG TIFR
#else
#define EXTINT_CTRL_REG EICRA
#define EXTINT_MASK_REG EIMSK
#define EXTINT_FLAG_REG EIFR
#define TIMER1_TIMSK_REG TIMSK1
#define TIMER1_TIFR_REG TIFR1
#endif

/* =========================================================
 * FORWARD DECLARATIONS
 * ========================================================= */

/** @brief Initialize the AVR TWI peripheral as an I2C slave. */
static void i2c_init(void);

/** @brief Configure INT0 and INT1 for normal runtime edge-triggered operation. */
static void extint_config_run_edges(void);

/** @brief Initialize Timer1 for approximate one-second compare interrupts. */
static void timer1_init_1s(void);

/** @brief Stop Timer1 and clear its pending compare flag. */
static inline void timer1_stop(void);

/** @brief Prepare peripherals and pins before entering power-down sleep. */
static void prepare_for_sleep_powerdown(void);

/** @brief Enter AVR power-down sleep until an enabled wake source fires. */
static void enter_sleep_powerdown(void);

/** @brief Restore clocks, I2C, interrupts, and LEDs after sleep. */
static void restore_after_sleep_powerdown(void);

/* =========================================================
 * CLOCK CONFIGURATION
 * ========================================================= */

/** @brief CPU frequency used by delay and timing code. */
#ifndef F_CPU
#define F_CPU 8000000UL
#endif

/**
 * @brief Timer1 compare value for approximate one-second ticks.
 *
 * This value is intentionally calibrated rather than mathematically ideal.
 * Internal RC oscillator drift applies.
 */
#define TIMER1_OCR1A       6695

/**
 * @brief OSCCAL value used at boot.
 *
 * Adjust only after measuring the actual oscillator behavior.
 */
#define CALIBRATED_OSCCAL  0x80

/* =========================================================
 * I2C SLAVE ADDRESS
 * ========================================================= */

/** @brief 7-bit I2C slave address. */
#define I2C_ADDR 0x08

/* =========================================================
 * GPIO ASSIGNMENTS
 * ========================================================= */

/** @brief Green LED pin on PORTC, active-low. */
#define GREEN_LED   PC2

/** @brief Red LED pin on PORTC, active-low. */
#define RED_LED     PC3

/** @brief Relay SET coil drive pin on PORTD. */
#define RELAY_SET   PD6

/** @brief Relay RESET coil drive pin on PORTD. */
#define RELAY_RESET PD5

/** @brief Wake button input on INT0 / PD2, active-low. */
#define INT0_PIN    PD2

/** @brief AC_OK input on INT1 / PD3, high means external supply valid. */
#define INT1_PIN    PD3

/* =========================================================
 * WATCHDOG CONFIGURATION
 * ========================================================= */

/** @brief Runtime hardware watchdog timeout. */
#define HW_WDT_TIMEOUT WDTO_2S

/** @brief Watchdog interrupt ticks per approximate minute. */
#define WDT_TICKS_PER_MIN 30

/* =========================================================
 * SHUTDOWN DELAY CONFIGURATION
 * ========================================================= */

/**
 * @brief Delay after Pi shutdown request before relay power is removed.
 *
 * The Pi sends command 0x01 before shutdown. The AVR waits this many
 * seconds, then pulses the relay RESET coil.
 */
#define SHUTDOWN_DELAY_SECONDS 30

/* =========================================================
 * I2C COMMANDS / REGISTERS
 * ========================================================= */

/** @brief Extended read register start. */
#define CMD_READ_EXT_STATUS 0xF0

/** @brief Command byte for setting the persistent wake timer. */
#define CMD_SET_WAKE_TIMER  0x07

/** @brief Firmware version byte returned at register 0xF0. */
#define FW_VERSION          0x22

/** @brief Maximum accepted wake timer value in minutes. */
#define MAX_WAKE_TIMER_MIN  1440

/* =========================================================
 * EEPROM CONFIGURATION
 * ========================================================= */

/** @brief EEPROM signature value, ASCII "PC". */
#define EEPROM_SIGNATURE 0x5043

/** @brief EEPROM structure version. */
#define EEPROM_VERSION   0x01

/** @brief EEPROM base address for configuration block. */
#define EEPROM_BASE_ADDR 0x00

/**
 * @brief Persistent EEPROM configuration.
 *
 * The firmware keeps a RAM copy in `cfg`. EEPROM is read at boot and only
 * updated when policy changes, such as changing the wake timer through I2C.
 */
typedef struct {
    uint16_t signature;       /**< Configuration validity signature. */
    uint8_t version;          /**< Configuration structure version. */
    uint16_t wake_timer_min;  /**< Persistent wake timer in minutes. */
    uint16_t checksum;        /**< Fletcher-16 checksum over prior fields. */
} eeprom_cfg_t;

/** @brief Active RAM configuration loaded from EEPROM. */
static eeprom_cfg_t cfg;

/**
 * @brief Compute Fletcher-16 checksum.
 *
 * @param data Pointer to data buffer.
 * @param len Number of bytes to process.
 * @return Fletcher-16 checksum.
 */
uint16_t config_fletcher16(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    while (len--) {
        sum1 = (sum1 + *p++) % 255;
        sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
}

/**
 * @brief Compute checksum for an EEPROM configuration block.
 *
 * @param cfgp Pointer to configuration block.
 * @return Fletcher-16 checksum over all fields before checksum.
 */
static uint16_t cfg_checksum(const eeprom_cfg_t *cfgp)
{
    return config_fletcher16(cfgp, offsetof(eeprom_cfg_t, checksum));
}

/**
 * @brief Load persistent configuration from EEPROM.
 *
 * @return 1 if EEPROM contents are valid, 0 if invalid.
 */
static uint8_t eeprom_load_cfg(void)
{
    eeprom_read_block((void *)&cfg,
                      (const void *)EEPROM_BASE_ADDR,
                      sizeof(cfg));

    if (cfg.signature != EEPROM_SIGNATURE) {
        return 0;
    }

    if (cfg.version != EEPROM_VERSION) {
        return 0;
    }

    if (cfg.checksum != cfg_checksum(&cfg)) {
        return 0;
    }

    return 1;
}

/**
 * @brief Initialize default configuration in RAM.
 *
 * Default wake timer is one minute. This is useful for testing. Change to
 * zero for production if automatic wake after shutdown is not desired.
 */
static void eeprom_default_cfg(void)
{
    cfg.signature = EEPROM_SIGNATURE;
    cfg.version = EEPROM_VERSION;
    cfg.wake_timer_min = 1;
    cfg.checksum = cfg_checksum(&cfg);
}

/**
 * @brief Save current RAM configuration to EEPROM.
 *
 * Uses EEPROM update semantics to reduce unnecessary writes.
 */
static void eeprom_save_cfg(void)
{
    cfg.checksum = cfg_checksum(&cfg);

    eeprom_update_block((const void *)&cfg,
                        (void *)EEPROM_BASE_ADDR,
                        sizeof(cfg));
}

/* =========================================================
 * SOFTWARE STATE
 * ========================================================= */

/** @brief Current I2C register pointer used for slave transmit reads. */
static volatile uint8_t i2c_reg_ptr = 0;

/** @brief I2C receive state, 0 idle, 1 expecting timer MSB, 2 expecting timer LSB. */
static volatile uint8_t rx_state = 0;

/** @brief Temporary receive buffer for multi-byte wake timer writes. */
static volatile uint16_t rx_tmp = 0;

/** @brief Wake timer value received through I2C, applied in main loop. */
static volatile uint16_t deferred_wake_timer = 0;

/** @brief Set by I2C ISR when a wake timer update should be applied. */
static volatile bool timer_update_pending = false;

/** @brief Runtime remaining wake timer in minutes. */
static volatile uint16_t wake_timer_min = 0;

/** @brief Runtime seconds counter used with Timer1. */
static volatile uint8_t timer_seconds = 0;

/** @brief Set when the wake timer has expired. */
static volatile bool timer_expired = false;

/** @brief Current logical relay state, 1 means relay ON. */
static volatile uint8_t relay_state = 0;

/** @brief Current logical red LED state, 1 means LED ON. */
static volatile uint8_t red_state = 0;

/** @brief Current logical green LED state, 1 means LED ON. */
static volatile uint8_t green_state = 0;

/** @brief Set by INT0 ISR when wake button interrupt fires. */
static volatile uint8_t wake_flag = 0;

/** @brief Set when Pi requested relay-off shutdown behavior. */
static volatile uint8_t pi_shutdown_requested = 0;

/** @brief Tracks whether the controller is already in a shutdown sleep cycle. */
static volatile bool shutdown_sleep_active = false;

/** @brief True while waiting before removing Pi power after shutdown request. */
static volatile bool shutdown_delay_active = false;

/** @brief Remaining shutdown delay in seconds. */
static volatile uint8_t shutdown_delay_seconds = 0;

/** @brief Previous sampled AC_OK state used for software rising-edge detection. */
static uint8_t ac_ok_prev = 0;

/** @brief Set by TWI ISR when a serious TWI error occurred. */
static volatile bool twi_error_blink_pending = false;

/**
 * @brief Deferred relay request.
 *
 * Relay coils are never pulsed inside interrupts. ISRs set this request and
 * main-line code performs the actual relay pulse.
 */
typedef enum {
    RELAY_REQ_NONE = 0,  /**< No relay request pending. */
    RELAY_REQ_ON,        /**< Pulse relay SET coil. */
    RELAY_REQ_OFF        /**< Pulse relay RESET coil. */
} relay_req_t;

/** @brief Pending relay request from I2C command path. */
static volatile relay_req_t relay_req = RELAY_REQ_NONE;

/* =========================================================
 * LED HELPERS
 * ========================================================= */

/**
 * @brief Set red LED state.
 *
 * @param on true to turn LED on, false to turn it off.
 */
static inline void led_red(bool on)
{
    if (on) {
        PORTC &= ~(1 << RED_LED);
        red_state = 1;
    } else {
        PORTC |= (1 << RED_LED);
        red_state = 0;
    }
}

/**
 * @brief Set green LED state.
 *
 * @param on true to turn LED on, false to turn it off.
 */
static inline void led_green(bool on)
{
    if (on) {
        PORTC &= ~(1 << GREEN_LED);
        green_state = 1;
    } else {
        PORTC |= (1 << GREEN_LED);
        green_state = 0;
    }
}

/**
 * @brief Blink an LED from main-line context.
 *
 * This function temporarily disables TWI interrupts while blinking to avoid
 * nested LED state changes during a diagnostic blink. It must not be called
 * from an ISR.
 *
 * @param pin LED pin selector, RED_LED or GREEN_LED.
 * @param count Number of blinks.
 */
static void led_blink(uint8_t pin, uint8_t count)
{
    uint8_t twcr_saved = TWCR;
    uint8_t red_was = red_state;
    uint8_t green_was = green_state;

    TWCR &= ~(1 << TWIE);

    for (uint8_t i = 0; i < count; i++) {
        if (pin == RED_LED) {
            led_red(true);
        } else {
            led_green(true);
        }

        for (uint8_t k = 0; k < 25; k++) {
            _delay_ms(10);
            wdt_reset();
        }

        if (pin == RED_LED) {
            led_red(false);
        } else {
            led_green(false);
        }

        for (uint8_t k = 0; k < 25; k++) {
            _delay_ms(10);
            wdt_reset();
        }
    }

    TWCR = twcr_saved;
    led_red(red_was);
    led_green(green_was);
}

/* =========================================================
 * RELAY HELPERS
 * ========================================================= */

/** @brief Relay coil pulse width in milliseconds. */
#define RELAY_PULSE_MS 30

/**
 * @brief Pulse one relay coil.
 *
 * This function must only be called from main-line code, never from an ISR.
 *
 * @param pin Relay coil pin, RELAY_SET or RELAY_RESET.
 */
static inline void relay_pulse(uint8_t pin)
{
    PORTD &= ~((1 << RELAY_SET) | (1 << RELAY_RESET));
    PORTD |= (1 << pin);
    _delay_ms(RELAY_PULSE_MS);
    PORTD &= ~(1 << pin);
}

/**
 * @brief Apply relay ON/OFF state with a latching relay pulse.
 *
 * @param on true to SET relay ON, false to RESET relay OFF.
 */
static inline void relay_apply(bool on)
{
    if (on) {
        relay_pulse(RELAY_SET);
        relay_state = 1;
    } else {
        relay_pulse(RELAY_RESET);
        relay_state = 0;
    }
}

/* =========================================================
 * WATCHDOG MINUTE TIMER
 * ========================================================= */

/** @brief Watchdog sleep timer sub-minute tick counter. */
static volatile uint8_t wdt_ticks = 0;

/**
 * @brief Watchdog interrupt handler.
 *
 * Used during power-down sleep as an approximate minute timer. Runtime
 * hardware watchdog mode is configured separately with `wdt_enable()`.
 */
ISR(WDT_vect)
{
    wdt_ticks++;

    if (wdt_ticks >= WDT_TICKS_PER_MIN) {
        wdt_ticks = 0;

        if (wake_timer_min > 0) {
            wake_timer_min--;

            if (wake_timer_min == 0) {
                timer_expired = true;
            }
        }
    }
}

/**
 * @brief Configure watchdog for interrupt-only sleep timing.
 */
static inline void wdt_arm_interrupt(void)
{
    cli();
    wdt_reset();

    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR =
        (1 << WDIE) |
        (1 << WDP2) | (1 << WDP1) | (1 << WDP0);

    sei();
}

/**
 * @brief Stop the watchdog.
 */
static inline void wdt_stop(void)
{
    cli();
    wdt_reset();

    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR = 0;

    sei();
}

/* =========================================================
 * SLEEP SUPPORT
 * ========================================================= */

/**
 * @brief Prepare the AVR for power-down sleep.
 *
 * Stops Timer1, disables TWI, disables unused peripherals, clears pending
 * external interrupt flags, enables wake sources, turns LEDs off, and arms
 * the watchdog interrupt timer.
 *
 * Important:
 * - LEDs are on PORTC.
 * - WAKE and AC_OK are on PORTD bits 2 and 3.
 * - Do not write LED bit masks to PORTD.
 */
static void prepare_for_sleep_powerdown(void)
{
    timer1_stop();

    TWCR = 0;

    /*
     * AC_OK is on PD3 / INT1.
     *
     * This removes the AVR internal pull-up on AC_OK during sleep.
     * Keep this only if AC_OK is externally driven or externally biased.
     * If AC_OK depends on the AVR pull-up, remove this line.
     */
    PORTD &= ~(1 << INT1_PIN);

    ADCSRA &= ~(1 << ADEN);
    ACSR |= (1 << ACD);

    PRR =
        (1 << PRADC) |
        (1 << PRTWI) |
        (1 << PRTIM0) |
        (1 << PRTIM1) |
        (1 << PRSPI) |
        (1 << PRUSART0);

    EXTINT_FLAG_REG |= (1 << INTF0) | (1 << INTF1);
    EXTINT_MASK_REG |= (1 << INT0) | (1 << INT1);

    PORTC |= (1 << RED_LED) | (1 << GREEN_LED);

    wdt_arm_interrupt();
}

/**
 * @brief Enter power-down sleep.
 *
 * Sleep exits after an enabled interrupt source wakes the MCU.
 */
static void enter_sleep_powerdown(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    cli();
    sleep_enable();

#if defined(sleep_bod_disable)
    sleep_bod_disable();
#endif

    sei();
    sleep_cpu();

    sleep_disable();
}

/**
 * @brief Restore normal runtime state after power-down sleep.
 */
static void restore_after_sleep_powerdown(void)
{
    cli();

    PRR = 0;

    PORTD |= (1 << INT1_PIN);

    i2c_init();
    extint_config_run_edges();

    led_red(red_state);
    led_green(green_state);

    sei();
}

/* =========================================================
 * EXTERNAL INTERRUPTS
 * ========================================================= */

/**
 * @brief Configure INT0 and INT1 for runtime edge detection.
 *
 * INT0 is falling edge for the active-low wake button.
 * INT1 is rising edge for AC_OK becoming valid.
 */
static void extint_config_run_edges(void)
{
    EXTINT_CTRL_REG |= (1 << ISC01);
    EXTINT_CTRL_REG &= ~(1 << ISC00);

    EXTINT_CTRL_REG |= (1 << ISC11) | (1 << ISC10);

    EXTINT_FLAG_REG |= (1 << INTF0) | (1 << INTF1);
    EXTINT_MASK_REG |= (1 << INT0) | (1 << INT1);
}

/**
 * @brief INT0 ISR for wake button.
 *
 * Masks INT0 until main-line code re-arms it. This prevents button bounce from
 * repeatedly retriggering before the main loop can debounce.
 */
ISR(INT0_vect)
{
    EXTINT_MASK_REG &= ~(1 << INT0);
    wake_flag = 1;
}

/**
 * @brief INT1 ISR for AC_OK.
 *
 * This ISR intentionally does not apply policy. It only wakes the MCU.
 * Main-line code samples AC_OK and performs software edge detection.
 */
ISR(INT1_vect)
{
}

/**
 * @brief Re-arm INT0 after button handling.
 */
static inline void rearm_int0(void)
{
    EXTINT_FLAG_REG |= (1 << INTF0);
    EXTINT_MASK_REG |= (1 << INT0);
}

/* =========================================================
 * TIMER1
 * ========================================================= */

/**
 * @brief Timer1 compare ISR.
 *
 * Provides approximate one-second ticks when Timer1 is used. Power-down sleep
 * uses watchdog timing instead.
 */
ISR(TIMER1_COMPA_vect)
{
    if (timer_seconds > 0) {
        timer_seconds--;
    } else if (wake_timer_min > 0) {
        wake_timer_min--;

        if (wake_timer_min > 0) {
            timer_seconds = 59;
        } else {
            timer_expired = true;
            timer1_stop();
        }
    }
}

/**
 * @brief Stop Timer1 and clear pending compare flag.
 */
static inline void timer1_stop(void)
{
    TCCR1B = 0;
    TIMER1_TIMSK_REG &= ~(1 << OCIE1A);
    TIMER1_TIFR_REG |= (1 << OCF1A);
}

/**
 * @brief Reload wake timer from persistent policy.
 */
static inline void reload_wake_timer(void)
{
    timer1_stop();

    wake_timer_min = cfg.wake_timer_min;

    if (wake_timer_min > 0) {
        timer_seconds = 59;
        timer1_init_1s();
    }
}

/**
 * @brief Initialize Timer1 for approximate one-second CTC interrupts.
 */
static void timer1_init_1s(void)
{
    TCCR1A = 0;
    TCCR1B = 0;

    TCCR1B |= (1 << WGM12);
    OCR1A = TIMER1_OCR1A;

    TIMER1_TIMSK_REG |= (1 << OCIE1A);

    TCCR1B |= (1 << CS10) | (1 << CS12);
}

/**
 * @brief Return true if a sampled signal rose from low to high.
 *
 * @param prev Previous sampled state.
 * @param now Current sampled state.
 * @return true if rising edge detected.
 */
static inline bool ac_ok_rising_edge(uint8_t prev, uint8_t now)
{
    return (!prev && now);
}

/* =========================================================
 * I2C SLAVE
 * ========================================================= */

/**
 * @brief Build current status byte for I2C reads.
 *
 * Practical status map:
 * - bit 1 / 0x02 = red LED logical ON
 * - bit 2 / 0x04 = green LED logical ON
 * - bit 3 / 0x08 = AC_OK physical PD3 HIGH
 *
 * Relay state is intentionally omitted. If the relay is actually off,
 * the Pi is unpowered and cannot read this status byte anyway.
 *
 * Wake timer active is intentionally omitted.
 *
 * @return Status byte.
 */
static uint8_t get_status_byte(void)
{
    uint8_t s = 0;

    if (red_state) {
        s |= 0x02;
    }

    if (green_state) {
        s |= 0x04;
    }

    if (PIND & (1 << PD3)) {
        s |= 0x08;
    }

    return s;
}

/**
 * @brief Read a virtual I2C register.
 *
 * Register map:
 * - 0x00 = status byte
 * - 0xF0 = firmware version
 * - 0xF1 = status byte
 * - 0xF2 = configured wake timer MSB
 * - 0xF3 = configured wake timer LSB
 * - 0xF4 = raw PIND debug register
 *
 * @param reg Register address.
 * @return Register value or 0xFF for unsupported register.
 */
static uint8_t read_reg(uint8_t reg)
{
    switch (reg) {
        case 0x00:
            return get_status_byte();

        case 0xF0:
            return FW_VERSION;

        case 0xF1:
            return get_status_byte();

        case 0xF2:
            return (uint8_t)(cfg.wake_timer_min >> 8);

        case 0xF3:
            return (uint8_t)(cfg.wake_timer_min & 0xFF);

        case 0xF4:
            return PIND;

        default:
            return 0xFF;
    }
}

/**
 * @brief Initialize TWI as an I2C slave.
 */
static void i2c_init(void)
{
    TWAR = (I2C_ADDR << 1);
    TWCR = (1 << TWEN) | (1 << TWEA) | (1 << TWIE) | (1 << TWINT);
}

/**
 * @brief TWI interrupt handler.
 *
 * Handles both slave receive and slave transmit state-machine events.
 */
ISR(TWI_vect)
{
    uint8_t status = TW_STATUS;

    switch (status) {
        case TW_SR_SLA_ACK:
            rx_state = 0;
            rx_tmp = 0;
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        case TW_SR_DATA_ACK: {
            uint8_t b = TWDR;

            if (rx_state == 1) {
                rx_tmp = ((uint16_t)b << 8);
                rx_state = 2;
                TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
                break;
            }

            if (rx_state == 2) {
                rx_tmp |= b;
                deferred_wake_timer =
                    (rx_tmp > MAX_WAKE_TIMER_MIN) ? MAX_WAKE_TIMER_MIN : rx_tmp;
                timer_update_pending = true;
                rx_state = 0;
                TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
                break;
            }

            switch (b) {
                case 0x00:
                    i2c_reg_ptr = 0x00;
                    break;

                case 0x01:
                    pi_shutdown_requested = 1;
                    shutdown_delay_active = true;
                    shutdown_delay_seconds = SHUTDOWN_DELAY_SECONDS;
                    break;

                case 0x02:
                    relay_req = RELAY_REQ_ON;
                    pi_shutdown_requested = 0;
                    shutdown_delay_active = false;
                    shutdown_delay_seconds = 0;
                    break;

                case 0x03:
                    led_red(false);
                    break;

                case 0x04:
                    led_red(true);
                    break;

                case 0x05:
                    led_green(false);
                    break;

                case 0x06:
                    led_green(true);
                    break;

                case CMD_SET_WAKE_TIMER:
                    rx_state = 1;
                    rx_tmp = 0;
                    break;

                case 0xF0:
                case 0xF1:
                case 0xF2:
                case 0xF3:
                case 0xF4:
                    i2c_reg_ptr = b;
                    break;

                default:
                    i2c_reg_ptr = 0x00;
                    break;
            }

            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
        } break;

        case TW_ST_SLA_ACK:
            TWDR = read_reg(i2c_reg_ptr);
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        case TW_ST_DATA_ACK:
            i2c_reg_ptr++;
            TWDR = read_reg(i2c_reg_ptr);
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        case TW_ST_DATA_NACK:
            i2c_reg_ptr = 0x00;
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        case TW_ST_LAST_DATA:
            i2c_reg_ptr = 0x00;
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        case TW_SR_STOP:
            rx_state = 0;
            rx_tmp = 0;
            TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            break;

        default:
            rx_state = 0;
            rx_tmp = 0;

            if (status == TW_BUS_ERROR || status == 0xB0) {
                TWCR = 0;
                _delay_us(10);
                i2c_init();
                twi_error_blink_pending = true;
            } else {
                TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
            }
            break;
    }
}

/* =========================================================
 * INITIALIZATION
 * ========================================================= */

/**
 * @brief Force the latching relay ON during boot.
 */
static void boot_force_relay_on(void)
{
    PORTD &= ~((1 << RELAY_SET) | (1 << RELAY_RESET));

    _delay_ms(2);

    relay_pulse(RELAY_SET);
    relay_state = 1;

    pi_shutdown_requested = 0;
    shutdown_sleep_active = false;
    shutdown_delay_active = false;
    shutdown_delay_seconds = 0;
    wake_flag = 0;
    timer_expired = false;
}

/**
 * @brief Initialize GPIO and low-level peripherals.
 */
static void io_init(void)
{
    DDRC |= (1 << GREEN_LED) | (1 << RED_LED);
    PORTC |= (1 << GREEN_LED) | (1 << RED_LED);

    DDRD |= (1 << RELAY_SET) | (1 << RELAY_RESET);
    PORTD &= ~((1 << RELAY_SET) | (1 << RELAY_RESET));

    DDRD &= ~((1 << INT0_PIN) | (1 << INT1_PIN));

    PORTD |= (1 << INT0_PIN);
    PORTD |= (1 << INT1_PIN);

    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2) |
            (1 << PB6) | (1 << PB7);

    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) |
               (1 << PB6) | (1 << PB7));

    DIDR0 = 0x3F;
    DIDR1 |= (1 << AIN0D) | (1 << AIN1D);

    ADCSRA &= ~(1 << ADEN);
    ACSR |= (1 << ACD);

    extint_config_run_edges();
}

/* =========================================================
 * MAIN
 * ========================================================= */

/**
 * @brief Firmware entry point.
 *
 * Initializes hardware, loads persistent configuration, services deferred
 * relay and timer updates, and manages shutdown sleep/wake behavior.
 *
 * @return Never returns.
 */
int main(void)
{
    wdt_disable();

    cli();

    OSCCAL = CALIBRATED_OSCCAL;

    io_init();
    boot_force_relay_on();
    i2c_init();

    sei();

    if (eeprom_load_cfg()) {
        led_blink(GREEN_LED, 5);
    } else {
        eeprom_default_cfg();
        eeprom_save_cfg();
        led_blink(RED_LED, 5);
    }

    ac_ok_prev = (PIND & (1 << INT1_PIN)) ? 1 : 0;

    wdt_enable(HW_WDT_TIMEOUT);

    for (;;) {
        wdt_reset();

        if (twi_error_blink_pending) {
            uint8_t s = SREG;

            cli();
            twi_error_blink_pending = false;
            SREG = s;
        }

        {
            relay_req_t r;
            uint8_t s = SREG;

            cli();
            r = relay_req;
            relay_req = RELAY_REQ_NONE;
            SREG = s;

            if (r == RELAY_REQ_ON) {
                relay_apply(true);
            }

            if (r == RELAY_REQ_OFF) {
                relay_apply(false);
            }
        }

        if (timer_update_pending) {
            uint8_t s = SREG;

            cli();
            cfg.wake_timer_min = deferred_wake_timer;
            timer_update_pending = false;
            SREG = s;

            eeprom_save_cfg();
        }

        if (shutdown_delay_active && pi_shutdown_requested) {
            while (shutdown_delay_active &&
                   pi_shutdown_requested &&
                   shutdown_delay_seconds > 0) {

                for (uint8_t j = 0; j < 10; j++) {
                    _delay_ms(100);
                    wdt_reset();

                    if (!shutdown_delay_active || !pi_shutdown_requested) {
                        break;
                    }
                }

                if (shutdown_delay_active &&
                    pi_shutdown_requested &&
                    shutdown_delay_seconds > 0) {
                    shutdown_delay_seconds--;
                }
            }

            if (shutdown_delay_active &&
                pi_shutdown_requested &&
                shutdown_delay_seconds == 0) {
                shutdown_delay_active = false;
                relay_req = RELAY_REQ_OFF;
            }
        }

        if (pi_shutdown_requested && !relay_state) {
            wdt_stop();

            rearm_int0();

            if (!shutdown_sleep_active) {
                reload_wake_timer();
                wdt_ticks = 0;
                shutdown_sleep_active = true;
            }

            prepare_for_sleep_powerdown();
            enter_sleep_powerdown();

            {
                bool full_wake = false;
                bool button_wake = (wake_flag != 0);

                if (button_wake) {
                    wake_flag = 0;

                    _delay_ms(10);

                    if (!(PIND & (1 << INT0_PIN))) {
                        full_wake = true;
                    }
                }

                if (timer_expired) {
                    timer_expired = false;
                    full_wake = true;
                }

                {
                    uint8_t ac_ok_now = (PIND & (1 << INT1_PIN)) ? 1 : 0;

                    if (ac_ok_rising_edge(ac_ok_prev, ac_ok_now)) {
                        full_wake = true;
                    }

                    ac_ok_prev = ac_ok_now;
                }

                if (full_wake) {
                    wdt_stop();
                    restore_after_sleep_powerdown();
                    relay_apply(true);
                    pi_shutdown_requested = 0;
                    shutdown_sleep_active = false;
                    shutdown_delay_active = false;
                    shutdown_delay_seconds = 0;
                    wdt_enable(HW_WDT_TIMEOUT);

                    if (button_wake) {
                        rearm_int0();
                    }
                }
            }
        }

        {
            uint8_t ac_ok_now = (PIND & (1 << INT1_PIN)) ? 1 : 0;

            if (ac_ok_rising_edge(ac_ok_prev, ac_ok_now)) {
                if (pi_shutdown_requested) {
                    timer1_stop();
                    relay_apply(true);
                    pi_shutdown_requested = 0;
                    shutdown_sleep_active = false;
                    shutdown_delay_active = false;
                    shutdown_delay_seconds = 0;
                }
            }

            ac_ok_prev = ac_ok_now;
        }
    }
}
