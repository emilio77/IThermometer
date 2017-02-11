// iThermometer by emilio  
// D3 : DS18B20 Sensor
// Optional WEMOS OLED

#include <ESP8266WiFi.h>                             //https://github.com/esp8266/Arduino
#include <EEPROM.h>                                  
#include <WiFiManager.h>                             //https://github.com/kentaylor/WiFiManager
#include <DoubleResetDetector.h>                     //https://github.com/datacute/DoubleResetDetector
#include <OneWire.h>                                 //http://www.pjrc.com/teensy/td_libs_OneWire.html
#include "DallasTemperature.h"
#include <Adafruit_GFX.h>                            //https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>                        //https://github.com/adafruit/Adafruit_SSD1306

#define Version "2.1.0"

#define deltaMeldungMillis 5000                      // Sendeintervall an die Brauerei in Millisekunden
#define DRD_TIMEOUT 10                               // Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0                                // RTC Memory Address for the DoubleResetDetector to use
#define RESOLUTION 12                                // OneWire - 12bit resolution == 750ms update rate
#define OWinterval (800 / (1 << (12 - RESOLUTION))) 
#define OLED_RESET 0


Adafruit_SSD1306 display(OLED_RESET);

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

IPAddress UDPip(192,168,178,255);                     // IP-Adresse an welche UDP-Nachrichten geschickt werden xx.xx.xx.255 = Alle Netzwerkteilnehmer die am Port horchen.
unsigned int answerPort = 5003;                       // Port auf den Temperaturen geschickt werden
WiFiUDP Udp;

OneWire ds(D3);                                       // OneWire an pin D3
DallasTemperature DS18B20(&ds);
DeviceAddress tempDeviceAddress;

const int PIN_LED = 2;                                // Controls the onboard LED.
bool initialConfig = false;                           // Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
float Temp = 0.0;
char charVal[8];
unsigned long jetztMillis = 0, letzteUDPMillis = 0, letzteMeldungMillis = 0;
unsigned long DSreqTime;
bool DSrequested = false;

void DisplayOut() {
  dtostrf(Temp, 3, 1, charVal);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Temp:");
  display.println(charVal);
  display.write(247);
  display.print("C");
  display.display();
}

void UDPOut() {
  dtostrf(Temp, 3, 1, charVal);
  Udp.beginPacket(UDPip, answerPort);
  Udp.write('T');
  Udp.write(charVal);
  Udp.write('t');
  Udp.println();
  Udp.endPacket();
}

void USBOut()
{
  dtostrf(Temp, 4, 1, charVal);
  delay(100);
  for (int i = 0; i < sizeof(charVal)-1; i++) { Serial.write(charVal[i]); }
  Serial.write(35);
  Serial.println();
}

void initDS18B20() {
  DS18B20.begin();                                               // Start up the DS18B20
  DS18B20.setWaitForConversion(false);
  DS18B20.getAddress(tempDeviceAddress, 0);
  DS18B20.setResolution(tempDeviceAddress, RESOLUTION);
  requestTemp();
}

float getTemperature(bool block = false) {
  float t = Temp;                                                 // we need to wait for DS18b20 to finish conversion
  while (block && (millis() - DSreqTime <= OWinterval))           // if we need the result we have to block
    yield();
  if (millis() - DSreqTime > OWinterval) {
    t = DS18B20.getTempCByIndex(0);
    DSrequested = false;
    if (t == DEVICE_DISCONNECTED_C ||                             // DISCONNECTED
        t == 85.0)                                                // we read 85 uninitialized
    {
      Serial.println(F("ERROR: OW DISCONNECTED"));
    }
  }
  return t;
}

void requestTemp() {
  if (DSrequested == false) {
    DS18B20.requestTemperatures();
    DSreqTime = millis();
    DSrequested = true;
  }
}

void ReadSettings() {
  EEPROM.begin(512);
  int addr=0;
  addr += EEPROM.get(addr, answerPort);
  EEPROM.commit();
  EEPROM.end();
}  

void WriteSettings() {
  EEPROM.begin(512);
  int addr=0;
  addr += EEPROM.put(addr, answerPort);
  EEPROM.end();    
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(19200);
  Serial.println("\n");
  Serial.println("Starting");
  Serial.print("iThermometer V");  
  Serial.println(Version);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  display.display();

  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  ReadSettings();

  WiFi.printDiag(Serial);            //Remove this line if you do not want to see WiFi password printed

  if (WiFi.SSID()==""){
    Serial.println("We haven't got any access point credentials, so get them now");   
    initialConfig = true;
  }
 
  if (drd.detectDoubleReset()) {
    Serial.println("Double Reset Detected");
    initialConfig = true;
  }
  
  if (initialConfig) {
    Serial.println("Starting configuration portal.");
    digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
    char convertedValue[5];
    
    sprintf(convertedValue, "%d", answerPort);
    WiFiManagerParameter p_answerPort("answerPort", "send temperature on UDP Port", convertedValue, 5);

    WiFiManager wifiManager;
    wifiManager.addParameter(&p_answerPort);
    wifiManager.setConfigPortalTimeout(300);

    wifiManager.setBreakAfterConfig(true);

    if (!wifiManager.startConfigPortal()) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yeey :)");
    }
    
    answerPort = atoi(p_answerPort.getValue());

    WriteSettings();
 
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.

  }
   
  digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
  WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
  unsigned long startedAt = millis();
  Serial.print("After waiting ");
  int connRes = WiFi.waitForConnectResult();
  float waited = (millis()- startedAt);
  Serial.print(waited/1000);
  Serial.print(" secs in setup() connection result is ");
  Serial.println(connRes);
  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
    UDPip=WiFi.localIP();
    UDPip[3]=255;
    Serial.print("UDP-Port: ");
    Serial.println(answerPort); 
  }
}

void loop() {

  drd.loop();
  
  jetztMillis = millis();
  
  if (WiFi.status()!=WL_CONNECTED){
    WiFi.reconnect();
    Serial.println("lost connection");
  } else{
    if(!deltaMeldungMillis == 0 && jetztMillis - letzteMeldungMillis > deltaMeldungMillis)
    {
      digitalWrite(PIN_LED, LOW);
      initDS18B20();
      Temp = getTemperature(true);
      UDPOut();
      USBOut();  
      DisplayOut();
      letzteMeldungMillis = jetztMillis;
      digitalWrite(PIN_LED, HIGH);
    }
  }  
}


