#include <avr/interrupt.h>

// ================= PINES PUENTE H =================
const int ENA = 9;
const int IN1 = 7;
const int IN2 = 8;

// ================= ENCODER (CUADRATURA COMPLETA) =================
const int ENC_A = 2;  // Canal A (verde)
const int ENC_B = 3;  // Canal B (amarillo)

// ================= LEDS FÍSICOS =================
const int LED_VERDE = 4;
const int LED_AMARILLO = 5;
const int LED_ROJO = 6;

// ================= VARIABLES ENCODER =================
volatile long pulsos = 0;
volatile byte estadoAnterior = 0;

// Ajusta este valor según la medición real de tu encoder
const float PULSOS_POR_VUELTA = 2076.0;

// Tabla de cuadratura
const int8_t tablaCuadratura[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// ================= ESTADO MOTOR =================
String estadoMotor = "PARO";

// ================= RPM Y REFERENCIA =================
float referenciaRPM = 100.0;
float rpmActual = 0.0;
float rpmFiltrada = 0.0;

// ================= PID (AJUSTADO FINAL) =================
float errorRPM = 0.0;
float errorAnterior = 0.0;
float integral = 0.0;
float derivada = 0.0;

// ⭐ PARÁMETROS REAJUSTADOS PARA MEJOR RESPUESTA
float KP = 0.20;   // Proporcional (aumentado)
float KI = 0.15;   // Integral (aumentado - clave para eliminar error)
float KD = 0.008;  // Derivativo (aumentado)

// ================= PWM (CORREGIDO) =================
float pwmControl = 0.0;
int pwmAplicado = 0;

const int PWM_MAX = 255;
const int PWM_ARRANQUE = 160;           // Empuje inicial para arrancar
const int PWM_UMBRAL_ARRANQUE = 40;     // ⭐ Reducido de 55
const int PWM_MIN_MOVIMIENTO = 0;       // Puede llegar a 0

// ================= TIEMPO PID =================
unsigned long tiempoAnteriorPID = 0;
long pulsosAnterioresPID = 0;

const unsigned long INTERVALO_PID = 100;

// ================= SEMÁFORO =================
float errorAbsoluto = 0.0;
float errorPorcentaje = 0.0;
String semaforoActual = "ROJO";

// ================= ANTI-WINDUP =================
const float INTEGRAL_LIMITE = 800.0;     // ⭐ Aumentado de 500
unsigned long tiempoEstable = 0;
bool primeraVez = true;

// ================= FUNCIÓN PARA LEER ENCODER =================
inline byte leerEncoder() {
  byte a = digitalRead(ENC_A);
  byte b = digitalRead(ENC_B);
  return (a << 1) | b;
}

// ================= INTERRUPCIÓN =================
void encoderISR() {
  byte estadoActual = leerEncoder();
  byte indice = (estadoAnterior << 2) | estadoActual;
  pulsos += tablaCuadratura[indice];
  estadoAnterior = estadoActual;
}

// ================= FUNCIONES AUXILIARES =================
float absoluto(float valor) {
  return (valor < 0) ? -valor : valor;
}

void apagarLeds() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);
}

void actualizarLedsFisicos() {
  apagarLeds();

  if (semaforoActual == "VERDE") {
    digitalWrite(LED_VERDE, HIGH);
  }
  else if (semaforoActual == "AMARILLO") {
    digitalWrite(LED_AMARILLO, HIGH);
  }
  else {
    digitalWrite(LED_ROJO, HIGH);
  }
}

void actualizarSemaforo(float rpm) {
  errorRPM = referenciaRPM - rpm;
  errorAbsoluto = absoluto(errorRPM);

  if (referenciaRPM > 0) {
    errorPorcentaje = (errorAbsoluto / referenciaRPM) * 100.0;
  } else if (referenciaRPM < 0) {
    errorPorcentaje = (errorAbsoluto / absoluto(referenciaRPM)) * 100.0;
  } else {
    errorPorcentaje = (rpm == 0) ? 0.0 : 100.0;
  }

  if (errorPorcentaje <= 5.0) {
    semaforoActual = "VERDE";
  }
  else if (errorPorcentaje <= 15.0) {
    semaforoActual = "AMARILLO";
  }
  else {
    semaforoActual = "ROJO";
  }

  actualizarLedsFisicos();
}

