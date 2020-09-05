# servoThermometer
Analog temperature display using servo and esp8266

Construction details at https://www.instructables.com/id/ServoThermometer/

It makes use of the BaseSupport library at

https://github.com/roberttidey/BaseSupport

Edit the WifiManager and update passwords in BaseConfig.h

## Features
- Self contained unit holding electronics, servo and battery
- High accuracy using ds18b20 digital sensor
- Rechargeable LIPO with inbuilt charger
- Very low quiescent current (< 20uA) for long battery life
- Servo only turned on for short periods again giving good battery life.
- Normally the module sleeps between temperature updates but can be turned into a non sleep mode for checking and configuration
- Configuration data and servo test from web interface​
- Minimum, maximum and update interval configurable
- Software can be updated via web interface
- Low cost





