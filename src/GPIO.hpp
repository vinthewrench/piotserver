//
//  GPIO.hpp
//  pumphouse
//
//  Created by Vincent Moscaritolo on 12/21/21.
//

#ifndef GPIO_hpp
#define GPIO_hpp
 
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>     /* va_list, va_start, va_arg, va_end */
#include <time.h>
#include <termios.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>
#include <tuple>

#if defined(__APPLE__)
// used for cross compile on osx
#include "macos_gpiod.h"
#else
#include <gpiod.h>
#endif

using namespace std;

class GPIO  {
	
public:


    typedef enum {
        GPIO_DIRECTION_INPUT    = GPIOD_LINE_DIRECTION_INPUT,
        GPIO_DIRECTION_OUTPUT   = GPIOD_LINE_DIRECTION_OUTPUT,
    }gpio_direction_t;
 
    typedef struct {
        uint8_t             lineNo;
        gpio_direction_t    direction;
        int                 flags;
  } gpio_pin_t;
 
	typedef vector<pair<uint8_t, bool>> gpioStates_t;

	GPIO();
	~GPIO();

	
    bool begin(vector<gpio_pin_t> pins, int  &error);
    
	void stop();

	bool isAvailable();

	bool set(gpioStates_t states);
	bool get(gpioStates_t &states);

private:
 
    typedef struct {
        uint8_t             lineNo;
        gpio_direction_t    direction;
        struct gpiod_line   *line;
     } gpio_line_t;

    vector<gpio_line_t>      _lines;

 	struct gpiod_chip* 		_chip;
	  
	bool 					_isSetup;

};

#endif /* GPIO_hpp */
