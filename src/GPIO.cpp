//
//  GPIO.cpp
//  pumphouse
//
//  Created by Vincent Moscaritolo on 12/21/21.
//

#include "GPIO.hpp"
#include <algorithm>

#include "LogMgr.hpp"

GPIO::GPIO(){
	_isSetup = false;
	_chip = NULL;
 }


GPIO::~GPIO(){
	stop();
	
}

bool GPIO::begin(vector<gpio_pin_t> pinsIn, int  &error){
    
    int err = 0;
    
    static const char *consumer = "pIoTServer";
    
    // open the device
    _chip = gpiod_chip_open_by_name("gpiochip4");
    
    if(_chip == NULL)
        _chip = gpiod_chip_open_by_name("gpiochip0");
    
    if(!_chip) {
        LOGT_ERROR("Error open GPIO chip: %s",strerror(errno));
        goto cleanup;
    }
    
    for(auto &p :pinsIn){
        auto line = gpiod_chip_get_line(_chip, p.lineNo);
        if(line){
              if(p.direction == GPIO_DIRECTION_OUTPUT){
                
                err = gpiod_line_request_output_flags(line, consumer, GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW, 0);
                if(err < 0){
                    LOGT_ERROR("gpiod_line_request_output(%d) failed : %s",p.lineNo, strerror(err));
                    gpiod_line_release(line);
                    goto cleanup;
                };
//                printf("gpiod_line_request_output(%d)\n", p.lineNo);
            }
            
            else if(p.direction == GPIO_DIRECTION_INPUT){
                err = gpiod_line_request_input_flags(line, consumer, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP);
                if(err < 0){
                    LOGT_ERROR("gpiod_line_request_input_flags(%d) failed : %s",p.lineNo, strerror(err));
                    gpiod_line_release(line);
                    goto cleanup;
                };
 //               printf("gpiod_line_request_input_flags(%d)\n", p.lineNo);
      
            }
            else {
                LOGT_ERROR("gpiod direction(%d) invalid = %d",p.lineNo, p.direction);
                gpiod_line_release(line);
                goto cleanup;
            };
            _lines.push_back({
                .lineNo = p.lineNo,
                .direction = p.direction,
                .line = line
            });
        }
    }
    
    _isSetup = true;
    return true;
    
    
cleanup:
    if(_chip){
        for(auto &p :_lines){
            if(p.line) gpiod_line_release(p.line);
        }
        _lines.clear();
        gpiod_chip_close(_chip);
    }
    _isSetup = false;
    
    
    return false;
}

void GPIO::stop(){

	if(_isSetup){
        for( auto &p :_lines){
            if(p.line)
                gpiod_line_release(p.line);
        }
        _lines.clear();
 
        gpiod_chip_close(_chip);
		_chip = NULL;
	}
	
	_isSetup = false;
}


bool GPIO::isAvailable(){
 
	return _isSetup;
}


bool GPIO::set(gpioStates_t states){
    
    if(!_isSetup)
        return false;
    
    int  error = 0;
    
    for(auto &s :states){
        for(auto &l : _lines){
            if(s.first == l.lineNo){
                error = gpiod_line_set_value(l.line, s.second);
                
  //              printf("gpiod_line_set_value(%p, %d)\n",  (void*) l.line, s.second);

                if(error) {
                    LOGT_ERROR("Failed set line values GPIO line %d %s ", l.lineNo,strerror(error));
                    return false;
                }
                break;
            }
        }
    }
    
    return true;
}

bool GPIO::get(gpioStates_t &statesOut){
    
    gpioStates_t states;
    
    if(!_isSetup)
        return false;
    
// get the current state of lines
    for(auto &p :_lines){
        int val = gpiod_line_get_value(p.line);
        
//        printf("gpiod_line_get_value(%p,  %d) = %d\n",  (void*) p.line,p.lineNo,  val);

        if(val < 0){
            LOGT_ERROR("Failed get line values GPIO line %d %s ", p.lineNo,strerror(val));
         }
        else{
            states.push_back(make_pair(p.lineNo, val?true:false));
        }
     }
    
    statesOut = states;
    return true;
}
