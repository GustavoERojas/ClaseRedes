#include <avr/interrupt.h>

// ================= PINES =================
const int ENA = 9;
const int IN1 = 7;
const int IN2 = 8;
const int ENC_A = 2;
const int ENC_B = 3;
const int S0 = 10;
const int S1 = 11;
const int S2 = 12;
const int S3 = 13;
const int SALIDA = A0;

// ================= ENCODER =================
volatile long pulsos = 0;
volatile byte estadoAnterior = 0;
const float PULSOS_POR_VUELTA = 1560.0;

const int8_t tablaCuadratura[16] = {
  0, 1, -1, 0,
  -1, 0, 0, 1,
  1, 0, 0, -1,
  0, -1, 1, 0
};

// ================= COLOR =================
const int VERDE_R_MIN = 140, VERDE_R_MAX = 195;
const int VERDE_G_MIN = 150, VERDE_G_MAX = 200;
const int VERDE_B_MIN = 160, VERDE_B_MAX = 210;

const int LADRILLO_R_MIN = 130, LADRILLO_R_MAX = 185;
const int LADRILLO_G_MIN = 80, LADRILLO_G_MAX = 140;
const int LADRILLO_B_MIN = 100, LADRILLO_B_MAX = 160;

// ================= VARIABLES PID =================
float referenciaRPM = 35.0;
float rpmActual = 0.0;
float rpmFiltrada = 0.0;
float errorRPM = 0.0;
float errorAnterior = 0.0;
float integral = 0.0;
float derivada = 0.0;
float pwmControl = 0.0;
int pwmAplicado = 0;

const float KP = 0.08;
const float KI = 0.02;
const float KD = 0.002;

const int PWM_MAX = 255;
const int PWM_MIN = 35;
const int PWM_ARRANQUE = 50;

// ================= TIEMPOS =================
unsigned long tiempoAnteriorPID = 0;
long pulsosAnterioresPID = 0;
const unsigned long INTERVALO_PID = 100;

// ================= CONTROL DE BANDA =================
unsigned long tiempoFinEvento = 0;
bool enEvento = false;
byte tipoEvento = 0;
float velocidadBase = 35.0;
const int TIEMPO_EVENTO = 1000;

// ================= CONTADORES =================
int piezasBuenas = 0;
int piezasMalas = 0;
byte ultimoColor = 0;
bool emergencia = false;

// ================= PROTOTIPOS =================
inline byte leerEncoder();
void encoderISR();
void setVelocidad(float rpm);
void asegurarDireccionAdelante();
void asegurarDireccionReversa();
void motor_stop_emergencia();
void motor_start();
void aplicarPWM();
void reiniciarPID();
void actualizarPID();
long leerRojo();
long leerVerde();
long leerAzul();
byte detectarColor(long r, long g, long b);
void procesarPieza(byte color);
void actualizarEvento();
void enviarStatus();

// ================= ENCODER =================
inline byte leerEncoder() {
  return ((digitalRead(ENC_A) << 1) | digitalRead(ENC_B));
}

void encoderISR() {
  byte estadoActual = leerEncoder();
  byte indice = (estadoAnterior << 2) | estadoActual;
  pulsos += tablaCuadratura[indice];
  estadoAnterior = estadoActual;
}

// ================= MOTOR =================
void setVelocidad(float rpm) {
  if (emergencia) return;
  referenciaRPM = rpm;
  reiniciarPID();
}

void asegurarDireccionAdelante() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void asegurarDireccionReversa() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
}

