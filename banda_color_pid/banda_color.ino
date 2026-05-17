#include <avr/interrupt.h>

// ================= PINES MOTOR (Puente H) =================
const int ENA = 9;
const int IN1 = 7;
const int IN2 = 8;

// ================= ENCODER (CUADRATURA COMPLETA) =================
const int ENC_A = 2;
const int ENC_B = 3;

// ================= LEDS FÍSICOS =================
const int LED_VERDE = 4;
const int LED_AMARILLO = 5;
const int LED_ROJO = 6;

// ================= SENSOR DE COLOR TCS3200 =================
const int S0 = 10;
const int S1 = 11;
const int S2 = 12;
const int S3 = 13;
const int SALIDA = A0;  // Salida del sensor

// ================= VARIABLES ENCODER =================
volatile long pulsos = 0;
volatile byte estadoAnterior = 0;
const float PULSOS_POR_VUELTA = 1560.0;

// Tabla de cuadratura
const int8_t tablaCuadratura[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// ================= ESTADO MOTOR =================
String estadoMotor = "PARO";
char comandoActual = 'P';

// ================= RPM Y REFERENCIA =================
float referenciaRPM = 0.0;
float rpmActual = 0.0;
float rpmFiltrada = 0.0;

// ================= PID =================
float errorRPM = 0.0;
float errorAnterior = 0.0;
float integral = 0.0;
float derivada = 0.0;

// Parámetros PID
float KP = 0.20;
float KI = 0.15;
float KD = 0.008;

// ================= PWM =================
float pwmControl = 0.0;
int pwmAplicado = 0;

const int PWM_MAX = 255;
const int PWM_ARRANQUE = 160;
const int PWM_MIN_MOVIMIENTO = 0;

// ================= TIEMPO PID =================
unsigned long tiempoAnteriorPID = 0;
long pulsosAnterioresPID = 0;
const unsigned long INTERVALO_PID = 100;

// ================= SEMÁFORO =================
float errorAbsoluto = 0.0;
float errorPorcentaje = 0.0;
String semaforoActual = "ROJO";

// ================= ANTI-WINDUP =================
const float INTEGRAL_LIMITE = 800.0;
unsigned long tiempoEstable = 0;
bool primeraVez = true;

// ================= SENSOR DE COLOR =================
long ultimoRojo = 0, ultimoVerde = 0, ultimoAzul = 0;
String ultimoColor = "DESCONOCIDO";
unsigned long tiempoUltimaLectura = 0;
const unsigned long INTERVALO_COLOR = 500;

// ================= CONTADORES DE PIEZAS =================
int piezasBuenas = 0;
int piezasMalas = 0;
bool piezaEnProceso = false;
unsigned long tiempoInicioPieza = 0;

// ================= FUNCIONES ENCODER =================
inline byte leerEncoder() {
  byte a = digitalRead(ENC_A);
  byte b = digitalRead(ENC_B);
  return (a << 1) | b;
}

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
  if (semaforoActual == "VERDE") digitalWrite(LED_VERDE, HIGH);
  else if (semaforoActual == "AMARILLO") digitalWrite(LED_AMARILLO, HIGH);
  else digitalWrite(LED_ROJO, HIGH);
}

void actualizarSemaforo(float rpm) {
  errorRPM = referenciaRPM - rpm;
  errorAbsoluto = absoluto(errorRPM);

  if (referenciaRPM > 0) {
    errorPorcentaje = (errorAbsoluto / referenciaRPM) * 100.0;
  } else {
    errorPorcentaje = (rpm == 0) ? 0.0 : 100.0;
  }

  if (errorPorcentaje <= 5.0) semaforoActual = "VERDE";
  else if (errorPorcentaje <= 15.0) semaforoActual = "AMARILLO";
  else semaforoActual = "ROJO";

  actualizarLedsFisicos();
}

// ================= FUNCIONES SENSOR DE COLOR =================
void iniciarSensorColor() {
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SALIDA, INPUT);
  
  // Escala 20% (reduce frecuencia para mejor lectura)
  digitalWrite(S0, LOW);
  digitalWrite(S1, HIGH);
}

long obtenerFrecuencia() {
  long duracion = pulseIn(SALIDA, HIGH, 100000);
  if (duracion > 0) {
    return 1000000 / (2 * duracion);
  }
  return 0;
}

long leerRojo() {
  digitalWrite(S2, LOW);
  digitalWrite(S3, LOW);
  delay(30);
  return obtenerFrecuencia();
}

long leerVerde() {
  digitalWrite(S2, HIGH);
  digitalWrite(S3, HIGH);
  delay(30);
  return obtenerFrecuencia();
}

long leerAzul() {
  digitalWrite(S2, LOW);
  digitalWrite(S3, HIGH);
  delay(30);
  return obtenerFrecuencia();
}

String detectarColor(long r, long g, long b) {
  // Rangos ajustables según calibración
  // VERDE (pieza buena)
  if (r >= 75 && r <= 90 && g >= 55 && g <= 70 && b >= 70 && b <= 88) {
    return "VERDE";
  }
  // LADRILLO (pieza rechazada)
  else if (r >= 80 && r <= 95 && g >= 50 && g <= 65 && b >= 75 && b <= 90) {
    return "LADRILLO";
  }
  return "DESCONOCIDO";
}

