import socket
import serial

# --- CONFIGURACIÓN ---
SERIAL_PORT = "/dev/ttyACM0"
BAUDRATE    = 9600

HOST = "0.0.0.0"
PORT = 5001
# ----------------------

VALID_CMDS = {"LED_ON", "LED_OFF"}

def main():
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
    print(f"Conectado a Arduino en {SERIAL_PORT} a {BAUDRATE} baudios")
    print(f"Servidor LED escuchando en {HOST}:{PORT}...")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(1)

        while True:
            conn, addr = s.accept()
            with conn:
                print(f"Conexión desde {addr}")

                data = conn.recv(1024)
                if not data:
                    continue

                cmd = data.decode("utf-8", errors="ignore").strip().upper()

                if cmd not in VALID_CMDS:
                    conn.sendall(b"ERR:CMD\n")
                    continue

                # IMPORTANTE: Arduino lee hasta '\n'
                ser.write((cmd + "\n").encode("utf-8"))
                ser.flush()

                # Arduino responde: OK:LED_ON / OK:LED_OFF / ERR:CMD
                resp = ser.readline().decode("utf-8", errors="ignore").strip()
                if not resp:
                    resp = "ERR:TIMEOUT"

                conn.sendall((resp + "\n").encode("utf-8"))

if __name__ == "__main__":
    main()
