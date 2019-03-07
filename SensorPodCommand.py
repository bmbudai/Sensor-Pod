#
# This Script connects to the SensorPod sketch on arduino and allows the user to send
# commands to the arduino. This includes reading the current values on the sensors,
# as well as reading from a data file (the result of a lost connection) and clearing
# the data file as well.
#
# Written by Benjamin Budai in 2019
#

import socket
import os
import sys
import atexit

UDP_IP_ADDRESS = '192.168.42.1' #The IP address of this pi

UDP_PORT_NO = 2390

serverSock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
serverSock.bind((UDP_IP_ADDRESS, UDP_PORT_NO))
data, addr = serverSock.recvfrom(1024)
serverSock.sendto("shellmode", addr);
data, addr = serverSock.recvfrom(1024)

def takeOrders():
        while True:
                command = raw_input("\033[1;32mSensorPod>>>\033[0;0m ")
		if command == "help":
			printHelp()
		elif command == "quit":
			quit()
		elif command == "sdread":
			sdread()
		elif command == "sdclear":
			sdclear()
		elif command == "sense":
			sense()
		elif command == "clear":
			clear()
		else:
			print("\033[1;31mThat command is unrecognized. Enter \"help\" for available commands.")

def printHelp():
	print("\n\nThis shell allows you to send commands to the arduino running the Sensor Pod.")
	print("Here are the available commands:")
	print("\nhelp  -- Show this help message")
	print("""\nsdread -- read the contents of the SD card on the arduino. 
	If there is an SD card, the contents of the data file holding 
	sensor information from a period of disconnection are saved to 
	a file on this pi""")
	print("\nsdclear -- delete the data file from the arduino")
	print("\nsense -- show the current reading from all of the sensors")
	print("\nclear -- clear the screen")
	print("\nquit -- Close this shell\n\n")

def exit():
	serverSock.sendto("quit", addr)
	response = getResponse()
	print(response)

def sdread():
	serverSock.sendto("sdread", addr)
	getFile()

def sdclear():
	yn = raw_input("\033[1;37;41mAre you sure you want to delete the Pod's data output file? (y/n)\033[0;0m ")
	if (yn == "y") | (yn == "Y"):
		serverSock.sendto("sdclear", addr)
		response = getResponse()
		print(response)
	else:
		print("\033[1;31mCancelled\033[0;0m")

def clear():
	os.system('clear')

def sense():
	serverSock.sendto("sense", addr)
	response = getResponse()
	print(response)

def getResponse():
	response, addr = serverSock.recvfrom(1024)
	return(response)

def getFile():
	filename = raw_input("What would you like to name the data file? ")
	if filename == "":
		empty = True
		print "No filename was specified... printing to the screen instead."
                while True:
                        response, addr = serverSock.recvfrom(1024)
                        if response != "EOF":
				if empty == True: empty = False	
                                print response
                        else:
				if empty == True:
					print("\033[1;31mThere was nothing to be read.\033[0;0m")
				else:
					print "Done"
				break

	else:
		dataFile = -1
		while True:
			response, addr = serverSock.recvfrom(1024)
			if response != "EOF":
				if dataFile == -1 : dataFile = open(filename, "w")
				dataFile.write("{}\n".format(response))
			else: break
		if dataFile != -1:
			dataFile.close()
			print "Wrote the data to {}.".format(filename)
		else:
			print("\033[1;31mThere was nothing to be read.\033[0;0m")

def exit_handler():
	exit()
atexit.register(exit_handler)

printHelp()
takeOrders()