void motor_stop_emergencia() {
  emergencia = true;
  referenciaRPM = 0;
  analogWrite(ENA, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void motor_start() {
  emergencia = false;
  enEvento = false;
  tipoEvento = 0;
  asegurarDireccionAdelante();
  setVelocidad(velocidadBase);
}

void aplicarPWM() {
  if (emergencia || referenciaRPM == 0) {
    analogWrite(ENA, 0);
    return;
  }
  pwmControl = constrain(pwmControl, PWM_MIN, PWM_MAX);
  pwmAplicado = (int)pwmControl;
  analogWrite(ENA, pwmAplicado);
}

void reiniciarPID() {
  noInterrupts();
  pulsosAnterioresPID = pulsos;
  interrupts();
  tiempoAnteriorPID = millis();
  rpmActual = 0;
  rpmFiltrada = 0;
  errorRPM = 0;
  errorAnterior = 0;
  integral = 0;
  pwmControl = (referenciaRPM != 0 && !emergencia) ? PWM_ARRANQUE : 0;
}

void actualizarPID() {
  if (emergencia || referenciaRPM == 0) return;
  
  unsigned long ahora = millis();
  if (ahora - tiempoAnteriorPID < INTERVALO_PID) return;

  float dt = (ahora - tiempoAnteriorPID) / 1000.0;

  noInterrupts();
  long pulsosActuales = pulsos;
  interrupts();

  long dp = pulsosActuales - pulsosAnterioresPID;
  pulsosAnterioresPID = pulsosActuales;
  tiempoAnteriorPID = ahora;

  float rpmInstantanea = 0;
  if (dt > 0) {
    rpmInstantanea = (dp / PULSOS_POR_VUELTA) * (60.0 / dt);
  }

  rpmFiltrada = (0.9 * rpmFiltrada) + (0.1 * rpmInstantanea);
  rpmActual = (rpmFiltrada > 0) ? rpmFiltrada : -rpmFiltrada;

  errorRPM = referenciaRPM - rpmActual;
  
  if (abs(errorRPM) < 10) {
    integral += errorRPM * dt;
  }
  integral = constrain(integral, -100, 100);
  
  derivada = (errorRPM - errorAnterior) / dt;
  derivada = constrain(derivada, -30, 30);

  float salidaPID = (KP * errorRPM) + (KI * integral) + (KD * derivada);
  pwmControl += salidaPID;
  pwmControl = constrain(pwmControl, PWM_MIN, PWM_MAX);
  
  errorAnterior = errorRPM;
  aplicarPWM();
}

// ================= SENSOR DE COLOR =================
long leerRojo() {
  digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  delayMicroseconds(50);
  long d = pulseIn(SALIDA, HIGH, 15000);
  return (d > 0) ? (1000000L / (2 * d)) : 0;
}

long leerVerde() {
  digitalWrite(S2, HIGH); digitalWrite(S3, HIGH);
  delayMicroseconds(50);
  long d = pulseIn(SALIDA, HIGH, 15000);
  return (d > 0) ? (1000000L / (2 * d)) : 0;
}

long leerAzul() {
  digitalWrite(S2, LOW); digitalWrite(S3, HIGH);
  delayMicroseconds(50);
  long d = pulseIn(SALIDA, HIGH, 15000);
  return (d > 0) ? (1000000L / (2 * d)) : 0;
}

byte detectarColor(long r, long g, long b) {
  if (r >= VERDE_R_MIN && r <= VERDE_R_MAX &&
      g >= VERDE_G_MIN && g <= VERDE_G_MAX &&
      b >= VERDE_B_MIN && b <= VERDE_B_MAX) {
    return 1;
  }
  if (r >= LADRILLO_R_MIN && r <= LADRILLO_R_MAX &&
      g >= LADRILLO_G_MIN && g <= LADRILLO_G_MAX &&
      b >= LADRILLO_B_MIN && b <= LADRILLO_B_MAX) {
    return 2;
  }
  return 0;
}

void procesarPieza(byte color) {
  if (emergencia) return;
  
  if (color == 1) {
    piezasBuenas++;
    ultimoColor = 1;
    tipoEvento = 1;
    enEvento = true;
    tiempoFinEvento = millis() + TIEMPO_EVENTO;
    asegurarDireccionAdelante();
    setVelocidad(90);
    Serial.print(F("VERDE:"));
    Serial.println(piezasBuenas);
  } 
  else if (color == 2) {
    piezasMalas++;
    ultimoColor = 2;
    tipoEvento = 2;
    enEvento = true;
    tiempoFinEvento = millis() + TIEMPO_EVENTO;
    asegurarDireccionReversa();
    setVelocidad(75);
    Serial.print(F("LADRILLO:"));
    Serial.println(piezasMalas);
  }
}

void actualizarEvento() {
  if (!enEvento) return;
  
  if (millis() >= tiempoFinEvento) {
    enEvento = false;
    tipoEvento = 0;
    asegurarDireccionAdelante();
    setVelocidad(velocidadBase);
    Serial.println(F("RETORNO_BASE"));
  }
}

void enviarStatus() {
  long p = (pulsos > 0) ? pulsos : -pulsos;
  String colorStr = (ultimoColor == 1) ? "VERDE" : ((ultimoColor == 2) ? "LADRILLO" : "DESCONOCIDO");
  
  float rpmMostrar = (referenciaRPM == 0) ? 0.0 : rpmActual;
  float errorMostrar = (referenciaRPM == 0) ? 0.0 : errorRPM;
  float errorPctMostrar = 0;
  if (referenciaRPM != 0) errorPctMostrar = (errorRPM / referenciaRPM * 100);
  if (errorPctMostrar < 0) errorPctMostrar = -errorPctMostrar;
  
  // Determinar estado para el video
  String estadoStr;
  if (emergencia) {
    estadoStr = "PARO";
  } 
  else if (enEvento) {
    if (tipoEvento == 1) {
      estadoStr = "DERECHA";
    } else if (tipoEvento == 2) {
      estadoStr = "IZQUIERDA";
    } else {
      estadoStr = "DERECHA";
    }
  } 
  else {
    estadoStr = "DERECHA";
  }
  
  Serial.print(F("OK:STATUS|estado="));
  Serial.print(estadoStr);
  
  Serial.print(F("|pulsos=")); Serial.print(p);
  Serial.print(F("|vueltas=")); Serial.print((float)p / PULSOS_POR_VUELTA, 2);
  Serial.print(F("|rpm=")); Serial.print(rpmMostrar, 1);
  Serial.print(F("|referencia=")); Serial.print(referenciaRPM, 1);
  Serial.print(F("|error=")); Serial.print(errorMostrar, 1);
  Serial.print(F("|errorpct=")); Serial.print(errorPctMostrar, 1);
  Serial.print(F("|pwm=")); Serial.print(pwmAplicado);
  Serial.print(F("|semaforo=VERDE|color=")); Serial.print(colorStr);
  Serial.print(F("|buenas=")); Serial.print(piezasBuenas);
  Serial.print(F("|malas=")); Serial.println(piezasMalas);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SALIDA, INPUT);
  
  digitalWrite(S0, LOW);
  digitalWrite(S1, HIGH);
  
  estadoAnterior = leerEncoder();
  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);
  
  // Iniciar banda
  emergencia = false;
  enEvento = false;
  tipoEvento = 0;
  asegurarDireccionAdelante();
  setVelocidad(velocidadBase);
  
  Serial.println(F("OK:READY"));
  Serial.println(F("Sistema iniciado - Banda a 35 RPM"));
}

