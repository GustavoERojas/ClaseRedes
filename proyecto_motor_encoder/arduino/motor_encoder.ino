from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from werkzeug.security import check_password_hash
import serial
import time

app = Flask(__name__)
app.secret_key = "REDES"

APP_USER = "admin"

APP_PW_HASH = "scrypt:32768:8:1$fJBGT1vXjplpTKs4$31608a996a71fee1935865c481cb59268bde99dda64bcb59ef6c212affd6882cf0b01f2c66a554dfd12605031abc9a816fe783e0adb7c800f4bf0fec198b66c0"

arduino = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
time.sleep(2)

ultimo_estado = {
    "cmd": "P",
    "pulsos": 0,
    "vueltas": 0.0,
    "rpm": 0.0,
    "raw": "Esperando datos..."
}


def is_logged_in():
    return session.get("logged_in", False)


def leer_serial():
    global ultimo_estado

    try:
        while arduino.in_waiting > 0:
            linea = arduino.readline().decode(
                "utf-8",
                errors="ignore"
            ).strip()

            if linea:
                print("SERIAL:", linea)
                ultimo_estado["raw"] = linea

                try:
                    if "Cmd:" in linea:
                        cmd = linea.split("Cmd:")[1].split("Pulsos:")[0].strip()

                        pulsos = int(
                            linea.split("Pulsos:")[1]
                            .split("Vueltas:")[0]
                            .strip()
                        )

                        vueltas = float(
                            linea.split("Vueltas:")[1]
                            .split("RPM:")[0]
                            .strip()
                        )

                        rpm = float(
                            linea.split("RPM:")[1]
                            .split("A:")[0]
                            .strip()
                        )

                        ultimo_estado["cmd"] = cmd
                        ultimo_estado["pulsos"] = pulsos
                        ultimo_estado["vueltas"] = vueltas
                        ultimo_estado["rpm"] = rpm

                except Exception as e:
                    print("Error parseando:", e)

    except Exception as e:
        ultimo_estado["raw"] = f"Error serial: {str(e)}"


@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        user = request.form.get("username", "").strip()
        pw = request.form.get("password", "")

        if user == APP_USER and check_password_hash(APP_PW_HASH, pw):
            session["logged_in"] = True
            return redirect(url_for("index"))

        return render_template(
            "login.html",
            error="Usuario o contraseña incorrectos"
        )

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

    leer_serial()

    return jsonify({
        "ok": True,
        "cmd": ultimo_estado["cmd"],
        "pulsos": ultimo_estado["pulsos"],
        "vueltas": ultimo_estado["vueltas"],
        "rpm": ultimo_estado["rpm"],
        "raw": ultimo_estado["raw"]
    })


@app.route("/control", methods=["POST"])
def control():
    if not is_logged_in():
        return jsonify({
            "ok": False,
            "error": "No autorizado"
        })

    data = request.get_json()
    comando = data.get("comando", "")

    if comando in ["A", "R", "P", "Z"]:
        print("ENVIANDO:", comando)
        arduino.write((comando + "\n").encode())
        arduino.flush()

        return jsonify({
            "ok": True,
            "mensaje": f"Comando {comando} enviado correctamente"
        })

    return jsonify({
        "ok": False,
        "error": "Comando inválido"
    })


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        debug=True
    )