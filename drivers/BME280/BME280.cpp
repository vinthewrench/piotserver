//
//  BME280.cpp
//  bmetest
//
//  Created by vinnie on 12/8/24.
//

#include "BME280.hpp"
#include <stdio.h>
#include <cstring>            //Needed for memset and string functions
#include "LogMgr.hpp"

/*
 extern "C" {

static void dumpHex(uint8_t* buffer, int length, int offset)
{
    char hexDigit[] = "0123456789ABCDEF";
    int            i;
    int                        lineStart;
    int                        lineLength;
    short                    c;
    const unsigned char      *bufferPtr = buffer;
    
    char                    lineBuf[1024];
    char                    *p;
    
#define kLineSize    8
    for (lineStart = 0, p = lineBuf; lineStart < length; lineStart += lineLength,  p = lineBuf )
    {
        lineLength = kLineSize;
        if (lineStart + lineLength > length)
            lineLength = length - lineStart;
        
        p += sprintf(p, "%6d: ", lineStart+offset);
        for (i = 0; i < lineLength; i++){
            *p++ = hexDigit[ bufferPtr[lineStart+i] >>4];
            *p++ = hexDigit[ bufferPtr[lineStart+i] &0xF];
            if((lineStart+i) &0x01)  *p++ = ' ';  ;
        }
        for (; i < kLineSize; i++)
            p += sprintf(p, "   ");
        
        p += sprintf(p,"  ");
        for (i = 0; i < lineLength; i++) {
            c = bufferPtr[lineStart + i] & 0xFF;
            if (c > ' ' && c < '~')
                *p++ = c ;
            else {
                *p++ = '.';
            }
        }
        *p++ = 0;
        
        
        printf("%s\n",lineBuf);
    }
#undef kLineSize
}
 }
*/

/* Sensor driver code for BME280 adpated from
  https://github https://github.com/boschsensortec/BME280_DeviceAPI
*/

/*! @name BME280 chip identifier */
#define BME280_CHIP_ID                            UINT8_C(0x60)
 
/*! @name Register Address */
#define BME280_REG_CHIP_ID                        UINT8_C(0xD0)
#define BME280_REG_RESET                          UINT8_C(0xE0)
#define BME280_REG_TEMP_PRESS_CALIB_DATA          UINT8_C(0x88)
#define BME280_REG_HUMIDITY_CALIB_DATA            UINT8_C(0xE1)
#define BME280_REG_CTRL_HUM                       UINT8_C(0xF2)
#define BME280_REG_STATUS                         UINT8_C(0xF3)
#define BME280_REG_PWR_CTRL                       UINT8_C(0xF4)
#define BME280_REG_CTRL_MEAS                      UINT8_C(0xF4)
#define BME280_REG_CONFIG                         UINT8_C(0xF5)
#define BME280_REG_DATA                           UINT8_C(0xF7)

/*! @name Sensor power modes */
#define BME280_Device_MODE_MSK                    UINT8_C(0x03)
#define BME280_Device_MODE_POS                    UINT8_C(0x00)


/*! @name Soft reset command */
#define BME280_SOFT_RESET_COMMAND                 UINT8_C(0xB6)

#define BME280_STATUS_IM_UPDATE                   UINT8_C(0x01)
#define BME280_STATUS_MEAS_DONE                   UINT8_C(0x08)

/*! @name Measurement delay calculation macros  */
#define BME280_MEAS_OFFSET                        UINT16_C(1250)
#define BME280_MEAS_DUR                           UINT16_C(2300)
#define BME280_PRES_HUM_MEAS_OFFSET               UINT16_C(575)
#define BME280_MEAS_SCALING_FACTOR                UINT16_C(1000)
#define BME280_STARTUP_DELAY                      UINT16_C(2000)

/*! @name Macros related to size */
#define BME280_LEN_TEMP_PRESS_CALIB_DATA          UINT8_C(26)
#define BME280_LEN_HUMIDITY_CALIB_DATA            UINT8_C(7)
#define BME280_LEN_P_T_H_DATA                     UINT8_C(8)

