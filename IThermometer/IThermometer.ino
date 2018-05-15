// iThermometer by emilio  
// D3 : DS18B20 Sensor
// Optional WEMOS OLED

#include <ESP8266WiFi.h>                             //https://github.com/esp8266/Arduino
#include <EEPROM.h>                                  
#include <WiFiManager.h>                             //https://github.com/kentaylor/WiFiManager
#include <ESP8266WebServer.h>                        //http://www.wemos.cc/tutorial/get_started_in_arduino.html
#include <DoubleResetDetector.h>                     //https://github.com/datacute/DoubleResetDetector
#include <OneWire.h>                                 //http://www.pjrc.com/teensy/td_libs_OneWire.html
#include <Adafruit_GFX.h>                            //https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>                        //https://github.com/mcauser/Adafruit_SSD1306

#define Version "2.1.3"

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
unsigned int localPort = 5010;                        // Port auf dem gelesen wird
ESP8266WebServer server(80);                          // Webserver initialisieren auf Port 80
WiFiUDP Udp;

OneWire ds(D3);                                       // OneWire an pin D3

const int PIN_LED = 2;                                // Controls the onboard LED.
bool initialConfig = false;                           // Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
char charVal[8];
unsigned long jetztMillis = 0, letzteUDPMillis = 0, letzteMeldungMillis = 0;
float Temp = 0.0;

void Hauptseite()
{
  char dummy[8];
  String Antwort = "";
  Antwort += "<meta http-equiv='refresh' content='5'/>";
  Antwort += "<font face=";
  Antwort += char(34);
  Antwort += "Courier New";
  Antwort += char(34);
  Antwort += ">";
   
  Antwort += "<b>Aktuelle Temperatur: </b>\n</br>";
  
  dtostrf(Temp, 5, 1, dummy);  
  Antwort += dummy;
  Antwort += " ";
  Antwort += char(176);
  Antwort += "C\n</br>";

  Antwort +="</br>Verbunden mit: ";
  Antwort +=WiFi.SSID(); 
  Antwort +="</br>Signalstaerke: ";
  Antwort +=WiFi.RSSI(); 
  Antwort +="dBm  </br>";
  Antwort +="</br>IP-Adresse: ";
  IPAddress IP = WiFi.localIP();
  Antwort += IP[0];
  Antwort += ".";
  Antwort += IP[1];
  Antwort += ".";
  Antwort += IP[2];
  Antwort += ".";
  Antwort += IP[3];
  Antwort +="</br>";
  Antwort +="</br>UDP-OUT port: ";
  Antwort +=answerPort; 
  Antwort +="</br></br>";
  Antwort += "</font>";
  server.send ( 300, "text/html", Antwort );
}

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
  if (Temp<10) {Udp.write(' ');}
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

float DS18B20lesen()
{
  int TReading, SignBit;
  byte i, present = 0, data[12], addr[8];
  if ( !ds.search(addr))  { ds.search(addr); }        // Wenn keine weitere Adresse vorhanden, von vorne anfangen
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);                                  // start Konvertierung, mit power-on am Ende
  delay(750);                                         // 750ms sollten ausreichen
  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);                                     // Wert lesen
  for ( i = 0; i < 9; i++) { data[i] = ds.read(); }
  TReading = (data[1] << 8) + data[0];
  SignBit = TReading & 0x8000;                        // test most sig bit
  if (SignBit) {TReading = (TReading ^ 0xffff) + 1;}  // 2's comp
  Temp = TReading * 0.0625;                           // Für DS18S20  temperatur = TReading*0.5
  if (SignBit) {Temp = Temp * -1;}
  return Temp;
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

  WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
  WiFi.setOutputPower(20.5);
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
    wifiManager.setBreakAfterConfig(true);
    wifiManager.addParameter(&p_answerPort);
    wifiManager.setConfigPortalTimeout(300);

    if (!wifiManager.startConfigPortal()) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yeey :)");
    }
    
    answerPort = atoi(p_answerPort.getValue());

    WriteSettings();

    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);

  }
   
  digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
  
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
   if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    UDPip=WiFi.localIP();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("IP-Adresse");    
    display.println("----------");
    display.print(UDPip[0]);
    display.print(".");
    display.print(UDPip[1]);
    display.println(".");
    display.print(UDPip[2]);
    display.print(".");
    display.print(UDPip[3]);
    display.display();
    delay(8000);  
    server.on("/", Hauptseite);
    server.begin();                          // HTTP-Server starten
  }

}

void loop() {

  drd.loop();
  
  jetztMillis = millis();

  server.handleClient(); // auf HTTP-Anfragen warten
  
  if (WiFi.status()!=WL_CONNECTED){
    WiFi.reconnect();
    Serial.println("lost connection");
    delay(5000);
  } else{
    if(!deltaMeldungMillis == 0 && jetztMillis - letzteMeldungMillis > deltaMeldungMillis)
    {
      digitalWrite(PIN_LED, LOW);
      Temp = DS18B20lesen();
      UDPOut();
      USBOut();  
      DisplayOut();
      Hauptseite();
      letzteMeldungMillis = jetztMillis;
      digitalWrite(PIN_LED, HIGH);
    }
  }  
}


