from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import time

# Configuración
APP_USER = "admin"
APP_PW_HASH = generate_password_hash("1234")  # Contraseña: 1234
SECRET_KEY = "CLAVE_SECRETA_SENSORES"

app = Flask(__name__)
app.secret_key = SECRET_KEY

# Base de datos simulada de sensores
sensor_data = {
    "humedad": 45,
    "temperatura": 23
}

def is_logged_in():
    return session.get("logged_in") is True

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

@app.route("/api/sensores", methods=["GET"])
def get_sensores():
    if not is_logged_in():
        return jsonify({"error": "No autorizado"}), 401
    return jsonify(sensor_data)

@app.route("/api/sensores", methods=["POST"])
def update_sensor():
    if not is_logged_in():
        return jsonify({"error": "No autorizado"}), 401
    
    data = request.get_json()
    sensor = data.get("sensor")
    valor = float(data.get("valor"))
    
    if sensor in sensor_data:
        # Limitar rangos
        if sensor == "humedad":
            sensor_data[sensor] = max(0, min(100, valor))
        elif sensor == "temperatura":
            sensor_data[sensor] = max(-10, min(50, valor))
        
        return jsonify({
            "success": True,
            "sensor": sensor,
            "valor": sensor_data[sensor]
        })
    
    return jsonify({"success": False, "error": "Sensor no válido"}), 400

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
