/**************************************

Sensor Pod -- Written by Benjamin Budai in 2019

This sketch connects the arduino to a raspberry pi
(set up as an access point with IP Address 192.168.42.1)
and publishes the readings for temperature and turbidity
every 1 second. This is done using MQTT.
If the arduino can't connect to the pi, it writes the
temperature, turbidity, and dissolved oxygen level to a text file
on the SD card (if one is connected to the wifi shield) every 5-ish
seconds until it can reconnect. It's not exactly 5 because the
process of trying to reconnect uses some time.

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
- Make sure that
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
#include <avr/pgmspace.h>
#include <EEPROM.h>
#include "arduino_secrets.h" // Holds the connection info needed to connect to the pi


//dissolved oxygen sensor analog output pin to arduino mainboard
#define DoSensorPin A14
//For arduino uno, the ADC reference is the AVCC, that is 5000mV(TYP)
#define VREF 5000

float doValue; //Current dissolved oxygen value, unit: mg/L
float temperature = 25; //The default temp is 25 Celsius

#define ReceivedBufferLength 20
char receivedBuffer[ReceivedBufferLength+1];    // store the serial command
byte receivedBufferIndex = 0;

// sum of sample point
#define SCOUNT 30
int analogBuffer[SCOUNT];    //store the analog value in the array, readed from ADC
int analogBufferTemp[SCOUNT];
int analogBufferIndex = 0,copyIndex = 0;

const float SaturationValueTab[41] PROGMEM = {      //saturation dissolved oxygen concentrations at various temperatures
   14.46, 14.22, 13.82, 13.44, 13.09,
   12.74, 12.42, 12.11, 11.81, 11.53,
   11.26, 11.01, 10.77, 10.53, 10.30,
   10.08, 9.86,  9.66,  9.46,  9.27,
   9.08,  8.90,  8.73,  8.57,  8.41,
   8.25,  8.11,  7.96,  7.82,  7.69,
   7.56,  7.43,  7.30,  7.18,  7.07,
   6.95,  6.84,  6.73,  6.63,  6.53,
   6.41,
};

//the address of the Saturation Oxygen voltage stored in the EEPROM
#define SaturationDoVoltageAddress 12
//the address of the Saturation Oxygen temperature stored in the EEPROM
#define SaturationDoTemperatureAddress 16

float SaturationDoVoltage,SaturationDoTemperature;
float averageVoltage;

#define EEPROM_write(address, p) {int i = 0; byte *pp = (byte*)&(p);for(; i < sizeof(p); i++) EEPROM.write(address+i, pp[i]);}
#define EEPROM_read(address, p)  {int i = 0; byte *pp = (byte*)&(p);for(; i < sizeof(p); i++) pp[i]=EEPROM.read(address+i);}


int status = WL_IDLE_STATUS;

char ssid[] = SECRET_SSID;     // SSID for the pi - gotten from "arduino_secrets.h"
char pass[] = SECRET_PASS;    // Password for the pi - gotten from "arduino_secrets.h"

float round_to_dp( float in_value, int decimal_place );

double getTurbidity();
int turbidityPin = A12;

void printWiFiStatus();
void connectToPi();
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


void setup() {
   //Initialize serial and wait for port to open:
   Serial.begin(9600);

   pinMode(DoSensorPin,INPUT);

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
   readDoCharacteristicValues();      //read Characteristic Values calibrated from the EEPROM
}

void loop() {
   printDoValue();
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

void printDoValue() {
   updateDoValue();
   Serial.print(F("DO Value:"));
   Serial.print(doValue,2);
   Serial.println(F("mg/L"));
}

void updateDoValue() {
      analogBuffer[analogBufferIndex] = analogRead(DoSensorPin);    //read the analog value and store into the buffer
      analogBufferIndex++;
      if(analogBufferIndex == SCOUNT)
      analogBufferIndex = 0;

      for(copyIndex=0;copyIndex<SCOUNT;copyIndex++)
      {
         analogBufferTemp[copyIndex]= analogBuffer[copyIndex];
      }
      // read the value more stable by the median filtering algorithm
      averageVoltage = getMedianNum(analogBufferTemp,SCOUNT) * (float)VREF / 1024.0;

      //calculate the do value, doValue = Voltage / SaturationDoVoltage * SaturationDoValue(with temperature compensation)
      doValue = pgm_read_float_near( &SaturationValueTab[0] + (int)(SaturationDoTemperature+0.5) ) * averageVoltage / SaturationDoVoltage;
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
      // we will write it to the SD card every 5 seconds:
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
   char payload[20];
   String(getTurbidity()).toCharArray(payload, 20);
   strcat(payload, ", ");

   char temp[6];
   String(getTemp()).toCharArray(temp, 6);
   strcat(payload, temp);

   strcat(payload, ", ");
   updateDoValue();
   String(doValue).toCharArray(temp, 6);
   strcat(payload, temp);

   client.publish("dataToPi", payload);
}

void writeDataToSD() {
   Serial.println("Attempting to write to SD card... ");

   File dataFile = SD.open("data.txt", FILE_WRITE);
   if (dataFile) {
      char payload[20];
      String(getTurbidity()).toCharArray(payload, 20);
      strcat(payload, ", ");

      char temp[6];
      String(getTemp()).toCharArray(temp, 6);
      strcat(payload, temp);

      strcat(payload, ", ");
      updateDoValue();
      String(doValue).toCharArray(temp, 6);
      strcat(payload, temp);
      strcat(payload, "\n");

      dataFile.print(payload);

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
   else if (strstr(message, "calibrate")) {
      calibrateDO();
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
   Serial.println("reconnect");
   // Loop until we're reconnected
   while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      if (client.connect("arduinoClient")) {
         Serial.println("connected");
         // ... and resubscribe
         client.subscribe("inTopic");
         client.subscribe("fileTransfer");
         client.subscribe("calibration");
      } else {
         Serial.print("failed, rc=");
         Serial.print(client.state());
         Serial.println(" try again in 5 seconds");
         // Wait 5 seconds before retrying
         delay(5000);
      }
   }
}



byte uartParse()
{
   byte modeIndex = 0;
   if(strstr(receivedBuffer, "CALIBRATION") != NULL)
   modeIndex = 1;
   else if(strstr(receivedBuffer, "EXIT") != NULL)
   modeIndex = 3;
   else if(strstr(receivedBuffer, "SATCAL") != NULL)
   modeIndex = 2;
   return modeIndex;
}

void calibrateDO() {
   client.publish("calibrationMessage", "Please expose the sensor to the air.");
   client.publish("calibrationMessage", "Make sure the temperture sensor is measureing the temperature of the calibration environment. ");
   client.publish("calibrationMessage", "Waiting 1 minute to make sure that the sensor has time to adjust.");
   delay(60000); //Wait 1 minute
   EEPROM_write(SaturationDoVoltageAddress, averageVoltage);
   EEPROM_write(SaturationDoTemperatureAddress, temperature);
   SaturationDoVoltage = averageVoltage;
   SaturationDoTemperature = temperature;
   client.publish("calibrationMessage", "Calibration complete.");
}

void doCalibration(byte mode)
{
   char *receivedBufferPtr;
   static boolean doCalibrationFinishFlag = 0,enterCalibrationFlag = 0;
   float voltageValueStore;
   switch(mode)
   {
      case 0:
      if(enterCalibrationFlag)
      Serial.println(F("Command Error"));
      break;

      case 1:
      enterCalibrationFlag = 1;
      doCalibrationFinishFlag = 0;
      Serial.println();
      Serial.println(F(">>>Enter Calibration Mode<<<"));
      Serial.println(F(">>>Please put the probe into the saturation oxygen water! <<<"));
      Serial.println();
      break;

      case 2:
      if(enterCalibrationFlag)
      {
         Serial.println();
         Serial.println(F(">>>Saturation Calibration Finish!<<<"));
         Serial.println();
         EEPROM_write(SaturationDoVoltageAddress, averageVoltage);
         EEPROM_write(SaturationDoTemperatureAddress, temperature);
         SaturationDoVoltage = averageVoltage;
         SaturationDoTemperature = temperature;
         doCalibrationFinishFlag = 1;
      }
      break;

      case 3:
      if(enterCalibrationFlag)
      {
         Serial.println();
         if(doCalibrationFinishFlag)
         Serial.print(F(">>>Calibration Successful"));
         else
         Serial.print(F(">>>Calibration Failed"));
         Serial.println(F(",Exit Calibration Mode<<<"));
         Serial.println();
         doCalibrationFinishFlag = 0;
         enterCalibrationFlag = 0;
      }
      break;
   }
}

int getMedianNum(int bArray[], int iFilterLen)
{
   int bTab[iFilterLen];
   for (byte i = 0; i<iFilterLen; i++)
   {
      bTab[i] = bArray[i];
   }
   int i, j, bTemp;
   for (j = 0; j < iFilterLen - 1; j++)
   {
      for (i = 0; i < iFilterLen - j - 1; i++)
      {
         if (bTab[i] > bTab[i + 1])
         {
            bTemp = bTab[i];
            bTab[i] = bTab[i + 1];
            bTab[i + 1] = bTemp;
         }
      }
   }
   if ((iFilterLen & 1) > 0)
   bTemp = bTab[(iFilterLen - 1) / 2];
   else
   bTemp = (bTab[iFilterLen / 2] + bTab[iFilterLen / 2 - 1]) / 2;
   return bTemp;
}


void readDoCharacteristicValues(void)
{
   EEPROM_read(SaturationDoVoltageAddress, SaturationDoVoltage);
   EEPROM_read(SaturationDoTemperatureAddress, SaturationDoTemperature);
   if(EEPROM.read(SaturationDoVoltageAddress)==0xFF && EEPROM.read(SaturationDoVoltageAddress+1)==0xFF && EEPROM.read(SaturationDoVoltageAddress+2)==0xFF && EEPROM.read(SaturationDoVoltageAddress+3)==0xFF)
   {
      SaturationDoVoltage = 1127.6;   //default voltage:1127.6mv
      EEPROM_write(SaturationDoVoltageAddress, SaturationDoVoltage);
   }
   if(EEPROM.read(SaturationDoTemperatureAddress)==0xFF && EEPROM.read(SaturationDoTemperatureAddress+1)==0xFF && EEPROM.read(SaturationDoTemperatureAddress+2)==0xFF && EEPROM.read(SaturationDoTemperatureAddress+3)==0xFF)
   {
      SaturationDoTemperature = 25.0;   //default temperature is 25^C
      EEPROM_write(SaturationDoTemperatureAddress, SaturationDoTemperature);
   }
}
