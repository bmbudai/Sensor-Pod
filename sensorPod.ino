/**************************************
Sensor Pod -- Written by Benjamin Budai in 2019
This sketch connects the arduino to a raspberry pi
(set up as an access point with IP Address 192.168.42.1)
and sends it the readings for temperature and turbidity
every 1 second. This is done using MQTT.
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
#include <PubSubClient.h>
#include <Wire.h>
#include <OneWire.h>
#include <SD.h>
#include <Thread.h>
#include <ThreadController.h>
#include "arduino_secrets.h" // Holds the connection info needed to connect to the pi

int status = WL_IDLE_STATUS;

char ssid[] = SECRET_SSID;     // SSID for the pi - gotten from "arduino_secrets.h"
char pass[] = SECRET_PASS;    // Password for the pi - gotten from "arduino_secrets.h"

float round_to_dp( float in_value, int decimal_place );

double getTurbidity();
int turbidityPin = A12;

void printWiFiStatus();
void connectToPi();
unsigned int localPort = 2390;      // Port to listen on and send on
char packetBuffer[255]; //buffer to hold incoming packet
void sendDataToPi();

void sendFile();
bool deleteFile();

//Temperature chip i/o
int DS18S20_Pin = 2;
OneWire ds(DS18S20_Pin);
double getTemp();

Thread writeData = Thread();
ThreadController tController = ThreadController();

void writeDataToSD();

IPAddress piAddress = IPAddress(192, 168, 42, 1);
IPAddress server(192, 168, 42, 1);
WiFiClient wifiClient;
PubSubClient client(wifiClient);
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);

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
   client.setServer(server, 1883);
   client.setCallback(callback);

   if (!SD.begin(4)) {
      Serial.println("SD couldn't be initialized!");
   }

   writeData.onRun(writeDataToSD);
   writeData.setInterval(5000);
   tController.add(&writeData);

   connectToPi();
}

void loop() {

   while (!isInShellMode) {
      if (WiFi.status() != WL_CONNECTED) {
         status = WL_DISCONNECTED;
         connectToPi();
      }
      if (!client.connected()) {
         reconnect();
      }
      sendDataToPi();
      client.loop();

      delay(1000);
   }
   while (isInShellMode) {
      if (WiFi.status() != WL_CONNECTED) {
         status = WL_DISCONNECTED;
         connectToPi();
      }
      if (!client.connected()) {
         reconnect();
      }
   }
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
      status = WiFi.begin(ssid, pass);
   }
   Serial.println("Connected to wifi");
   printWiFiStatus();

   Serial.println("\nStarting connection to server...");
}

void sendDataToPi() {
   char b[10];
   String(getTurbidity()).toCharArray(b, 10);

   client.publish("dataToPi", b);

   char tempBuff[] = ",";
   String(getTemp()).toCharArray(b, 10);
   client.publish("dataToPi", tempBuff);
   client.publish("dataToPi", b);
}

void writeDataToSD() {
   Serial.println("Attempting to write to SD card... ");

   File dataFile = SD.open("data.txt", FILE_WRITE);
   if (dataFile) {
      dataFile.print(getTurbidity());
      dataFile.print(",");
      dataFile.print(getTemp());
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

void callback(char* topic, byte* payload, unsigned int length) {
   Serial.print("Message arrived [");
   Serial.print(topic);
   Serial.print("] ");

   char message[length];
   for (unsigned int i=0;i<length;i++) {
      Serial.print((char)payload[i]);
      message[i] = (char)payload[i];
   }
   Serial.println();

   if (strstr(message, "Read SD File")) {
      Serial.println("Sending file contents...");
      sendFile();
   }
   else if (strstr(message, "Delete SD File")) {
      Serial.println("Delete");
      if (deleteFile()) {
         client.publish("messageForPi", "Deleted the data file.");
      }
      else {
         client.publish("messageForPi", "File couldn't be deleted.");
      }
   }
   else {
      Serial.println("Unrecognized message.");
   }
}

void sendFile() {
   if (SD.exists("data.txt")) {
      Serial.println("Found data.txt");
      File dataFile = SD.open("data.txt", FILE_READ);
      char buff[73];
      while(dataFile.available()) {
         dataFile.readStringUntil('\n').toCharArray(buff, 73, 0);
         client.publish("fileToPi", buff);
      }
   }
   else {
      client.publish("messageForPi", "Data file not found!");
      Serial.println("Data file not found!");
   }
}

bool deleteFile() {
   if (SD.exists("data.txt")) {
      Serial.println("Removed data.txt");
      SD.remove("data.txt");
      return true;
   }
   else {
      return false;
   }
}

void reconnect() {
   // Loop until we're reconnected
   while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      if (client.connect("arduinoClient")) {
         Serial.println("connected");
         // ... and resubscribe
         client.subscribe("inTopic");
         client.subscribe("fileTransfer");
      } else {
         Serial.print("failed, rc=");
         Serial.print(client.state());
         Serial.println(" try again in 5 seconds");
         // Wait 5 seconds before retrying
         delay(5000);
      }
   }
}
