//
//  QwiicButton.hpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 12/21/21.
//

#ifndef QwiicButton_hpp
#define QwiicButton_hpp

#include <stdio.h>
#include "I2C.hpp"


using namespace std;

class QwiicButton
{
 
public:
	QwiicButton();
	~QwiicButton();
 
	// Address of QwiicButton (0x6E,0x6F)
 	bool begin(uint8_t deviceAddress = 0x6F);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
	bool isOpen();
 
	uint8_t	getDevAddr();

	 	//Button status/config

	/**
	 * @brief test button/switch is pressed
	 * @return Returns 1 if the button/switch is pressed, and 0 otherwise
	*/
	bool isPressed( bool &state);

	/**
	 * @brief test button/switch is clicked
	 * @return Returns 1 if the button/switch is clicked, and 0 otherwise
	*/
	bool hasBeenClicked(bool &state);
 
	/**
	 * @brief Sets isPressed, hasBeenClicked, and eventAvailable to zero
	 * @return Returns true if set is sucessfull
	*/
	bool clearEventBits();
 
	
	bool LEDconfig(uint8_t brightness, uint16_t cycleTime,
						uint16_t offTime, uint8_t granularity = 1); //Configures the LED with the given max brightness, granularity (1 is fine for most applications), cycle time, and off time.
	bool LEDoff();                                             //Turns the onboard LED off
	bool LEDon(uint8_t brightness = 255);                      //Turns the onboard LED on with specified brightness. Set brightness to an integer between 0 and 255, where 0 is off and 255 is max brightness.

	/**
	 * @brief Get the device type
	 * @param deviceType 0 if there is no device attached, 1 if a button is attached, 2 if a switch is attached
	 * @return true if device is connected . false if not connected
 	*/
	bool getDeviceType(uint8_t &deviceType);
 
	/**
	 * @brief /Returns the firmware version of the attached device as a 16-bit integer.
	 * @param version The leftmost (high) byte is the major revision number, and the rightmost (low) byte is the minor revision number..
	 * @return true if device is connected . false if not connected
 	*/
	bool getFirmwareVersion(uint16_t & version);
 
private:
	
	I2C 		_i2cPort;
	bool		_isSetup;

};

#endif /* QwiicButton_hpp */
