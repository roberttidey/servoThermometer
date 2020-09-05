/*
 R. J. Tidey 2018/06/27
 Servo based thermometer display
 Designed to run on battery with deep sleep but can be overridden for maintenance
 WifiManager can be used to config wifi network
 
 Temperature reporting Code based on work by Igor Jarc
 Some code based on https://github.com/DennisSc/easyIoT-ESPduino/blob/master/sketches/ds18b20.ino
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#define ESP8266
#include "BaseConfig.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Servo.h>

#define ONE_WIRE_BUS 13  // DS18B20 pin
#define SERVO_PIN 5
#define SERVO_ENABLE 4
#define PUSH_BUTTON1 12

Servo myservo;  // create servo object to control a servo
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define BUTTON_INTTIME_MIN 250
int tempValid;
int timeInterval = 50;
unsigned long elapsedTime;
unsigned long startUpTime;
int servoMaxTemp = 38;
int servoMinTemp = 8;
int tempUnits = 0;
int servoInitialised = 0;
unsigned long updateCount = 0;

int8_t timeZone = 0;
int8_t minutesTimeZone = 0;
time_t currentTime;

#define AP_AUTHID "14153143"

#define COUNT_FILE "/servoTempCount.txt"
#define LOG_FILE "/servoTempLog.txt"
//event types >= 100 are always logged
#define EVENT_TEMP 1
#define EVENT_BATTERY 100
#define EVENT_WIFI 101
//OFF= on all time with wifi. LIGHT=onll time no wifi, DEEP=one shot + wake up
#define SLEEP_MODE_OFF 0
#define SLEEP_MODE_DEEP 1
#define SLEEP_MODE_LIGHT 2

//general variables
int logging = 0;
int sleepInterval = 300; //seconds
int sleepMode = SLEEP_MODE_OFF;
float newTemp;

float ADC_CAL =0.96;
float battery_mult = 1220.0/220.0/1024;//resistor divider, vref, max count
float battery_volts;

/*
  Log event
*/
void logEvent(int eventType, String event) {
	if(logging || eventType >= EVENT_BATTERY) {
		File f = FILESYS.open(LOG_FILE, "a");
		f.print(String(updateCount) + "," + String(eventType) + "," + event + "\n");
		f.close();
	}
}

/*
  Load config
*/
void loadConfig() {
	String line = "";
	int config = 0;
	File f = FILESYS.open(CONFIG_FILE, "r");
	if(f) {
		while(f.available()) {
			line =f.readStringUntil('\n');
			line.replace("\r","");
			if(line.length() > 0 && line.charAt(0) != '#') {
				switch(config) {
					case 0: host = line;break;
					case 1: servoMinTemp = line.toInt();break;
					case 2: servoMaxTemp = line.toInt();break;
					case 3: sleepInterval = line.toInt();break;
					case 4: sleepMode = line.toInt();break;
					case 5: logging = line.toInt();break;
					case 6: tempUnits =line.toInt();break;
					case 7:
						ADC_CAL =line.toFloat();
						Serial.println(F("Config loaded from file OK"));
						break;
				}
				config++;
			}
		}
		f.close();
		if(sleepInterval < 15) sleepInterval = 15;
		Serial.println("Config loaded");
		if(digitalRead(PUSH_BUTTON1) == 0) {
			sleepMode = SLEEP_MODE_OFF;
			Serial.println(F("Sleep override active"));
		}
		if(sleepMode != SLEEP_MODE_OFF) setupWifi = 0;
		Serial.print(F("host:"));Serial.println(host);
		Serial.print(F("servoMinTemp:"));Serial.println(servoMinTemp);
		Serial.print(F("servoMaxTemp:"));Serial.println(servoMaxTemp);
		Serial.print(F("sleepInterval:"));Serial.println(sleepInterval);
		Serial.print(F("sleepMode:"));Serial.println(sleepMode);
		Serial.print(F("logging:"));Serial.println(logging);
		Serial.print(F("tempUnits:"));Serial.println(tempUnits);
		Serial.print(F("ADC_CAL:"));Serial.println(ADC_CAL);
	} else {
		sleepMode = SLEEP_MODE_OFF;
		Serial.println(String(CONFIG_FILE) + " not found");
	}
}

/*
  test servo
*/
void testServo() {
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(401, "text/html", "Unauthorized");
	} else {
		newTemp = server.arg("temp").toFloat();
		servoDisplay(newTemp);
		server.send(200, "text/html", "Servo set to " + String(newTemp));
	}
}

/*
 Update servo to display analog Temp
*/
void servoDisplay(float value) {
	int servoPos;
	Serial.println("ServoDisplay " + String(value));
	if(value < servoMinTemp) value = servoMinTemp;
	if(value > servoMaxTemp) value = servoMaxTemp;
	if(servoMaxTemp > servoMinTemp) {
		servoPos = 180 - 180 * (value - servoMinTemp) / (servoMaxTemp - servoMinTemp);
		myservo.write(servoPos);
		if(!servoInitialised) {
			servoInitialised = 1;
			myservo.attach(SERVO_PIN);
		}
		delaymSec(100);
		digitalWrite(SERVO_ENABLE, 1);
		//Allow time for servo to position before turning off
		delaymSec(600);
		// don't turn off ifon all time
		if(sleepMode == SLEEP_MODE_DEEP) digitalWrite(SERVO_ENABLE, 0);
	}
}


/*
 Check temperature and report if necessary
*/
void checkTemp() {
	DS18B20.requestTemperatures(); 
	newTemp = DS18B20.getTempCByIndex(0);
	if(newTemp != 85.0 && newTemp != (-127.0)) {
		//convert to Fahrenheit if required
		if(tempUnits == 1) newTemp = newTemp * 1.8 + 32;
		Serial.print("New temperature:");
		Serial.println(String(newTemp).c_str());
		servoDisplay(newTemp);
		logEvent(EVENT_TEMP, String(newTemp));
	} else {
		Serial.println("Invalid temp reading");
	}
}

void setupStart() {
	startUpTime = millis();
	pinMode(PUSH_BUTTON1, INPUT_PULLUP);
	pinMode(SERVO_ENABLE, OUTPUT);
	digitalWrite(SERVO_ENABLE, 0);
	updateCounter(0);
}

void extraHandlers() {
	server.on("/test", testServo);
}

/*
  updateCounter
*/
void updateCounter(int inc) {
	File f = FILESYS.open(COUNT_FILE, "r");
	if(f) {
		updateCount = strtoul(f.readStringUntil('\n').c_str(), NULL, 10) + inc;
		f.close();
	}
	f = FILESYS.open(COUNT_FILE, "w");
	f.print(String(updateCount) + "\n");
	f.close();
}


/*
  Main loop to read temperature and publish as required
*/
void loop() {
	int i;
	updateCounter(1);
	if((updateCount % 10) == 0) {
		battery_volts = battery_mult * ADC_CAL * analogRead(A0);
		logEvent(EVENT_BATTERY, String(battery_volts));
	}
	checkTemp();
	if(sleepMode == SLEEP_MODE_DEEP) {
		Serial.println("Active Msec before sleep " + String(millis()-startUpTime));
		digitalWrite(SERVO_ENABLE, 0);
		ESP.deepSleep(1e6*sleepInterval);
	}
	for(i = 0;i < sleepInterval*1000/timeInterval; i++) {
		server.handleClient();
		wifiConnect(1);
		delaymSec(timeInterval);
		elapsedTime++;
	}
}
