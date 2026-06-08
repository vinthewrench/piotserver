/**
 * @file powercontrol.c
 * @brief Power Controller Firmware for ATmega88PB.
 *
 * Firmware version: 27
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

static void i2c_init(void);
static void extint_config_run_edges(void);
static void timer1_init_1s(void);
static inline void timer1_stop(void);
static void prepare_for_sleep_powerdown(void);
static void enter_sleep_powerdown(void);
static void restore_after_sleep_powerdown(void);

#define POWERCONTROL_FW_VERSION 27

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#define TIMER1_OCR1A       6695
#define CALIBRATED_OSCCAL  0x80

#define I2C_ADDR 0x08

#define GREEN_LED   PC2
#define RED_LED     PC3

#define RELAY_SET   PD6
#define RELAY_RESET PD5

#define INT0_PIN    PD2
#define INT1_PIN    PD3

#define HW_WDT_TIMEOUT    WDTO_2S
#define WDT_TICKS_PER_MIN 30

#define SHUTDOWN_DELAY_SECONDS 30

#define CMD_SHUTDOWN_REQ      'S'
#define CMD_CANCEL_SHUTDOWN   'C'
#define CMD_RED_ON            'R'
#define CMD_RED_OFF           'r'
#define CMD_GREEN_ON          'G'
#define CMD_GREEN_OFF         'g'

#define CMD_WAKE_CLEAR        '0'
#define CMD_WAKE_1_MIN        '1'
#define CMD_WAKE_5_MIN        '5'
#define CMD_WAKE_15_MIN       'F'
#define CMD_WAKE_60_MIN       'H'
#define CMD_WAKE_8_HOURS      '8'
#define CMD_WAKE_24_HOURS     'D'

#define STATUS_RED_ON         0x02
#define STATUS_GREEN_ON       0x04
#define STATUS_AC_OK          0x08

#define STATUS_WAKE_MASK      0x70
#define STATUS_WAKE_CLEAR     0x00
#define STATUS_WAKE_1_MIN     0x10
#define STATUS_WAKE_5_MIN     0x20
#define STATUS_WAKE_15_MIN    0x30
#define STATUS_WAKE_60_MIN    0x40
#define STATUS_WAKE_8_HOURS   0x50
#define STATUS_WAKE_24_HOURS  0x60

#define EEPROM_SIGNATURE 0x5043
#define EEPROM_VERSION   0x01
#define EEPROM_BASE_ADDR 0x00

typedef struct {
    uint16_t signature;
    uint8_t version;
    uint16_t wake_timer_min;
    uint16_t checksum;
} eeprom_cfg_t;

static eeprom_cfg_t cfg;

static volatile uint16_t wake_timer_min = 0;
static volatile uint8_t timer_seconds = 0;
static volatile bool timer_expired = false;

static volatile uint8_t relay_state = 0;
static volatile uint8_t red_state = 0;
static volatile uint8_t green_state = 0;

static volatile uint8_t wake_flag = 0;

static volatile uint8_t pi_shutdown_requested = 0;
static volatile bool shutdown_sleep_active = false;
static volatile bool shutdown_delay_active = false;
static volatile uint8_t shutdown_delay_seconds = 0;

static uint8_t ac_ok_prev = 0;

static volatile uint8_t i2c_status_cache = 0;
static volatile bool i2c_reinit_pending = false;

static volatile bool wake_timer_save_pending = false;
static volatile uint16_t pending_wake_timer_min = 0;

typedef enum {
    RELAY_REQ_NONE = 0,
    RELAY_REQ_ON,
    RELAY_REQ_OFF
} relay_req_t;

static volatile relay_req_t relay_req = RELAY_REQ_NONE;

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

static uint16_t cfg_checksum(const eeprom_cfg_t *cfgp)
{
    return config_fletcher16(cfgp, offsetof(eeprom_cfg_t, checksum));
}

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

static void eeprom_default_cfg(void)
{
    cfg.signature = EEPROM_SIGNATURE;
    cfg.version = EEPROM_VERSION;
    cfg.wake_timer_min = 0;
    cfg.checksum = cfg_checksum(&cfg);
}

static void eeprom_save_cfg(void)
{
    cfg.checksum = cfg_checksum(&cfg);

    eeprom_update_block((const void *)&cfg,
                        (void *)EEPROM_BASE_ADDR,
                        sizeof(cfg));
}

static uint8_t wake_minutes_to_status_bits(uint16_t minutes)
{

   switch (minutes) {
        case 0:
            return STATUS_WAKE_CLEAR;

        case 1:
            return STATUS_WAKE_1_MIN;

        case 5:
            return STATUS_WAKE_5_MIN;

        case 15:
            return STATUS_WAKE_15_MIN;

        case 60:
            return STATUS_WAKE_60_MIN;

        case 8 * 60:
            return STATUS_WAKE_8_HOURS;

        case 24 * 60:
            return STATUS_WAKE_24_HOURS;

        default:
            return STATUS_WAKE_CLEAR;
    }
}

static inline uint8_t build_status_live(void)
{
    uint8_t s = 0;

    if (red_state) {
        s |= STATUS_RED_ON;
    }

    if (green_state) {
        s |= STATUS_GREEN_ON;
    }

    if (!(PIND & (1 << INT1_PIN))) {
        s |= STATUS_AC_OK;
    }

    s |= wake_minutes_to_status_bits(cfg.wake_timer_min);

    return s;
}

static inline void update_status_cache(void)
{
    /*
     * Main-line code may update the cached byte.
     *
     * Main-line code must NOT write TWDR. TWDR belongs only to i2c_init()
     * and ISR(TWI_vect). Writing TWDR from main can race the TWI shifter.
     */
    i2c_status_cache = build_status_live();
}

