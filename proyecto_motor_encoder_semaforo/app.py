from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from werkzeug.security import check_password_hash
import serial
import serial.tools.list_ports
import time
import threading
import re

app = Flask(__name__)
app.secret_key = "REDES"

APP_USER = "admin"
APP_PW_HASH = "scrypt:32768:8:1$fJBGT1vXjplpTKs4$31608a996a71fee1935865c481cb59268bde99dda64bcb59ef6c212affd6882cf0b01f2c66a554dfd12605031abc9a816fe783e0adb7c800f4bf0fec198b66c0"

# ========== CONEXIÓN SERIAL ==========
arduino = None

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

# ========== ESTADO DEL MOTOR ==========
ultimo_estado = {
    "estado": "PARO",
    "pulsos": 0,
    "vueltas": 0.0,
    "rpm": 0.0,
    "referencia": 0.0,
    "error": 0.0,
    "error_abs": 0.0,
    "error_pct": 0.0,
    "pwm": 0,
    "integral": 0.0,
    "semaforo": "ROJO",
    "raw": "Esperando datos..."
}

def is_logged_in():
    return session.get("logged_in", False)

def leer_serial():
    global ultimo_estado
    if arduino is None or not arduino.is_open:
        return

    try:
        while arduino.in_waiting > 0:
            linea = arduino.readline().decode("utf-8", errors="ignore").strip()
            if linea:
                print(f"📡 {linea}")
                ultimo_estado["raw"] = linea

                if "OK:STATUS" in linea:
                    try:
                        estado_match = re.search(r'estado=([A-Z]+)', linea)
                        pulsos_match = re.search(r'pulsos=(\d+)', linea)
                        vueltas_match = re.search(r'vueltas=([\d\.]+)', linea)
                        rpm_match = re.search(r'rpm=([\d\.]+)', linea)
                        ref_match = re.search(r'referencia=([\d\.]+)', linea)
                        error_match = re.search(r'error=([-\d\.]+)', linea)
                        errorabs_match = re.search(r'errorabs=([\d\.]+)', linea)
                        errorpct_match = re.search(r'errorpct=([\d\.]+)', linea)
                        pwm_match = re.search(r'pwm=(\d+)', linea)
                        integral_match = re.search(r'integral=([\d\.]+)', linea)
                        semaforo_match = re.search(r'semaforo=([A-Z]+)', linea)
                        
                        if estado_match: ultimo_estado["estado"] = estado_match.group(1)
                        if pulsos_match: ultimo_estado["pulsos"] = int(pulsos_match.group(1))
                        if vueltas_match: ultimo_estado["vueltas"] = float(vueltas_match.group(1))
                        if rpm_match: ultimo_estado["rpm"] = float(rpm_match.group(1))
                        if ref_match: ultimo_estado["referencia"] = float(ref_match.group(1))
                        if error_match: ultimo_estado["error"] = float(error_match.group(1))
                        if errorabs_match: ultimo_estado["error_abs"] = float(errorabs_match.group(1))
                        if errorpct_match: ultimo_estado["error_pct"] = float(errorpct_match.group(1))
                        if pwm_match: ultimo_estado["pwm"] = int(pwm_match.group(1))
                        if integral_match: ultimo_estado["integral"] = float(integral_match.group(1))
                        if semaforo_match: ultimo_estado["semaforo"] = semaforo_match.group(1)
                    except Exception as e:
                        print(f"Error parseando: {e}")
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
        "pulsos": ultimo_estado["pulsos"],
        "vueltas": round(ultimo_estado["vueltas"], 4),
        "rpm": round(ultimo_estado["rpm"], 1),
        "referencia": round(ultimo_estado["referencia"], 1),
        "error": round(ultimo_estado["error"], 1),
        "error_abs": round(ultimo_estado["error_abs"], 1),
        "error_pct": round(ultimo_estado["error_pct"], 1),
        "pwm": ultimo_estado["pwm"],
        "integral": round(ultimo_estado["integral"], 1),
        "semaforo": ultimo_estado["semaforo"],
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
        "A": "RIGHT",
        "P": "STOP"
    }
    
    if comando in comandos_map:
        enviar_comando_arduino(comandos_map[comando])
        return jsonify({"ok": True, "mensaje": f"Comando {comando} enviado"})

    return jsonify({"ok": False, "error": "Comando inválido"})

@app.route("/api/reset", methods=["POST"])
def reset():
    if not is_logged_in():
        return jsonify({"ok": False, "error": "No autorizado"})
    
    enviar_comando_arduino("RESET")
    return jsonify({"ok": True, "mensaje": "Contador resetado"})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)