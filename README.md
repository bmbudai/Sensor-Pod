# Sensor-Pod
This project is an arduino project that allows data to be sent from a wifi shield equipped arduino to a raspberry pi access point.


Data is transferred using the MQTT protocol, with the broker being on the raspberry pi. If the Pod isn't able to connect to the pi, it stores data to an SD card if there is one inserted into the slot on the wifi shield. If you want some basic documentation or to see pictures, please take a look at Basic_Documentation.pdf.