static inline void led_red(bool on)
{
    if (on) {
        PORTC &= ~(1 << RED_LED);
        red_state = 1;
    } else {
        PORTC |= (1 << RED_LED);
        red_state = 0;
    }

    update_status_cache();
}

static inline void led_green(bool on)
{
    if (on) {
        PORTC &= ~(1 << GREEN_LED);
        green_state = 1;
    } else {
        PORTC |= (1 << GREEN_LED);
        green_state = 0;
    }

    update_status_cache();
}

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
    update_status_cache();
}

static inline void led_red_physical_toggle(void)
{
    PORTC ^= (1 << RED_LED);
}

static inline void led_red_restore(void)
{
    if (red_state) {
        PORTC &= ~(1 << RED_LED);
    } else {
        PORTC |= (1 << RED_LED);
    }
}

#define RELAY_PULSE_MS 30

static inline void relay_pulse(uint8_t pin)
{
    PORTD &= ~((1 << RELAY_SET) | (1 << RELAY_RESET));
    PORTD |= (1 << pin);
    _delay_ms(RELAY_PULSE_MS);
    PORTD &= ~(1 << pin);
}

static inline void relay_apply(bool on)
{
    if (on) {
        relay_pulse(RELAY_SET);
        relay_state = 1;
    } else {
        relay_pulse(RELAY_RESET);
        relay_state = 0;
    }

    update_status_cache();
}

static volatile uint8_t wdt_ticks = 0;

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

static inline void wdt_stop(void)
{
    cli();
    wdt_reset();

    WDTCSR = (1 << WDCE) | (1 << WDE);
    WDTCSR = 0;

    sei();
}

static void prepare_for_sleep_powerdown(void)
{
    timer1_stop();

    TWCR = 0;

    /*
     * AC_OK is externally biased. Disable the internal pull-up while asleep.
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

static void restore_after_sleep_powerdown(void)
{
    cli();

    PRR = 0;

    PORTD |= (1 << INT1_PIN);

    update_status_cache();
    i2c_init();
    extint_config_run_edges();

    led_red(red_state);
    led_green(green_state);
    update_status_cache();

    sei();
}

static void extint_config_run_edges(void)
{
    /*
     * INT0: falling edge for active-low wake button.
     */
    EXTINT_CTRL_REG |= (1 << ISC01);
    EXTINT_CTRL_REG &= ~(1 << ISC00);

    /*
     * INT1: rising edge. Used only as a sleep wake source.
     */
    EXTINT_CTRL_REG |= (1 << ISC11) | (1 << ISC10);

    EXTINT_FLAG_REG |= (1 << INTF0) | (1 << INTF1);

    EXTINT_MASK_REG |= (1 << INT0);
    EXTINT_MASK_REG &= ~(1 << INT1);
}

