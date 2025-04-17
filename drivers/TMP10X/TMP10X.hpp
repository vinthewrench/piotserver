//
//  TMP10X.hpp
//  pumphouse
//
//  Created by Vincent Moscaritolo on 9/10/21.
//

#ifndef TMP10X_hpp
#define TMP10X_hpp

#include <stdio.h>
#include "I2C.hpp"

class TMP10X
{
 
public:
	TMP10X();
	~TMP10X();
 
	// Address of Temperature sensor (0x48,0x49,0x4A,0x4B)

    bool begin(uint8_t deviceAddress = 0x48);
    bool begin(uint8_t deviceAddress,  int &error);

	void stop();
	bool isOpen();
	
	bool readTempC(float&);
	bool readTempF(float&);
	
	uint8_t	getDevAddr();

private:
	
	I2C 		_i2cPort;
	bool		_isSetup;

};

#endif /* TMP10X_hpp */