#define BME280_PRESS                              UINT8_C(1)
#define BME280_TEMP                               UINT8_C(1 << 1)
#define BME280_HUM                                UINT8_C(1 << 2)
#define BME280_ALL                                UINT8_C(0x07)

/*! @name Settings selection macros */
#define BME280_SEL_OSR_PRESS                      UINT8_C(1)
#define BME280_SEL_OSR_TEMP                       UINT8_C(1 << 1)
#define BME280_SEL_OSR_HUM                        UINT8_C(1 << 2)
#define BME280_SEL_FILTER                         UINT8_C(1 << 3)
#define BME280_SEL_STANDBY                        UINT8_C(1 << 4)
#define BME280_SEL_ALL_SETTINGS                   UINT8_C(0x1F)

/*! @name Oversampling macros */
#define BME280_NO_OVERSAMPLING                    UINT8_C(0x00)
#define BME280_OVERSAMPLING_1X                    UINT8_C(0x01)
#define BME280_OVERSAMPLING_2X                    UINT8_C(0x02)
#define BME280_OVERSAMPLING_4X                    UINT8_C(0x03)
#define BME280_OVERSAMPLING_8X                    UINT8_C(0x04)
#define BME280_OVERSAMPLING_16X                   UINT8_C(0x05)
#define BME280_OVERSAMPLING_MAX                   UINT8_C(16)

#define BME280_CTRL_HUM_MSK                       UINT8_C(0x07)
#define BME280_CTRL_HUM_POS                       UINT8_C(0x00)
#define BME280_CTRL_PRESS_MSK                     UINT8_C(0x1C)
#define BME280_CTRL_PRESS_POS                     UINT8_C(0x02)
#define BME280_CTRL_TEMP_MSK                      UINT8_C(0xE0)
#define BME280_CTRL_TEMP_POS                      UINT8_C(0x05)

/*! @name Filter coefficient selection macros */
#define BME280_FILTER_COEFF_OFF                   (0x00)
#define BME280_FILTER_COEFF_2                     (0x01)
#define BME280_FILTER_COEFF_4                     (0x02)
#define BME280_FILTER_COEFF_8                     (0x03)
#define BME280_FILTER_COEFF_16                    (0x04)

#define BME280_FILTER_MSK                         UINT8_C(0x1C)
#define BME280_FILTER_POS                         UINT8_C(0x02)

/*! @name Standby duration selection macros */
#define BME280_STANDBY_TIME_0_5_MS                (0x00)
#define BME280_STANDBY_TIME_62_5_MS               (0x01)
#define BME280_STANDBY_TIME_125_MS                (0x02)
#define BME280_STANDBY_TIME_250_MS                (0x03)
#define BME280_STANDBY_TIME_500_MS                (0x04)
#define BME280_STANDBY_TIME_1000_MS               (0x05)
#define BME280_STANDBY_TIME_10_MS                 (0x06)
#define BME280_STANDBY_TIME_20_MS                 (0x07)

#define BME280_STANDBY_MSK                        UINT8_C(0xE0)
#define BME280_STANDBY_POS                        UINT8_C(0x05)

/*! @name Bit shift macros */
#define BME280_12_BIT_SHIFT                       UINT8_C(12)
#define BME280_8_BIT_SHIFT                        UINT8_C(8)
#define BME280_4_BIT_SHIFT                        UINT8_C(4)
 

/*! @name Macro to combine two 8 bit data's to form a 16 bit data */
#define BME280_CONCAT_BYTES(msb, lsb)             (((uint16_t)msb << 8) | (uint16_t)lsb)
 
/*! @name Sensor component selection macros
 * These values are internal for API implementation. Don't relate this to
 * data sheet.
 */

