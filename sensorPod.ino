/**************************************

Sensor Pod -- Written by Benjamin Budai in 2019

This sketch connects the arduino to a raspberry pi
(set up as an access point with IP Address 192.168.42.1)
and sends it the readings for temperature and turbidity
every 1 second.

If the arduino can't connect to the pi, it writes the
temperature and turbidity to a text file on the SD card
(if one is connected to the wifi shield)

You will need:
- WINC1500 wifi shield
- Micro SD card (optional)
- SEN0189 Turbidity Sensor (from DFRobot)
- Waterproof DS18B20 Digital temperature sensor (also DFRobot)
- a means by which to power the arduino and sensors
- a grove connection shield could be helpful for keeping connections clean

Connections:
1. Attach the wifi shield
2. Attach the power and ground wires from all sensors to the arduino
3. Attach the data wire from the temperature sensor to pin 2 (you can change this -- just change DS18S20_Pin appropriately)
4. Attach the data wire from the turbidity sensor to Analogue pin 12 (You can change this as well, just change turbidityPin)

Other setup:
You will need to configure the sketch to work with your raspberry pi, so make sure
to make the following updates:
- Change SECRET_PASS and SECRET_SSID in "arduino_secrets.h" to be the password and SSID
   for accessing the raspberry pi (set up as an access point)
- Make sure that "localPort" is the port you want to use (it has to be the same as the port
   that the raspberry pi will be listening on)
- Change "piAddress" to reflect the IP address of your raspberry pi.

***************************************/


#include <SPI.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <OneWire.h>
#include <SD.h>
#include <Thread.h>
#include <ThreadController.h>
#include "arduino_secrets.h" // Holds the connection info needed to connect to the pi

int status = WL_IDLE_STATUS;

char ssid[] = SECRET_SSID;        // SSID for the pi - gotten from "arduino_secrets.h"
char pass[] = SECRET_PASS;    // Password for the pi - gotten from "arduino_secrets.h"

float round_to_dp( float in_value, int decimal_place );

double getTurbidity();
int turbidityPin = A12;

void printWiFiStatus();
void connectToPi();
unsigned int localPort = 2390;      // Port to listen on and send on
char packetBuffer[255]; //buffer to hold incoming packet
void sendDataToPi();
void checkForUdpData();

//Temperature chip i/o
int DS18S20_Pin = 2;
OneWire ds(DS18S20_Pin);
double getTemp();

Thread writeData = Thread();
ThreadController tController = ThreadController();

void writeDataToSD();

WiFiUDP Udp;
IPAddress piAddress = IPAddress(192, 168, 42, 1);

bool isInShellMode = false;

void setup() {
   //Initialize serial and wait for port to open:
   Serial.begin(9600);

   // check for the presence of the shield:
   if (WiFi.status() == WL_NO_SHIELD) {
      Serial.println("WiFi shield not present");
      // don't continue:
      while (true);
   }

   if (!SD.begin(4)) {
      Serial.println("SD couldn't be initialized!");
   }

   writeData.onRun(writeDataToSD);
   writeData.setInterval(5000);
   tController.add(&writeData);

   connectToPi();
}

void loop() {
   if (WiFi.status() != WL_CONNECTED) {
      status = WL_DISCONNECTED;
      connectToPi();
   }
   else {
      if (!isInShellMode) {
         sendDataToPi();
      }
      checkForUdpData();
   }

   delay(1000);
}

double getTurbidity() {
   double volt = 0;
   double ntu = 3000;
   for(int i=0; i<800; i++) {
      volt += ((float)analogRead(turbidityPin)/1023)*5;
   }
   volt = volt/800;
   volt = round_to_dp(volt,1);
   if(volt < 2.5) {
     ntu = 3000;
   }
   else {
     ntu = -1120.4*square(volt)+5742.3*volt-4353.8;
   }
   return ntu;
}

void connectToPi() {
   // attempt to connect to WiFi network:
   while ( status != WL_CONNECTED) {
       // Make sure that since we can't send data to the pi,
      //  we will write it to the SD card every 5 seconds:
      tController.run();

      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      status = WiFi.begin(ssid, pass);
   }
   Serial.println("Connected to wifi");
   printWiFiStatus();

   Serial.println("\nStarting connection to server...");
   // if you get a connection, report back via serial:
   Udp.begin(localPort);
}

void sendDataToPi() {
   Udp.beginPacket(piAddress, localPort);

   char t[] = "Turbidity: ";
   Udp.write(t);
   char b[10];
   String(getTurbidity()).toCharArray(b, 10);
   Udp.write(b);

   char tempBuff[] = "     Temperature: ";
   Udp.write(tempBuff);
   String(getTemp()).toCharArray(b, 10);
   Udp.write(b);

   Udp.endPacket();
}

