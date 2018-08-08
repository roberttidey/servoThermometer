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

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include "FS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Servo.h>

//put -1 s at end
int unusedPins[11] = {0,2,14,15,16,-1,-1,-1,-1,-1,-1};

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "servoTherm"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
char wmName[33];

//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

#define ONE_WIRE_BUS 13  // DS18B20 pin
#define SERVO_PIN 5
#define SERVO_ENABLE 4
#define PUSH_BUTTON1 12

Servo myservo;  // create servo object to control a servo
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define WIFI_CHECK_TIMEOUT 30000
#define BUTTON_INTTIME_MIN 250
int tempValid;
int timeInterval = 50;
unsigned long elapsedTime;
unsigned long wifiCheckTime;
unsigned long startUpTime;
int servoMaxTemp = 38;
int servoMinTemp = 8;
int tempUnits = 0;
int servoInitialised = 0;
unsigned long updateCount = 0;

//holds the current upload
File fsUploadFile;

int8_t timeZone = 0;
int8_t minutesTimeZone = 0;
time_t currentTime;

//For update service
String host = "esp8266-water";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password";

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_AUTHID "12345678"
#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;

#define CONFIG_FILE "/servoTempConfig.txt"
#define COUNT_FILE "/servoTempCount.txt"
#define LOG_FILE "/servoTempLog.txt"
//event types >= 100 are always logged
#define EVENT_TEMP 1
#define EVENT_BATTERY 100
//OFF= on all time with wifi. LIGHT=onll time no wifi, DEEP=one shot + wake up
#define SLEEP_MODE_OFF 0
#define SLEEP_MODE_DEEP 1
#define SLEEP_MODE_LIGHT 2

//general variables
int logging = 0;
int sleepInterval = 300; //seconds
int sleepMode = SLEEP_MODE_DEEP;
float newTemp;

float ADC_CAL =0.96;
float battery_mult = 1220.0/220.0/1024;//resistor divider, vref, max count
float battery_volts;
void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

void unusedIO() {
	int i;
	
	for(i=0;i<11;i++) {
		if(unusedPins[i] < 0) {
			break;
		} else if(unusedPins[i] != 16) {
			pinMode(unusedPins[i],INPUT_PULLUP);
		} else {
			pinMode(16,INPUT_PULLDOWN_16);
		}
	}
}

/*
  Log event
*/
void logEvent(int eventType, String event) {
	if(logging || eventType >= EVENT_BATTERY) {
		File f = SPIFFS.open(LOG_FILE, "a");
		f.print(String(updateCount) + "," + String(eventType) + "," + event + "\n");
		f.close();
	}
}

/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
			if(WiFi.status() != WL_CONNECTED) {
				Serial.println(F("Wifi connection timed out. Try to relink"));
			} else {
				wifiCheckTime = elapsedTime;
				return 1;
			}
		} else {
			return 0;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println(F("Set up managed Web"));
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	wifiManager.setConfigPortalTimeout(180);
	//Revert to STA if wifimanager times out as otherwise APA is left on.
	strcpy(wmName, WM_NAME);
	strcat(wmName, macAddr.c_str());
	wifiManager.autoConnect(wmName, WM_PASSWORD);
	WiFi.mode(WIFI_STA);
#else
	Serial.println(F("Set up manual Web"));
	int retries = 0;
	Serial.print(F("Connecting to AP"));
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println(F("WiFi connection attempt failed")); 
		return 0;
	} 
#endif
	//wifi_set_sleep_type(LIGHT_SLEEP_T);
}


