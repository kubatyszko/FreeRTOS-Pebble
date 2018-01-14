#!/usr/bin/python
import bluetooth
import thread
import subprocess
from time import sleep
from struct import *

s = "hello....."
t = s.encode("utf-8")
enc = pack("<LH%dsH" % (len(s),), 0xbaadd00d, len(s), str(t), 0xdec0)
print enc

#devices = bluetooth.discover_devices(lookup_names=True)
#print(type(devices))

#print("Devices found: %s" % len(devices))

#for item in devices:
#    print(item)

#bd_addr = "B0:B4:48:89:0E:F2"
bd_addr = "B0:B4:48:99:2E:C3"
port = 1
passkey = "0000"

# kill any "bluetooth-agent" process that is already running
subprocess.call("kill -9 `pidof bluetooth-agent`",shell=True)

# Start a new "bluetooth-agent" process where XXXX is the passkey
status = subprocess.call("bluetooth-agent " + passkey + " &",shell=True)

print "Connecting..."
sock=bluetooth.BluetoothSocket( bluetooth.RFCOMM )
sock.connect((bd_addr, port))

print "Connected!"

def rx_data():
	while True:
		data = sock.recv(10)
		print 'Received', data
		sleep(0.1)
	s.close()

thread.start_new_thread( rx_data, () )

while 1:
	texta = raw_input()
	if texta == "quit":
		break
	if texta[:2] == "a ":
		s = texta[2:]
		t = s.encode("utf-8")
		enc = pack("<LH%dsH" % (len(s),), 0xbaadd00d, len(s), str(t), 0xc0de)
		sock.send(enc)
#	sock.send(bytes(texta.encode('UTF-8')))
s.close()


