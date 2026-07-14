from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from werkzeug.security import check_password_hash
import serial
import serial.tools.list_ports
import time
import threading
import re
from datetime import datetime

app = Flask(__name__)
app.secret_key = "REDES_INDUSTRIALES_2024"

APP_USER = "admin"
APP_PW_HASH = "scrypt:32768:8:1$fJBGT1vXjplpTKs4$31608a996a71fee1935865c481cb59268bde99dda64bcb59ef6c212affd6882cf0b01f2c66a554dfd12605031abc9a816fe783e0adb7c800f4bf0fec198b66c0"

# ========== CONEXIÓN SERIAL ==========
arduino = None
ultimo_mensaje = ""

def conectar_arduino():
    global arduino
    try:
        puertos = serial.tools.list_ports.comports()
        print("🔍 Puertos disponibles:")
        for puerto in puertos:
            print(f"   - {puerto.device}: {puerto.description}")
        
        for puerto in puertos:
            if "COM" in puerto.device or "ttyACM" in puerto.device or "ttyUSB" in puerto.device:
                print(f"🔌 Conectando a {puerto.device}...")
                arduino = serial.Serial(puerto.device, 115200, timeout=1)
                time.sleep(2)
                arduino.reset_input_buffer()
                print(f"✅ Conectado exitosamente a {puerto.device}")
                return True
        
        print("❌ No se encontró Arduino")
        return False
    except Exception as e:
        print(f"❌ Error de conexión: {e}")
        return False

conectar_arduino()

# ========== ESTADO DEL SISTEMA ==========
ultimo_estado = {
    "estado": "DETENIDO",
    "rpm": 0.0,
    "referencia": 0.0,
    "error": 0.0,
    "pwm": 0,
    "semaforo": "ROJO",
    "color_detectado": "NINGUNO",
    "rojo": 0,
    "verde": 0,
    "azul": 0,
    "piezas_rojas": 0,
    "piezas_verdes": 0,
    "piezas_azules": 0,
    "piezas_desconocidas": 0,
    "historial_colores": [],
    "raw": "Esperando datos..."
}

# ========== FUNCIONES ==========
def is_logged_in():
    return session.get("logged_in", False)

def leer_serial():
    global ultimo_estado, ultimo_mensaje
    if arduino is None or not arduino.is_open:
        return

    try:
        while arduino.in_waiting > 0:
            linea = arduino.readline().decode("utf-8", errors="ignore").strip()
            if linea:
                print(f"📡 {linea}")
                ultimo_estado["raw"] = linea
                ultimo_mensaje = linea

                # Buscar el mensaje de estado principal
                if "⚡ RPM:" in linea:
                    try:
                        rpm_match = re.search(r'RPM:\s*([\d\.]+)', linea)
                        pwm_match = re.search(r'PWM:\s*(\d+)', linea)
                        error_match = re.search(r'Error:\s*([-\d\.]+)', linea)
                        piezas_match = re.search(r'📦:\s*(\d+)', linea)
                        clasificadas_match = re.search(r'✅:\s*(\d+)', linea)
                        azules_match = re.search(r'🔵:\s*(\d+)', linea)
                        
                        if rpm_match: ultimo_estado["rpm"] = float(rpm_match.group(1))
                        if pwm_match: ultimo_estado["pwm"] = int(pwm_match.group(1))
                        if error_match: ultimo_estado["error"] = float(error_match.group(1))
                    except Exception as e:
                        print(f"Error parseando: {e}")

                # Buscar mensajes de detección de color
                if "ROJO detectado" in linea:
                    ultimo_estado["color_detectado"] = "ROJO"
                    ultimo_estado["piezas_rojas"] += 1
                    ultimo_estado["historial_colores"].insert(0, {"color": "ROJO", "tiempo": datetime.now().strftime("%H:%M:%S")})
                    if len(ultimo_estado["historial_colores"]) > 10:
                        ultimo_estado["historial_colores"].pop()
                
                elif "VERDE detectado" in linea:
                    ultimo_estado["color_detectado"] = "VERDE"
                    ultimo_estado["piezas_verdes"] += 1
                    ultimo_estado["historial_colores"].insert(0, {"color": "VERDE", "tiempo": datetime.now().strftime("%H:%M:%S")})
                    if len(ultimo_estado["historial_colores"]) > 10:
                        ultimo_estado["historial_colores"].pop()
                
                elif "AZUL detectado" in linea:
                    ultimo_estado["color_detectado"] = "AZUL"
                    ultimo_estado["piezas_azules"] += 1
                    ultimo_estado["historial_colores"].insert(0, {"color": "AZUL", "tiempo": datetime.now().strftime("%H:%M:%S")})
                    if len(ultimo_estado["historial_colores"]) > 10:
                        ultimo_estado["historial_colores"].pop()
                
                elif "Pieza manual:" in linea:
                    if "ROJO" in linea:
                        ultimo_estado["color_detectado"] = "ROJO"
                        ultimo_estado["piezas_rojas"] += 1
                        ultimo_estado["historial_colores"].insert(0, {"color": "ROJO", "tiempo": datetime.now().strftime("%H:%M:%S")})
                        if len(ultimo_estado["historial_colores"]) > 10:
                            ultimo_estado["historial_colores"].pop()
                    elif "VERDE" in linea:
                        ultimo_estado["color_detectado"] = "VERDE"
                        ultimo_estado["piezas_verdes"] += 1
                        ultimo_estado["historial_colores"].insert(0, {"color": "VERDE", "tiempo": datetime.now().strftime("%H:%M:%S")})
                        if len(ultimo_estado["historial_colores"]) > 10:
                            ultimo_estado["historial_colores"].pop()
                    elif "AZUL" in linea:
                        ultimo_estado["color_detectado"] = "AZUL"
                        ultimo_estado["piezas_azules"] += 1
                        ultimo_estado["historial_colores"].insert(0, {"color": "AZUL", "tiempo": datetime.now().strftime("%H:%M:%S")})
                        if len(ultimo_estado["historial_colores"]) > 10:
                            ultimo_estado["historial_colores"].pop()

                # Buscar estado del sistema
                if "Sistema DETENIDO" in linea:
                    ultimo_estado["estado"] = "DETENIDO"
                elif "PRODUCCIÓN INICIADA" in linea:
                    ultimo_estado["estado"] = "PRODUCIENDO"
                elif "Sistema pausado" in linea:
                    ultimo_estado["estado"] = "PAUSADO"

    except Exception as e:
        print(f"Error serial: {e}")