// ================= APLICAR PWM =================
void aplicarPWM() {
  if (estadoMotor == "PARO" || referenciaRPM == 0) {
    pwmAplicado = 0;
    analogWrite(ENA, 0);
    return;
  }

  if (primeraVez && referenciaRPM != 0) {
    pwmControl = PWM_ARRANQUE;
    primeraVez = false;
  }

  // Solo aplicar umbral mínimo si la referencia es significativa
  if (pwmControl > 0 && pwmControl < PWM_UMBRAL_ARRANQUE && absoluto(referenciaRPM) > 15) {
    pwmControl = PWM_UMBRAL_ARRANQUE;
  }

  pwmControl = constrain(pwmControl, PWM_MIN_MOVIMIENTO, PWM_MAX);
  pwmAplicado = (int)pwmControl;

  analogWrite(ENA, pwmAplicado);
}

void reiniciarPID() {
  noInterrupts();
  pulsosAnterioresPID = pulsos;
  interrupts();

  tiempoAnteriorPID = millis();

  rpmActual = 0.0;
  rpmFiltrada = 0.0;

  errorRPM = 0.0;
  errorAnterior = 0.0;
  integral = 0.0;
  derivada = 0.0;

  primeraVez = true;

  if (referenciaRPM != 0) {
    pwmControl = PWM_ARRANQUE;
  } else {
    pwmControl = 0;
  }

  aplicarPWM();
}

void actualizarPID() {
  unsigned long ahora = millis();

  if (ahora - tiempoAnteriorPID < INTERVALO_PID) {
    return;
  }

  float dt = (ahora - tiempoAnteriorPID) / 1000.0;

  noInterrupts();
  long pulsosActuales = pulsos;
  interrupts();

  long dp = pulsosActuales - pulsosAnterioresPID;

  pulsosAnterioresPID = pulsosActuales;
  tiempoAnteriorPID = ahora;

  float rpmInstantanea = 0.0;

  if (dt > 0) {
    rpmInstantanea = (dp / PULSOS_POR_VUELTA) * (60.0 / dt);
  }

  if (estadoMotor == "PARO" || referenciaRPM == 0) {
    rpmActual = 0.0;
    rpmFiltrada = 0.0;
    pwmControl = 0;
    pwmAplicado = 0;
    integral = 0.0;
    errorAnterior = 0.0;

    analogWrite(ENA, 0);
    actualizarSemaforo(0.0);
    return;
  }

  // Filtro para RPM
  rpmFiltrada = (0.5 * rpmFiltrada) + (0.5 * rpmInstantanea);
  rpmActual = rpmFiltrada;

  errorRPM = referenciaRPM - rpmActual;

  // Anti-windup con límite aumentado
  integral += errorRPM * dt;
  integral = constrain(integral, -INTEGRAL_LIMITE, INTEGRAL_LIMITE);

  if (dt > 0) {
    derivada = (errorRPM - errorAnterior) / dt;
    derivada = constrain(derivada, -150, 150);
  }

  float salidaPID = (KP * errorRPM) + (KI * integral) + (KD * derivada);

  pwmControl += salidaPID;
  pwmControl = constrain(pwmControl, PWM_MIN_MOVIMIENTO, PWM_MAX);

  errorAnterior = errorRPM;

  aplicarPWM();
  actualizarSemaforo(rpmActual);

  // Ajuste fino cuando está estable
  if (absoluto(errorRPM) < 3.0 && referenciaRPM != 0) {
    if (tiempoEstable == 0) {
      tiempoEstable = ahora;
    } else if (ahora - tiempoEstable > 3000) {
      if (absoluto(errorRPM) > 1.0) {
        integral += errorRPM * 0.05;
        integral = constrain(integral, -INTEGRAL_LIMITE, INTEGRAL_LIMITE);
      }
    }
  } else {
    tiempoEstable = 0;
  }
}

// ================= CONTROL MOTOR =================
void derecha() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  estadoMotor = "DERECHA";
  reiniciarPID();
}

void izquierda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  estadoMotor = "IZQUIERDA";
  reiniciarPID();
}

void paro() {
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  estadoMotor = "PARO";
  rpmActual = 0.0;
  rpmFiltrada = 0.0;
  pwmControl = 0;
  pwmAplicado = 0;
  integral = 0.0;
  errorAnterior = 0.0;
  primeraVez = true;
  tiempoEstable = 0;
  actualizarSemaforo(0.0);
}

void resetConteo() {
  noInterrupts();
  pulsos = 0;
  interrupts();
  pulsosAnterioresPID = 0;
  tiempoAnteriorPID = millis();
  rpmActual = 0.0;
  rpmFiltrada = 0.0;
  integral = 0.0;
  errorAnterior = 0.0;
}

