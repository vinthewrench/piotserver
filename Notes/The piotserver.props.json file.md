# The piotserver.props.json file

File is found in assets directory

- JSON

I have been using an online JSON validator to help with debugging

https://jsonformatter.org/

*Config
 ```json
  "config": {
       "access_keys": [
          {
              "api_key": "your API_KEY_ID",
              "api_secret": "your API_KEY_SECRET"
              }
      ],
   "description": "The Raspbery Pi Irrigation System",
    "lat_long": "34.74741777, -92.26354096",
    "name": "Garden Irrigation"
  }
```

*Devices

 ```json
   "devices": [
    {
      "address": "0x48",
      "data_type": "TEMPERATURE",
      "device_type": "TMP10X",
      "interval": 30,
      "key": "TEMP 48",
      "title": "Temperature 48",
      "tracking": "track.range"
    },
      {
      "address": "/mnt/1wire/28.793434000000/",
      "data_type": "TEMPERATURE",
      "description": "Plot A Soil Temperature",
      "device_type": "1wire",
      "interval": 3600,
      "key": "PLOT1",
      "title": "Soil Temp Plot A",
      "tracking": "track.changes"
    },
     {
      "address": "0x44",
      "description": "Outside Environment Sensor",
      "device_type": "SHT30",
      "pins": [
        {
          "data_type": "TEMPERATURE",
          "interval": 30,
          "key": "GARDEN_TEMPERATURE",
          "title": "Garden Temperature",
          "tracking": "track.range"
        },
        {
          "data_type": "HUMIDITY",
          "key": "GARDEN_HUMIDITY",
          "title": "Garden Humidity",
          "tracking": "track.range"
        },
        {
          "data_type": "SERIAL_NO",
          "key": "GARDEN_SERIAL_NO",
          "title": "Garden Sensor Serial No",
          "tracking": "dont.record"
        }
      ],
      "title": "Garden"
    },
    
       {
      "device_type": "MCP23008",
      "address": "0x20",
      "title": "NCD Relay+I/O",
      "interval": 5,
      "pins": [
        {
          "bit": 0,
          "data_type": "BOOL",
          "key": "BIG_RELAY",
          "title": "Big Relay",
          "gpio.mode": "output"
        },
        {
          "bit": 1,
          "data_type": "BOOL",
          "key": "BIG_RELAY2",
          "title": "Big Relay2",
          "gpio.mode": "output"
        },
        {
          "bit": 7,
          "data_type": "BOOL",
          "key": "RAIN_SENSOR",
          "title": "Orbit Rain Sensor",
          "gpio.mode": "input"
        }
      ]
    },
    
     {
      "device_type": "GPIO",
      "pins": [
        {
          "BCM": 5,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_1",
          "title": "relay 1"
        },
        {
          "BCM": 6,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_2",
          "title": "relay 2"
        },
        {
          "BCM": 13,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_3",
          "title": "relay 3"
        },
        {
          "BCM": 16,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_4",
          "title": "relay 4"
        },
        {
          "BCM": 19,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_5",
          "title": "relay 5"
        },
        {
          "BCM": 20,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_6",
          "title": "relay 6"
        },
        {
          "BCM": 21,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_7",
          "title": "relay 7"
        },
        {
          "BCM": 26,
          "data_type": "BOOL",
          "gpio.mode": "output",
          "key": "RELAY_8",
          "title": "relay 8"
        },
        {
          "BCM": 22,
          "data_type": "BOOL",
          "gpio.flags": "0x20",
          "gpio.mode": "input",
          "key": "RAIN_SENSOR",
          "title": "rain sensor"
        }
      ],
      "title": "RPi Relay Board"
    }
    ]
    
    ```