def enviar_comando_arduino(comando):
    if arduino is None or not arduino.is_open:
        print(f"⚠️ No se puede enviar {comando}: Arduino no conectado")
        return False
    try:
        arduino.write((comando + "\n").encode())
        arduino.flush()
        print(f"📤 ENVIADO: {comando}")
        time.sleep(0.05)
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

# ========== RUTAS ==========
@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        user = request.form.get("username", "").strip()
        pw = request.form.get("password", "")
        if user == APP_USER and check_password_hash(APP_PW_HASH, pw):
            session["logged_in"] = True
            return redirect(url_for("index"))
        return render_template("login.html", error="Usuario o contraseña incorrectos")
    return render_template("login.html", error=None)

@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))

@app.route("/")
def index():
    if not is_logged_in():
        return redirect(url_for("login"))
    return render_template("index.html")

@app.route("/api/data")
def api_data():
    if not is_logged_in():
        return jsonify({"ok": False})
    
    if arduino and arduino.is_open:
        enviar_comando_arduino("GET_STATUS")
        time.sleep(0.05)
        leer_serial()

    return jsonify({
        "ok": True,
        "estado": ultimo_estado["estado"],
        "rpm": round(ultimo_estado["rpm"], 1),
        "referencia": round(ultimo_estado["referencia"], 1),
        "error": round(ultimo_estado["error"], 1),
        "pwm": ultimo_estado["pwm"],
        "semaforo": ultimo_estado["semaforo"],
        "color_detectado": ultimo_estado["color_detectado"],
        "rojo": ultimo_estado["rojo"],
        "verde": ultimo_estado["verde"],
        "azul": ultimo_estado["azul"],
        "piezas_rojas": ultimo_estado["piezas_rojas"],
        "piezas_verdes": ultimo_estado["piezas_verdes"],
        "piezas_azules": ultimo_estado["piezas_azules"],
        "piezas_desconocidas": ultimo_estado["piezas_desconocidas"],
        "historial_colores": ultimo_estado["historial_colores"],
        "raw": ultimo_estado["raw"]
    })

@app.route("/api/set_referencia", methods=["POST"])
def set_referencia():
    if not is_logged_in():
        return jsonify({"ok": False, "error": "No autorizado"})
    
    data = request.get_json()
    nueva_ref = float(data.get("rpm_referencia", 0))
    
    enviar_comando_arduino(f"SET_REF:{nueva_ref}")
    
    return jsonify({
        "ok": True,
        "rpm_referencia": nueva_ref,
        "mensaje": f"Referencia establecida a {nueva_ref} RPM"
    })

@app.route("/control", methods=["POST"])
def control():
    if not is_logged_in():
        return jsonify({"ok": False, "error": "No autorizado"})

    data = request.get_json()
    comando = data.get("comando", "")

    comandos_map = {
        "O": "O",
        "S": "S",
        "P": "P",
        "M": "M"
    }
    
    if comando in comandos_map:
        enviar_comando_arduino(comandos_map[comando])
        return jsonify({"ok": True, "mensaje": f"Comando {comando} enviado"})

    return jsonify({"ok": False, "error": "Comando inválido"})

@app.route("/api/reset", methods=["POST"])
def reset():
    if not is_logged_in():
        return jsonify({"ok": False, "error": "No autorizado"})
    
    enviar_comando_arduino("P")
    return jsonify({"ok": True, "mensaje": "Sistema reseteado"})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)