void leerSensorColor() {
  unsigned long ahora = millis();
  if (ahora - tiempoUltimaLectura >= INTERVALO_COLOR) {
    tiempoUltimaLectura = ahora;
    
    ultimoRojo = leerRojo();
    ultimoVerde = leerVerde();
    ultimoAzul = leerAzul();
    
    String colorDetectado = detectarColor(ultimoRojo, ultimoVerde, ultimoAzul);
    
    if (colorDetectado != ultimoColor) {
      ultimoColor = colorDetectado;
      
      if (colorDetectado == "VERDE") {
        piezasBuenas++;
        referenciaRPM = 100;  // Velocidad normal para pieza buena
        Serial.print("✅ PIEZA BUENA DETECTADA | Total: ");
        Serial.println(piezasBuenas);
      }
      else if (colorDetectado == "LADRILLO") {
        piezasMalas++;
        referenciaRPM = -80;  // Velocidad reversa para rechazar
        Serial.print("❌ PIEZA RECHAZADA DETECTADA | Total: ");
        Serial.println(piezasMalas);
      }
    }
    
    actualizarSemaforo(rpmActual);
  }
}

// ================= FUNCIONES MOTOR =================
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

  if (pwmControl > 0 && pwmControl < PWM_MIN_MOVIMIENTO && absoluto(referenciaRPM) > 15) {
    pwmControl = PWM_MIN_MOVIMIENTO;
  }

  pwmControl = constrain(pwmControl, 0, PWM_MAX);
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
  pwmControl = (referenciaRPM != 0) ? PWM_ARRANQUE : 0;
  aplicarPWM();
}

void actualizarPID() {
  unsigned long ahora = millis();
  if (ahora - tiempoAnteriorPID < INTERVALO_PID) return;

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
    integral = 0.0;
    analogWrite(ENA, 0);
    actualizarSemaforo(0.0);
    return;
  }

  rpmFiltrada = (0.5 * rpmFiltrada) + (0.5 * rpmInstantanea);
  rpmActual = rpmFiltrada;

  errorRPM = referenciaRPM - rpmActual;
  integral += errorRPM * dt;
  integral = constrain(integral, -INTEGRAL_LIMITE, INTEGRAL_LIMITE);

  if (dt > 0) derivada = (errorRPM - errorAnterior) / dt;

  float salidaPID = (KP * errorRPM) + (KI * integral) + (KD * derivada);
  pwmControl += salidaPID;
  pwmControl = constrain(pwmControl, 0, PWM_MAX);

  errorAnterior = errorRPM;
  aplicarPWM();
  actualizarSemaforo(rpmActual);
}

// ================= CONTROL MOTOR =================
void derecha() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  estadoMotor = "DERECHA";
  comandoActual = 'A';
  reiniciarPID();
}

void izquierda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  estadoMotor = "IZQUIERDA";
  comandoActual = 'R';
  reiniciarPID();
}

void paro() {
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  estadoMotor = "PARO";
  comandoActual = 'P';
  rpmActual = 0.0;
  rpmFiltrada = 0.0;
  pwmControl = 0;
  integral = 0.0;
  errorAnterior = 0.0;
  primeraVez = true;
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

void resetContadoresPiezas() {
  piezasBuenas = 0;
  piezasMalas = 0;
  Serial.println("📊 Contadores de piezas reiniciados");
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
  Serial.print(semaforoActual);
  Serial.print("|color=");
  Serial.print(ultimoColor);
  Serial.print("|rojo=");
  Serial.print(ultimoRojo);
  Serial.print("|verde=");
  Serial.print(ultimoVerde);
  Serial.print("|azul=");
  Serial.print(ultimoAzul);
  Serial.print("|buenas=");
  Serial.print(piezasBuenas);
  Serial.print("|malas=");
  Serial.println(piezasMalas);
}

void cambiarReferencia(String cmd) {
  int separador = cmd.indexOf(':');
  if (separador < 0) separador = cmd.indexOf('=');
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

  if (estadoMotor != "PARO" && referenciaRPM != 0) {
    pwmControl = PWM_ARRANQUE;
    aplicarPWM();
  }

  actualizarSemaforo(rpmActual);
  Serial.print("OK:REF|referencia=");
  Serial.print(referenciaRPM, 1);
  Serial.println("");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(30);

  // Configurar motor
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  // Configurar encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  
  // Configurar LEDs
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  // Configurar sensor de color
  iniciarSensorColor();

  paro();
  estadoAnterior = leerEncoder();

  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);

  tiempoAnteriorPID = millis();

  Serial.println("OK:READY");
  Serial.println("=== BANDA TRANSPORTADORA CON CONTROL POR COLOR ===");
  Serial.println("PID activo | Sensor TCS3200");
  Serial.println("");
  Serial.println("Comandos disponibles:");
  Serial.println("  RIGHT o D     → Girar derecha");
  Serial.println("  LEFT o I      → Girar izquierda");
  Serial.println("  STOP o S      → Parar motor");
  Serial.println("  SET_REF:100   → Velocidad 100 RPM");
  Serial.println("  GET_STATUS    → Ver estado completo");
  Serial.println("  RESET         → Resetear contador de pulsos");
  Serial.println("  RESET_COUNTERS→ Resetear contadores de piezas");
}

// ================= LOOP =================
void loop() {
  actualizarPID();
  leerSensorColor();

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
    else if (cmd == "RESET") {
      resetConteo();
      Serial.println("OK:RESET");
    }
    else if (cmd == "RESET_COUNTERS") {
      resetContadoresPiezas();
      Serial.println("OK:RESET_COUNTERS");
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