// ================= COMUNICACIÓN =================
void enviarEstado() {
  noInterrupts();
  long p = pulsos;
  interrupts();

  float vueltas = p / PULSOS_POR_VUELTA;

  actualizarSemaforo(rpmActual);

  Serial.print("OK:STATUS");
  Serial.print("|estado=");
  Serial.print(estadoMotor);
  Serial.print("|pulsos=");
  Serial.print(p);
  Serial.print("|vueltas=");
  Serial.print(vueltas, 2);
  Serial.print("|rpm=");
  Serial.print(rpmActual, 1);
  Serial.print("|referencia=");
  Serial.print(referenciaRPM, 1);
  Serial.print("|error=");
  Serial.print(errorRPM, 1);
  Serial.print("|errorabs=");
  Serial.print(errorAbsoluto, 1);
  Serial.print("|errorpct=");
  Serial.print(errorPorcentaje, 1);
  Serial.print("|pwm=");
  Serial.print(pwmAplicado);
  Serial.print("|integral=");
  Serial.print(integral, 1);
  Serial.print("|semaforo=");
  Serial.println(semaforoActual);
}

void cambiarReferencia(String cmd) {
  int separador = cmd.indexOf(':');

  if (separador < 0) {
    separador = cmd.indexOf('=');
  }

  if (separador < 0) {
    Serial.println("ERR:REF");
    return;
  }

  String valorTexto = cmd.substring(separador + 1);
  valorTexto.trim();

  float nuevaReferencia = valorTexto.toFloat();

  referenciaRPM = nuevaReferencia;

  integral = 0.0;
  errorAnterior = 0.0;
  primeraVez = true;
  tiempoEstable = 0;

  if (estadoMotor != "PARO" && referenciaRPM != 0) {
    pwmControl = PWM_ARRANQUE;
    aplicarPWM();
  }

  actualizarSemaforo(rpmActual);

  Serial.print("OK:REF");
  Serial.print("|referencia=");
  Serial.print(referenciaRPM, 1);
  Serial.print("|semaforo=");
  Serial.println(semaforoActual);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(30);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  paro();

  estadoAnterior = leerEncoder();

  // Configurar interrupciones para pines 2 y 3
  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);

  tiempoAnteriorPID = millis();

  Serial.println("OK:READY");
  Serial.println("=== MOTOR PID CORREGIDO ===");
  Serial.println("Parametros ajustados para 50 RPM");
  Serial.println("");
  Serial.println("Comandos disponibles:");
  Serial.println("  RIGHT o D     → Girar derecha");
  Serial.println("  LEFT o I      → Girar izquierda");
  Serial.println("  STOP o S      → Parar motor");
  Serial.println("  SET_REF:50    → Velocidad 50 RPM");
  Serial.println("  SET_REF:100   → Velocidad 100 RPM");
  Serial.println("  GET_STATUS    → Ver estado completo");
  Serial.println("  RESET o R     → Resetear contador");
}

// ================= LOOP =================
void loop() {
  actualizarPID();

  // Monitoreo en tiempo real (cada 500ms)
  static unsigned long lastMonitor = 0;
  if (millis() - lastMonitor >= 500 && referenciaRPM != 0) {
    lastMonitor = millis();
    
    String icono;
    if (absoluto(errorRPM) < 2) icono = "✅";
    else if (absoluto(errorRPM) < 5) icono = "🟡";
    else icono = "⚡";
    
    Serial.print(icono);
    Serial.print(" ");
    Serial.print(rpmActual, 0);
    Serial.print("/");
    Serial.print(referenciaRPM, 0);
    Serial.print(" RPM | PWM:");
    Serial.print(pwmAplicado);
    Serial.print(" | Error:");
    Serial.print(errorRPM, 0);
    Serial.print(" | Integral:");
    Serial.print(integral, 0);
    Serial.print(" | Pulsos:");
    Serial.println(pulsos);
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "RIGHT" || cmd == "D") {
      derecha();
      Serial.println("OK:RIGHT");
    }
    else if (cmd == "LEFT" || cmd == "I") {
      izquierda();
      Serial.println("OK:LEFT");
    }
    else if (cmd == "STOP" || cmd == "S") {
      paro();
      Serial.println("OK:STOP");
    }
    else if (cmd == "RESET" || cmd == "R") {
      resetConteo();
      Serial.println("OK:RESET");
    }
    else if (cmd == "GET_STATUS") {
      enviarEstado();
    }
    else if (cmd.startsWith("SET_REF")) {
      cambiarReferencia(cmd);
    }
    else {
      Serial.println("ERR:CMD");
    }
  }
}