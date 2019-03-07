#This is a simple script to listen for UDP strings from the arduino, and write them to a file
import socket
import os
import threading
import datetime
import time

UDP_IP_ADDRESS = "192.168.42.1" #The IP address of this pi

UDP_PORT_NO = 2390

serverSock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
serverSock.bind((UDP_IP_ADDRESS, UDP_PORT_NO))

now = datetime.datetime.now()
dataFile = open(now.isoformat(), "w")

#sem = threading.Semaphore()

def readData():
        while True:
#		sem.acquire()
                data, addr = serverSock.recvfrom(1024)
                dataFile.write("{}\n".format(data))
#		sem.release()

#def takeOrders():
#        while True:
#		sem.acquire()
#                command = input("SensorPod>>>")
#                serverSock.sendto(str(command), addr)
#		sem.release()
#
#t1 = threading.Thread(target=readData())
#t2 = threading.Thread(target=takeOrders())
#t2.start()
#t1.start()

readData()
