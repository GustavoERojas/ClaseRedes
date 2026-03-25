from flask import Flask, render_template, jsonify, request, session, redirect
import serial
import time

app = Flask(__name__)
app.secret_key = "clave_secreta"

# ⚠️ CAMBIA TU PUERTO SI ES NECESARIO
try:
    arduino = serial.Serial("COM14", 9600, timeout=1)
    time.sleep(2)  # Esperar a que se estabilice la conexión
    print("✅ Conectado al Arduino en COM14")
except:
    print("⚠️ No se pudo conectar al Arduino. Verifica el puerto.")
    arduino = None

# 🔐 LOGIN
USER = "*********"
PASSWORD_HASH = "*******************"

from werkzeug.security import check_password_hash

def is_logged():
    return session.get("login", False)

@app.route("/login", methods=["GET","POST"])
def login():
    if request.method == "POST":
        user = request.form["user"]
        pw = request.form["password"]

        if user == USER and check_password_hash(PASSWORD_HASH, pw):
            session["login"] = True
            return redirect("/")
        return render_template("login.html", error="Datos incorrectos")

    return render_template("login.html")

@app.route("/")
def index():
    if not is_logged():
        return redirect("/login")
    return render_template("index.html")

@app.route("/api/data")
def data():
    if not is_logged():
        return jsonify({"error":"no autorizado"}), 401

    if arduino is None:
        return jsonify({"error":"arduino no conectado"}), 500

    try:
        # Limpiar el buffer y leer línea
        arduino.reset_input_buffer()
        line = arduino.readline().decode().strip()
        
        print(f"📡 Datos recibidos: {line}")  # Para depuración

        if "TEMP" in line and "HUM" in line:
            partes = line.split(",")
            
            temp = float(partes[0].split(":")[1])
            hum = float(partes[1].split(":")[1])
            pwm = int(partes[2].split(":")[1])
            rango = partes[3].split(":")[1]

            return jsonify({
                "temp": temp,
                "hum": hum,
                "pwm": pwm,
                "rango": rango
            })
    except Exception as e:
        print(f"❌ Error leyendo datos: {e}")
        pass

    return jsonify({"error":"sin datos"})

@app.route("/logout")
def logout():
    session.clear()
    return redirect("/login")

if __name__ == "__main__":
    app.run(debug=True, use_reloader=False, host="0.0.0.0", port=5000)
