import serial

arduino = serial.Serial("COM14", 9600, timeout=1)

while True:
    linea = arduino.readline().decode().strip()
    print(linea)