void checkForUdpData() {
   // if there's data available, read a packet
   int packetSize = Udp.parsePacket();
   if (packetSize) {
      Serial.print("Received packet of size ");
      Serial.println(packetSize);
      Serial.print("From ");
      IPAddress remoteIp = Udp.remoteIP();
      Serial.print(remoteIp);
      Serial.print(", port ");
      Serial.println(Udp.remotePort());

      // read the packet into packetBufffer
      int len = Udp.read(packetBuffer, 255);
      Udp.flush();
      if (len > 0) packetBuffer[len] = 0;
      char data[len];
      for (int i = 0; i < len; i++) {
         data[i] = packetBuffer[i];
      }
      Serial.println("Contents:");
      Serial.println(data);
      char clear[] = "sdclear";
      char read[] = "sdread";
      char sense[] = "sense";
      char quit[] = "quit";
      char shellMode[] = "shellmode";
      if(strstr(packetBuffer, shellMode)) {
         Serial.println("Entering Shell Mode!");
         isInShellMode = true;
      }
      else if(strstr(packetBuffer, quit)) {
         Udp.beginPacket(piAddress, localPort);
         char b[] = "Exiting shell mode";
         Udp.write(b);
         Udp.endPacket();
         Udp.flush();
         isInShellMode = false;
      }
      else if(strstr(packetBuffer, clear)) {
         Udp.beginPacket(piAddress, localPort);
         if (SD.exists("data.txt")) {
            Serial.println("Removed data.txt");
            SD.remove("data.txt");
            Udp.beginPacket(piAddress, localPort);
            char removed[] = "Removed data.txt";
            Udp.write(removed);
            Udp.endPacket();
         }
         else {
            char b[] = "Didn't clear it -- data.txt not found";
            Udp.write(b);
            Udp.endPacket();
         }
      }
      else if(strstr(packetBuffer, read)) {
         Udp.beginPacket(piAddress, localPort);
         if (SD.exists("data.txt")) {
            Serial.println("Found data.txt");
            File dataFile = SD.open("data.txt", FILE_READ);
            char buff[73];
            while(dataFile.available()) {
               dataFile.readStringUntil('\n').toCharArray(buff, 73, 0);
               Udp.write(buff);
               Serial.print(buff);
               Udp.endPacket();
               Udp.beginPacket(piAddress, localPort);
            }
            char end[] = "EOF";
            Udp.write(end);
            Udp.endPacket();
         }
         else {
            // char b[] = "Didn't read -- data.txt not found";
            // Udp.write(b);
            // Udp.endPacket();
            //
            // Udp.beginPacket(piAddress, localPort);
            char end[] = "EOF";
            Udp.write(end);
            Udp.endPacket();
         }
         // Udp.flush();
      }
      else if(strstr(packetBuffer, sense)) {
         sendDataToPi();
         Udp.flush();
      }
      else {
         Udp.beginPacket(piAddress, localPort);
         char b[] = "Unrecognized command";
         Udp.write(b);
         Udp.endPacket();
         Udp.flush();
      }
   }
}

void writeDataToSD() {
   Serial.println("Attempting to write to SD card... ");

   File dataFile = SD.open("data.txt", FILE_WRITE);
   if (dataFile) {
      dataFile.print("Temperature: ");
      dataFile.print(getTemp());
      dataFile.print(", Turbidity: ");
      dataFile.print(getTurbidity());
      dataFile.print("\n");
      dataFile.close();
      Serial.println("Done.");
   }
   else {
      Serial.println("Error opening the file.");
   }
}

void printWiFiStatus() {
   // print the SSID of the network you're attached to:
   Serial.print("SSID: ");
   Serial.println(WiFi.SSID());

   // print your WiFi shield's IP address:
   IPAddress ip = WiFi.localIP();
   Serial.print("IP Address: ");
   Serial.println(ip);

   // print the received signal strength:
   long rssi = WiFi.RSSI();
   Serial.print("signal strength (RSSI):");
   Serial.print(rssi);
   Serial.println(" dBm");
}

float round_to_dp( float in_value, int decimal_place )
{
  float multiplier = powf( 10.0f, decimal_place );
  in_value = roundf( in_value * multiplier ) / multiplier;
  return in_value;
}

double getTemp(){
  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
      //no more sensors on chain, reset search
      ds.reset_search();
      return -1000;
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return -1000;
  }

  if ( addr[0] != 0x10 && addr[0] != 0x28) {
      Serial.print("Device is not recognized");
      return -1000;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44,1); // start conversion, with parasite power on at the end

  ds.reset();
  ds.select(addr);
  ds.write(0xBE);

  for (int i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }

  ds.reset_search();

  byte MSB = data[1];
  byte LSB = data[0];

  float tempRead = ((MSB << 8) | LSB); //using two's compliment
  float TemperatureSum = tempRead / 16;
  double FahrenheitTemp = TemperatureSum * 9 / 5 + 32;

  return FahrenheitTemp;
}