ISR(INT0_vect)
{
    EXTINT_MASK_REG &= ~(1 << INT0);
    wake_flag = 1;
}

ISR(INT1_vect)
{
}

static inline void rearm_int0(void)
{
    EXTINT_FLAG_REG |= (1 << INTF0);
    EXTINT_MASK_REG |= (1 << INT0);
}

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

static inline void timer1_stop(void)
{
    TCCR1B = 0;
    TIMER1_TIMSK_REG &= ~(1 << OCIE1A);
    TIMER1_TIFR_REG |= (1 << OCF1A);
}

static inline void reload_wake_timer(void)
{
    timer1_stop();

    wake_timer_min = cfg.wake_timer_min;

    if (wake_timer_min > 0) {
        timer_seconds = 59;
        timer1_init_1s();
    }
}

static void timer1_init_1s(void)
{
    TCCR1A = 0;
    TCCR1B = 0;

    TCCR1B |= (1 << WGM12);
    OCR1A = TIMER1_OCR1A;

    TIMER1_TIMSK_REG |= (1 << OCIE1A);

    TCCR1B |= (1 << CS10) | (1 << CS12);
}

static inline bool ac_ok_rising_edge(uint8_t prev, uint8_t now)
{
    return (!prev && now);
}

static inline void twi_arm_ack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN) | (1 << TWIE);
}

static inline void request_wake_timer_save(uint16_t minutes)
{
    pending_wake_timer_min = minutes;
    wake_timer_save_pending = true;
}

static inline void process_command_byte(uint8_t cmd)
{
    switch (cmd) {
        case CMD_SHUTDOWN_REQ:
            pi_shutdown_requested = 1;
            shutdown_delay_active = true;
            shutdown_delay_seconds = SHUTDOWN_DELAY_SECONDS;
            update_status_cache();
            break;

        case CMD_CANCEL_SHUTDOWN:
            pi_shutdown_requested = 0;
            shutdown_delay_active = false;
            shutdown_delay_seconds = 0;
            update_status_cache();
            break;

        case CMD_RED_ON:
            led_red(true);
            break;

        case CMD_RED_OFF:
            led_red(false);
            break;

        case CMD_GREEN_ON:
            led_green(true);
            break;

        case CMD_GREEN_OFF:
            led_green(false);
            break;

        case CMD_WAKE_CLEAR:
            request_wake_timer_save(0);
            break;

        case CMD_WAKE_1_MIN:
            request_wake_timer_save(1);
            break;

        case CMD_WAKE_5_MIN:
            request_wake_timer_save(5);
            break;

        case CMD_WAKE_15_MIN:
            request_wake_timer_save(15);
            break;

        case CMD_WAKE_60_MIN:
            request_wake_timer_save(60);
            break;

        case CMD_WAKE_8_HOURS:
            request_wake_timer_save(8 * 60);
            break;

        case CMD_WAKE_24_HOURS:
            request_wake_timer_save(24 * 60);
            break;

        default:
            break;
    }
}

static void i2c_init(void)
{
    i2c_reinit_pending = false;

    TWCR = 0;

    TWAR = (I2C_ADDR << 1);

    /*
     * Preload TWDR before enabling TWI.
     *
     * This fixes the observed first-byte phase problem on repeated bare reads.
     */
    update_status_cache();
    TWDR = i2c_status_cache;

    TWCR =
        (1 << TWINT) |
        (1 << TWEA) |
        (1 << TWEN) |
        (1 << TWIE);
}

ISR(TWI_vect)
{
    uint8_t status = TW_STATUS;

    switch (status) {

        case TW_SR_SLA_ACK:
        case TW_SR_ARB_LOST_SLA_ACK:
            twi_arm_ack();
            break;

        case TW_SR_DATA_ACK:
        case TW_SR_DATA_NACK:
            process_command_byte(TWDR);
            update_status_cache();
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        case TW_SR_STOP:
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        case TW_ST_SLA_ACK:
        case TW_ST_ARB_LOST_SLA_ACK:
            /*
             * Critical: do not rebuild status here.
             * Keep this path short. Just load the already-cached byte.
             */
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        case TW_ST_DATA_ACK:
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        case TW_ST_DATA_NACK:
        case TW_ST_LAST_DATA:
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        case TW_BUS_ERROR:
            i2c_reinit_pending = true;
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;

        default:
            TWDR = i2c_status_cache;
            twi_arm_ack();
            break;
    }
}

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

    update_status_cache();
}