void initFS() {
	if(!SPIFFS.begin()) {
		Serial.println(F("No SIFFS found. Format it"));
		if(SPIFFS.format()) {
			SPIFFS.begin();
		} else {
			Serial.println(F("No SIFFS found. Format it"));
		}
	} else {
		Serial.println(F("SPIFFS file list"));
		Dir dir = SPIFFS.openDir("/");
		while (dir.next()) {
			Serial.print(dir.fileName());
			Serial.print(F(" - "));
			Serial.println(dir.fileSize());
		}
	}
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.printf_P(PSTR("handleFileRead: %s\r\n"), path.c_str());
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.printf_P(PSTR("handleFileUpload Name: %s\r\n"), filename.c_str());
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    Serial.printf_P(PSTR("handleFileUpload Data: %d\r\n"), upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.printf_P(PSTR("handleFileUpload Size: %d\r\n"), upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileDelete: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileCreate: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.printf_P(PSTR("handleFileList: %s\r\n"),path.c_str());
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  server.send(200, "text/json", output);
}

void handleMinimalUpload() {
  char temp[700];

  snprintf ( temp, 700,
    "<!DOCTYPE html>\
    <html>\
      <head>\
        <title>ESP8266 Upload</title>\
        <meta charset=\"utf-8\">\
        <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
      </head>\
      <body>\
        <form action=\"/edit\" method=\"post\" enctype=\"multipart/form-data\">\
          <input type=\"file\" name=\"data\">\
          <input type=\"text\" name=\"path\" value=\"/\">\
          <button>Upload</button>\
         </form>\
      </body>\
    </html>"
  );
  server.send ( 200, "text/html", temp );
}

void handleSpiffsFormat() {
	SPIFFS.format();
	server.send(200, "text/json", "format complete");
}

/*
  Get config
*/
void getConfig() {
	String line = "";
	int config = 0;
	File f = SPIFFS.open(CONFIG_FILE, "r");
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
		Serial.print(F("host:"));Serial.println(host);
		Serial.print(F("servoMinTemp:"));Serial.println(servoMinTemp);
		Serial.print(F("servoMaxTemp:"));Serial.println(servoMaxTemp);
		Serial.print(F("sleepInterval:"));Serial.println(sleepInterval);
		Serial.print(F("sleepMode:"));Serial.println(sleepMode);
		Serial.print(F("logging:"));Serial.println(logging);
		Serial.print(F("tempUnits:"));Serial.println(tempUnits);
		Serial.print(F("ADC_CAL:"));Serial.println(ADC_CAL);
	} else {
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
		digitalWrite(SERVO_ENABLE, 1);
		delaymSec(100);
		servoPos = 180 - 180 * (value - servoMinTemp) / (servoMaxTemp - servoMinTemp);
		myservo.write(servoPos);
		if(!servoInitialised) {
			servoInitialised = 1;
			myservo.attach(SERVO_PIN);
		}
		//Allow time for servo to position before turning off
		delaymSec(600);
		digitalWrite(SERVO_ENABLE, 0);
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

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	startUpTime = millis();
	unusedIO();
	pinMode(PUSH_BUTTON1, INPUT_PULLUP);
	pinMode(SERVO_ENABLE, OUTPUT);
	digitalWrite(SERVO_ENABLE, 0);
	Serial.begin(115200);
	Serial.println(F("Set up filing system"));
	initFS();
	getConfig();
	if(digitalRead(PUSH_BUTTON1) == 0) {
		sleepMode = SLEEP_MODE_OFF;
		Serial.println(F("Sleep override active"));
	}
	if(sleepMode == SLEEP_MODE_OFF) {
		Serial.println(F("Set up Wifi services"));
		macAddr = WiFi.macAddress();
		macAddr.replace(":","");
		Serial.println(macAddr);
		wifiConnect(0);
		//Update service
		MDNS.begin(host.c_str());
		httpUpdater.setup(&server, update_path, update_username, update_password);
		Serial.println(F("Set up web server"));
		//Simple upload
		server.on("/test", testServo);
		server.on("/upload", handleMinimalUpload);
		server.on("/format", handleSpiffsFormat);
		server.on("/list", HTTP_GET, handleFileList);
		//load editor
		server.on("/edit", HTTP_GET, [](){
		if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");});
		//create file
		server.on("/edit", HTTP_PUT, handleFileCreate);
		//delete file
		server.on("/edit", HTTP_DELETE, handleFileDelete);
		//first callback is called after the request has ended with all parsed arguments
		//second callback handles file uploads at that location
		server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);
		//called when the url is not defined here
		//use it to load content from SPIFFS
		server.onNotFound([](){if(!handleFileRead(server.uri())) server.send(404, "text/plain", "FileNotFound");});
		server.begin();
		MDNS.addService("http", "tcp", 80);
	}
	Serial.println(F("Set up complete"));
}

/*
  updateCounter
*/
void updateCounter() {
	File f = SPIFFS.open(COUNT_FILE, "r");
	if(f) {
		updateCount = strtoul(f.readStringUntil('\n').c_str(), NULL, 10) + 1;
		f.close();
	}
	f = SPIFFS.open(COUNT_FILE, "w");
	f.print(String(updateCount) + "\n");
	f.close();
}


/*
  Main loop to read temperature and publish as required
*/
void loop() {
	int i;
	updateCounter();
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