// ================= LOOP =================
void loop() {
  actualizarPID();
  actualizarEvento();
  
  static unsigned long lastSensor = 0;
  unsigned long ahora = millis();
  
  if (ahora - lastSensor >= 150 && !emergencia) {
    lastSensor = ahora;
    long r = leerRojo();
    long g = leerVerde();
    long b = leerAzul();
    byte color = detectarColor(r, g, b);
    if (color != 0 && !enEvento) {
      procesarPieza(color);
    }
  }
  
  static unsigned long lastStatus = 0;
  if (ahora - lastStatus >= 500) {
    lastStatus = ahora;
    enviarStatus();
  }
  
  // ===== COMANDOS SERIALES =====
  if (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == 'R') {           // START - Reiniciar motor
      motor_start();
      Serial.println(F("OK:START"));
    }
    else if (c == 'S') {      // STOP - Paro emergencia
      motor_stop_emergencia();
      Serial.println(F("OK:STOP"));
    }
    else if (c == 'C') {      // RESET - Reset contadores
      piezasBuenas = 0;
      piezasMalas = 0;
      ultimoColor = 0;
      motor_start();
      Serial.println(F("OK:RESET"));
    }
    else if (c == 'G') {      // GET_STATUS
      enviarStatus();
    }
  }
}