static void io_init(void)
{
    DDRC |= (1 << GREEN_LED) | (1 << RED_LED);
    PORTC |= (1 << GREEN_LED) | (1 << RED_LED);

    red_state = 0;
    green_state = 0;

    DDRD |= (1 << RELAY_SET) | (1 << RELAY_RESET);
    PORTD &= ~((1 << RELAY_SET) | (1 << RELAY_RESET));

    DDRD &= ~((1 << INT0_PIN) | (1 << INT1_PIN));

    PORTD |= (1 << INT0_PIN);
    PORTD |= (1 << INT1_PIN);

    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2) |
            (1 << PB6) | (1 << PB7);

    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) |
               (1 << PB6) | (1 << PB7));

    /*
     * Do not disable digital input buffers on PC4/PC5.
     *
     * PC4 = SDA
     * PC5 = SCL
     *
     * DIDR0 = 0x3F disables ADC0..ADC5 digital input buffers and includes
     * SDA/SCL on ATmega88PB. That is not safe for TWI/I2C.
     */
    DIDR0 = 0x00;

    DIDR1 |= (1 << AIN0D) | (1 << AIN1D);

    ADCSRA &= ~(1 << ADEN);
    ACSR |= (1 << ACD);

    extint_config_run_edges();
    update_status_cache();
}

int main(void)
{
    bool cfg_loaded = false;

    wdt_disable();

    cli();

    OSCCAL = CALIBRATED_OSCCAL;

    /*
     * cfg.wake_timer_min is used by build_status_live().
     * Therefore cfg must be valid before io_init(), boot_force_relay_on(),
     * or i2c_init(), because those can build/preload the I2C status byte.
     */
    cfg_loaded = eeprom_load_cfg();

    if (!cfg_loaded) {
        eeprom_default_cfg();
        eeprom_save_cfg();
    }

    io_init();
    boot_force_relay_on();
    i2c_init();

    sei();

    if (cfg_loaded) {
        led_blink(GREEN_LED, 5);
    } else {
        led_blink(RED_LED, 5);
    }

    /*
     * Blink changes LED state and temporarily masks TWI interrupts.
     * Rebuild status and reinitialize TWI afterward.
     */
    update_status_cache();
    i2c_init();

    ac_ok_prev = (PIND & (1 << INT1_PIN)) ? 1 : 0;

    wdt_enable(HW_WDT_TIMEOUT);

    for (;;) {
        wdt_reset();

        if (wake_timer_save_pending) {
            uint16_t t;
            uint8_t s = SREG;

            cli();
            t = pending_wake_timer_min;
            wake_timer_save_pending = false;
            SREG = s;

            cfg.wake_timer_min = t;
            eeprom_save_cfg();
            update_status_cache();
        }

        if (i2c_reinit_pending) {
            uint8_t s = SREG;

            cli();
            i2c_reinit_pending = false;
            SREG = s;

            i2c_init();
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

        if (shutdown_delay_active && pi_shutdown_requested) {
            while (shutdown_delay_active &&
                   pi_shutdown_requested &&
                   shutdown_delay_seconds > 0) {

                for (uint8_t j = 0; j < 10; j++) {
                    if ((j == 0) || (j == 5)) {
                        led_red_physical_toggle();
                    }

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

            led_red_restore();
            update_status_cache();

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
                    update_status_cache();
                }

                if (full_wake) {
                    wdt_stop();
                    restore_after_sleep_powerdown();
                    relay_apply(true);

                    pi_shutdown_requested = 0;
                    shutdown_sleep_active = false;
                    shutdown_delay_active = false;
                    shutdown_delay_seconds = 0;

                    update_status_cache();
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

                    update_status_cache();
                }
            }

            ac_ok_prev = ac_ok_now;
            update_status_cache();
        }
    }
}