/*! @name Macro to SET and GET BITS of a register */
#define BME280_SET_BITS(reg_data, bitname, data) \
    ((reg_data & ~(bitname##_MSK)) | \
     ((data << bitname##_POS) & bitname##_MSK))

#define BME280_SET_BITS_POS_0(reg_data, bitname, data) \
    ((reg_data & ~(bitname##_MSK)) | \
     (data & bitname##_MSK))

#define BME280_GET_BITS(reg_data, bitname)        ((reg_data & (bitname##_MSK)) >> \
                                                   (bitname##_POS))
#define BME280_GET_BITS_POS_0(reg_data, bitname)  (reg_data & (bitname##_MSK))

/*!
 * @brief bme280 sensor structure which comprises of uncompensated temperature,
 * pressure and humidity data
 */
struct bme280_uncomp_data
{
    /*! Un-compensated pressure */
    uint32_t pressure;

    /*! Un-compensated temperature */
    uint32_t temperature;

    /*! Un-compensated humidity */
    uint32_t humidity;
};

/*!
 * bme280 sensor settings structure which comprises of mode,
 * oversampling and filter settings.
 */
struct bme280_settings
{
    /*! Pressure oversampling */
    uint8_t osr_p;

    /*! Temperature oversampling */
    uint8_t osr_t;

    /*! Humidity oversampling */
    uint8_t osr_h;

    /*! Filter coefficient */
    uint8_t filter;

    /*! Standby time */
    uint8_t standby_time;
};



BME280::BME280(){
    _isSetup = false;
}

BME280::~BME280(){
    stop();
    
}


bool BME280::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool BME280::begin(uint8_t deviceAddress,    int &error){
    
    uint8_t  chipID = 0;
    
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
    
    if( !getChipID(chipID)){
        LOGT_INFO("BME280(%02x) READ BME280_REG_CHIP_ID failed \n", deviceAddress );
        error = ENODEV;
        return  false;
    }
    
    if(chipID != BME280_CHIP_ID)  {
        LOGT_INFO("BME280(%02x) unexpected chipID = %02x\n", deviceAddress, chipID );
        error = ENODEV;
        return  false;
    }
  
    if( !softReset()){
        LOGT_INFO("BME280(%02x) SOFT RESET failed \n", deviceAddress );
        error = ENODEV;
        return  false;
    }
  
    if( !getCalibrationData()){
        LOGT_INFO("BME280(%02x) GET CALIBRATION DATA failed \n", deviceAddress );
        error = ENODEV;
        return  false;
    }
    
    if( !setPowerMode(BME280_POWERMODE_SLEEP)){
        LOGT_INFO("BME280(%02x) SET POWER MODE TO BME280_POWERMODE_SLEEP failed \n", deviceAddress );
        error = ENODEV;
        return  false;
    }

    if( !configureForWeather()){
        LOGT_INFO("BME280(%02x) CONFIG FOR WEATHER failed \n", deviceAddress );
        error = ENODEV;
        return  false;
    }

    _isSetup = true;
    
    return _isSetup;
}
 
void BME280::stop(){
//    LOGT_INFO("BME280(%02x) stop\n",  _i2cPort.getDevAddr());

    _isSetup = false;
    _i2cPort.stop();
}
 
bool BME280::isOpen(){
    return _i2cPort.isAvailable() && _isSetup;
    
};


uint8_t    BME280::getDevAddr(){
    return _i2cPort.getDevAddr();
};


bool BME280::getChipID(uint8_t &chipID){
    
    bool success = _i2cPort.readByte(BME280_REG_CHIP_ID, chipID);
    return success;
}

bool BME280::softReset(){

    /*
     The “reset” register contains the soft reset word reset[7:0].
     If the value 0xB6 is written to the register, the device is reset using the complete power-on-reset
     procedure. Writing other values than 0xB6 has no effect. The readout value is always 0x00.
     
     Status register update bit is  Automatically set to ‘1’ when the NVM data are being
     copied to image registers and back to ‘0’ when the copying is done.
     The data are copied at power-on-reset and before every conversion.
     */
    
    bool success = false;
    
    uint8_t status_reg = 0;
    uint8_t try_run = 5;

    if(!_i2cPort.writeByte(BME280_REG_RESET, BME280_SOFT_RESET_COMMAND) )
        return false;
    
    do{
        usleep(BME280_STARTUP_DELAY);
 
        if(!_i2cPort.readByte(BME280_REG_STATUS,status_reg))
            return false;
   
    }  while (try_run-- && (status_reg & BME280_STATUS_MEAS_DONE));
    
    success = (status_reg & BME280_STATUS_IM_UPDATE) == 0;
 
    return success;
}


/*  BME280 output consists of the ADC output values. However, each sensing element behaves
 differently. Therefore, the actual pressure and temperature must be calculated using a set of
 calibration parameters.
 
 he trimming parameters are programmed into the devices’ non-volatile memory (NVM) during
 production and cannot be altered by the customer. Each compensation word is a 16-bit signed or
 unsigned integer value stored in two’s complement. As the memory is organized into 8-bit words, two
 words must always be combined in order to represent the compensation word. The 8-bit registers are
 named calib00…calib41 and are stored at memory addresses 0x88…0xA1 and 0xE1…0xE7. The
 corresponding compensation words are named dig_T# for temperature compensation related values,
 dig_P# for pressure related values and dig_H# for humidity related values.
 
 */


/*!
 *  @brief This internal API is used to parse the temperature and
 *  pressure calibration data and store it in device structure.
 */
static bool  parse_temp_press_calib_data(const uint8_t *reg_data , struct bme280_calib_data &d)
{
    
    d.dig_t1 = BME280_CONCAT_BYTES(reg_data[1], reg_data[0]);
    d.dig_t2 = (int16_t)BME280_CONCAT_BYTES(reg_data[3], reg_data[2]);
    d.dig_t3 = (int16_t)BME280_CONCAT_BYTES(reg_data[5], reg_data[4]);
    d.dig_p1 = BME280_CONCAT_BYTES(reg_data[7], reg_data[6]);
    d.dig_p2 = (int16_t)BME280_CONCAT_BYTES(reg_data[9], reg_data[8]);
    d.dig_p3 = (int16_t)BME280_CONCAT_BYTES(reg_data[11], reg_data[10]);
    d.dig_p4 = (int16_t)BME280_CONCAT_BYTES(reg_data[13], reg_data[12]);
    d.dig_p5 = (int16_t)BME280_CONCAT_BYTES(reg_data[15], reg_data[14]);
    d.dig_p6 = (int16_t)BME280_CONCAT_BYTES(reg_data[17], reg_data[16]);
    d.dig_p7 = (int16_t)BME280_CONCAT_BYTES(reg_data[19], reg_data[18]);
    d.dig_p8 = (int16_t)BME280_CONCAT_BYTES(reg_data[21], reg_data[20]);
    d.dig_p9 = (int16_t)BME280_CONCAT_BYTES(reg_data[23], reg_data[22]);
    d.dig_h1 = reg_data[25];
    return true;
}

static bool  parse_humidity_calib_data(const uint8_t *reg_data,  struct bme280_calib_data &d)
{
      int16_t dig_h4_lsb;
      int16_t dig_h4_msb;
      int16_t dig_h5_lsb;
      int16_t dig_h5_msb;

      d.dig_h2 = (int16_t)BME280_CONCAT_BYTES(reg_data[1], reg_data[0]);
      d.dig_h3 = reg_data[2];
      dig_h4_msb = (int16_t)(int8_t)reg_data[3] * 16;
      dig_h4_lsb = (int16_t)(reg_data[4] & 0x0F);
      d.dig_h4 = dig_h4_msb | dig_h4_lsb;
      dig_h5_msb = (int16_t)(int8_t)reg_data[5] * 16;
      dig_h5_lsb = (int16_t)(reg_data[4] >> 4);
      d.dig_h5 = dig_h5_msb | dig_h5_lsb;
      d.dig_h6 = (int8_t)reg_data[6];

    return true;
}


bool BME280::getCalibrationData(){
    
    bool success = false;
     I2C::i2c_block_t calib_data = { 0 };
    
    std::memset((void *) &_calib_data, 0, sizeof(_calib_data));
    
    success = _i2cPort.isAvailable()
        && _i2cPort.readBytes(BME280_REG_TEMP_PRESS_CALIB_DATA,BME280_LEN_TEMP_PRESS_CALIB_DATA, calib_data)
        && parse_temp_press_calib_data(calib_data, _calib_data)
        && _i2cPort.readBytes(BME280_REG_HUMIDITY_CALIB_DATA,BME280_LEN_HUMIDITY_CALIB_DATA, calib_data)
        && parse_humidity_calib_data(calib_data, _calib_data);
 
    return success;
}


/*
 power modes
 The BME280 offers three power modes: sleep mode, forced mode and normal mode. These can be
 selected using the mode[1:0] setting (see chapter 5.4.5). The available modes are:
 • Sleep mode: no operation, all registers accessible, lowest power, selected after startup
 • Forced mode: perform one measurement, store results and return to sleep mode
 • Normal mode: perpetual cycling of measurements and inactive periods.
 */
bool  BME280::setPowerMode(uint8_t powerMode){
    
    bool success = false;
 
    uint8_t data;
    
    /* Read the power mode register */
    
    if( _i2cPort.isAvailable()
            && _i2cPort.readByte(BME280_REG_PWR_CTRL, data))
    {
        // update the power mode in Bits 0:1
        data = BME280_SET_BITS(data, BME280_Device_MODE, powerMode);
        success = _i2cPort.writeByte(BME280_REG_PWR_CTRL, data);
    }
 
    return success;
}

bool  BME280::getPowerMode(uint8_t &powerMode){
    
    bool success = false;
    
    uint8_t data;
    
    /* Read the power mode register */
    
    if( _i2cPort.isAvailable()
       && _i2cPort.readByte(BME280_REG_PWR_CTRL, data))  {
        // Get the power mode in Bits 0:1
        powerMode = BME280_GET_BITS_POS_0(data, BME280_Device_MODE);
        success = true;
    }
    
    return success;
}


/*!
 * @brief This internal API is used to compensate the raw temperature data and
 * return the compensated temperature data in integer data type.
 */
static double compensate_temperature(const struct bme280_uncomp_data *uncomp_data, struct bme280_calib_data *calib_data)
{
    double var1;
    double var2;
    double temperature;
    double temperature_min = -40;
    double temperature_max = 85;

    var1 = (((double)uncomp_data->temperature) / 16384.0 - ((double)calib_data->dig_t1) / 1024.0);
    var1 = var1 * ((double)calib_data->dig_t2);
    var2 = (((double)uncomp_data->temperature) / 131072.0 - ((double)calib_data->dig_t1) / 8192.0);
    var2 = (var2 * var2) * ((double)calib_data->dig_t3);
    calib_data->t_fine = (int32_t)(var1 + var2);
    temperature = (var1 + var2) / 5120.0;

    if (temperature < temperature_min)
    {
        temperature = temperature_min;
    }
    else if (temperature > temperature_max)
    {
        temperature = temperature_max;
    }

    return temperature;
}

/* 32 bit compensation for pressure data */

/*!
 * @brief This internal API is used to compensate the raw pressure data and
 * return the compensated pressure data in integer data type.
 */
static double compensate_pressure(const struct bme280_uncomp_data *uncomp_data,
                                  const struct bme280_calib_data *calib_data)
{
    double var1;
    double var2;
    double var3;
    double pressure;
    double pressure_min = 30000.0;
    double pressure_max = 110000.0;

    var1 = ((double)calib_data->t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)calib_data->dig_p6) / 32768.0;
    var2 = var2 + var1 * ((double)calib_data->dig_p5) * 2.0;
    var2 = (var2 / 4.0) + (((double)calib_data->dig_p4) * 65536.0);
    var3 = ((double)calib_data->dig_p3) * var1 * var1 / 524288.0;
    var1 = (var3 + ((double)calib_data->dig_p2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)calib_data->dig_p1);

    /* Avoid exception caused by division by zero */
    if (var1 > (0.0))
    {
        pressure = 1048576.0 - (double) uncomp_data->pressure;
        pressure = (pressure - (var2 / 4096.0)) * 6250.0 / var1;
        var1 = ((double)calib_data->dig_p9) * pressure * pressure / 2147483648.0;
        var2 = pressure * ((double)calib_data->dig_p8) / 32768.0;
        pressure = pressure + (var1 + var2 + ((double)calib_data->dig_p7)) / 16.0;

        if (pressure < pressure_min)
        {
            pressure = pressure_min;
        }
        else if (pressure > pressure_max)
        {
            pressure = pressure_max;
        }
    }
    else /* Invalid case */
    {
        pressure = pressure_min;
    }

    return pressure;
}


/*!
 * @brief This internal API is used to compensate the raw humidity data and
 * return the compensated humidity data in integer data type.
 */
static double compensate_humidity(const struct bme280_uncomp_data *uncomp_data,
                                  const struct bme280_calib_data *calib_data)
{
    double humidity;
    double var1;
    double var2;
    double var3;
    double var4;
    double var5;
    double var6;

    var1 = ((double)calib_data->t_fine) - 76800.0;
    var2 = (((double)calib_data->dig_h4) * 64.0 + (((double)calib_data->dig_h5) / 16384.0) * var1);
    var3 = uncomp_data->humidity - var2;
    var4 = ((double)calib_data->dig_h2) / 65536.0;
    var5 = (1.0 + (((double)calib_data->dig_h3) / 67108864.0) * var1);
    var6 = 1.0 + (((double)calib_data->dig_h6) / 67108864.0) * var1 * var5;
    var6 = var3 * var4 * (var5 * var6);
    humidity = var6 * (1.0 - ((double)calib_data->dig_h1) * var6 / 524288.0);

    return  max(0.0, min(100.0, humidity));
}

 
bool BME280::processSensorData(uint8_t *reg_data, compensated_data& dataOut){
    
    bool success = false;
    
    /* Variables to store the sensor data */
    uint32_t data_xlsb;
    uint32_t data_lsb;
    uint32_t data_msb;
    
    /*! Un-compensated pressure , temperature ,  humidity */
    struct bme280_uncomp_data uncomp_data = { 0 };
    
    /* Store the parsed register values for pressure data */
    data_msb = (uint32_t)reg_data[0] << BME280_12_BIT_SHIFT;
    data_lsb = (uint32_t)reg_data[1] << BME280_4_BIT_SHIFT;
    data_xlsb = (uint32_t)reg_data[2] >> BME280_4_BIT_SHIFT;
    uncomp_data.pressure = data_msb | data_lsb | data_xlsb;
    double pressure = compensate_pressure(&uncomp_data, &_calib_data);
    
    /* Store the parsed register values for temperature data */
    data_msb = (uint32_t)reg_data[3] << BME280_12_BIT_SHIFT;
    data_lsb = (uint32_t)reg_data[4] << BME280_4_BIT_SHIFT;
    data_xlsb = (uint32_t)reg_data[5] >> BME280_4_BIT_SHIFT;
    uncomp_data.temperature = data_msb | data_lsb | data_xlsb;
    double temperature = compensate_temperature(&uncomp_data, &_calib_data);
    
    /* Store the parsed register values for humidity data */
    data_msb = (uint32_t)reg_data[6] << BME280_8_BIT_SHIFT;
    data_lsb = (uint32_t)reg_data[7];
    uncomp_data.humidity = data_msb | data_lsb;
    double humidity = compensate_humidity(&uncomp_data, &_calib_data);
    
    dataOut.humidity = humidity;
    dataOut.pressure = pressure;
    dataOut.temperature = temperature;
    
    success = true;
    
    return success;
}

/*
 Suggested settings for weather monitoring
 Sensor mode forced mode, 1 sample / minute
 Oversampling settings pressure ×1
 Forced
 
  settings.filter = BME280_FILTER_COEFF_2;

  settings.osr_h = BME280_OVERSAMPLING_1X;
 settings.osr_p = BME280_OVERSAMPLING_1X;
 settings.osr_t = BME280_OVERSAMPLING_1X;

  settings.standby_time = BME280_STANDBY_TIME_0_5_MS;
*/


bool BME280::configureForWeather(){
    bool success = false;
    
    I2C::i2c_block_t regs;
    
    if(  _i2cPort.isAvailable() && _i2cPort.readBytes(BME280_REG_CTRL_HUM, 4, regs))
    {
        bme280_settings  settings {0};
  
        /* Configuring the over-sampling rate, filter coefficient and standby time */
        /* Overwrite the desired settings */
        
        /* Over-sampling rate for humidity, temperature and pressure */
        settings.osr_h = BME280_OVERSAMPLING_1X;
        settings.osr_p = BME280_OVERSAMPLING_1X;
        settings.osr_t = BME280_OVERSAMPLING_1X;
 
        /* Setting the standby time */
         settings.standby_time = BME280_STANDBY_TIME_0_5_MS;
         settings.filter = BME280_FILTER_COEFF_2;

        uint8_t mode  = 0;
        getPowerMode(mode);
 //       printf("mode was %d\n", mode);
        if(mode != BME280_POWERMODE_SLEEP)
            setPowerMode(BME280_POWERMODE_SLEEP);
        
        uint8_t ctrl_hum;
        ctrl_hum = settings.osr_h & BME280_CTRL_HUM_MSK;
        /* Write the humidity control value in the register */
        _i2cPort.writeByte(BME280_REG_CTRL_HUM, ctrl_hum);
        
        uint8_t ctrl_meas = 0;
        _i2cPort.readByte(BME280_REG_CTRL_MEAS, ctrl_meas);
        ctrl_meas = BME280_SET_BITS(ctrl_meas, BME280_CTRL_PRESS, settings.osr_p);
        ctrl_meas = BME280_SET_BITS(ctrl_meas, BME280_CTRL_TEMP, settings.osr_t);
        _i2cPort.writeByte(BME280_REG_CTRL_MEAS, ctrl_meas);
        /* Humidity related changes will be only effective after a
         * write operation to ctrl_meas register
         */
     
        uint8_t cnf_reg;
        _i2cPort.readByte(BME280_REG_CONFIG, cnf_reg);
        cnf_reg = BME280_SET_BITS(cnf_reg, BME280_FILTER, settings.filter);
        cnf_reg = BME280_SET_BITS(cnf_reg, BME280_STANDBY, settings.standby_time);
        _i2cPort.writeByte(BME280_REG_CONFIG, cnf_reg);
 
        success = true;
    }
    
    return success;
}

bool  BME280::readSensor(compensated_data &dataOut){
    
    bool success = false;
    uint8_t try_run = 10;
    
    // set the BME280 into forced mode and wait till it is no longer in Forced/
    if( _isSetup && setPowerMode(BME280_POWERMODE_FORCED) ) {
        
        uint8_t newMode;
        do{
            usleep(5000);    // 11.5 ms?
            
            if(!getPowerMode(newMode))
                return false;
            
            if(try_run-- == 0)
                return false;
            
        } while (newMode == BME280_POWERMODE_FORCED);
        
        I2C::i2c_block_t reg_data = { 0 };
        
        // GET REGISTERS
        /*
         Data readout is done by starting a burst read from 0xF7 to 0xFE (temperature, pressure and humidity).
         The data are read out in an unsigned 20-bit format both for pressure and for temperature
         and in an unsigned 16-bit format for humidity.
         
         In spite of what Bosch claims, the BME280 doesnt handle I2c Burst reads,
         so read it a byte at a time.
         */
        
        success = _i2cPort.readBytes(BME280_REG_DATA,BME280_LEN_P_T_H_DATA, reg_data)
                    && processSensorData(reg_data, dataOut);
    }
    
    return success;
}


