/*
 * ====================================================================
 * SISTEMA COMPLETO - BANDA + DISTRIBUIDOR (VERSIÓN FINAL)
 * ====================================================================
 */

#include <Wire.h>
#include <Servo.h>

// ====================================================================
// PINES DE LA BANDA TRANSPORTADORA (SIN CAMBIOS)
// ====================================================================

#define PIN_MOTOR_ENA  11
#define PIN_MOTOR_IN1  8
#define PIN_MOTOR_IN2  7
#define PIN_ENCODER_A  2
#define PIN_ENCODER_B  3
#define PIN_TRIG_VERDE 4
#define PIN_ECHO_VERDE 5
#define PIN_TRIG_ROJO  6
#define PIN_ECHO_ROJO  12
#define PIN_SERVO_VERDE 9
#define PIN_SERVO_ROJO  10
#define TCS34725_ADDRESS 0x29

// ====================================================================
// PINES DEL DISTRIBUIDOR (NUEVOS - SIN CONFLICTOS)
// ====================================================================

#define PIN_TRIG_DISTRIBUIDOR  22
#define PIN_ECHO_DISTRIBUIDOR  24
#define PIN_SERVO_DISTRIBUIDOR 26

// ====================================================================
// PUERTOS SERIALES
// ====================================================================

#define SERIAL_BAUDRATE 115200
#define SERIAL_LEARM_BAUDRATE 9600

// ====================================================================
// PARÁMETROS DEL MOTOR Y PI
// ====================================================================

#define PULSOS_POR_VUELTA 865.0

#define KP 3.5
#define KI 0.5
#define PID_SAMPLE_TIME 40
#define RPM_SETPOINT 55.0

#define PWM_MINIMO 100
#define PWM_MAXIMO 255
#define PWM_ARRANQUE 200
#define TIEMPO_ARRANQUE 1500

#define PWM_GOLPE_INICIAL 255
#define TIEMPO_GOLPE_INICIAL 300

// ====================================================================
// PARÁMETROS DEL DISTRIBUIDOR - OPTIMIZADOS
// ====================================================================

#define DISTANCIA_LIMITE 5.8
#define SERVO_GIRAR 125
#define SERVO_DETENER 90
#define TIEMPO_CONFIRMACION 800      // REDUCIDO de 2000 a 800ms
#define TIEMPO_ENTRE_DETECCIONES 2000

// FILTRO DE CONFIRMACIÓN
#define LECTURAS_CONFIRMACION 2      // REDUCIDO de 3 a 2 para más rapidez
#define TIEMPO_ENTRE_LECTURAS 30     // REDUCIDO de 50 a 30ms

// NUEVO: Tiempo para considerar que la pieza ya no está
#define TIEMPO_AUSENCIA_PIEZA 500    // 500ms sin detección = pieza retirada

// ====================================================================
// PARÁMETROS DEL SENSOR DE COLOR
// ====================================================================

#define UMBRAL_COLOR 20.0
#define TIEMPO_LECTURA_COLOR 100
#define TIEMPO_ENTRE_DETECCIONES 1500
#define FIFO_MAX_PIEZAS 50

#define TIEMPO_INTEGRACION_TCS 0xEB
#define LECTURAS_PROMEDIO 2
#define CLEAR_MINIMO 80
#define CLEAR_MAXIMO 8000
#define VARIACION_MAXIMA 0.20
#define TIEMPO_CALIBRACION_FONDO 5000

// ====================================================================
// SERVOS DE LA BANDA
// ====================================================================

#define SERVO_STOP 1500
#define SERVO_ADELANTE 1200
#define SERVO_ATRAS 1800
#define SERVO_TIEMPO_AVANCE 560
#define SERVO_TIEMPO_RETROCESO 560

#define TIEMPO_RETRASO_ROJO 1000
#define TIEMPO_RETRASO_VERDE 940

// ====================================================================
// ULTRASONIDOS DE LA BANDA
// ====================================================================

#define DISTANCIA_DETECCION 5.0
#define DISTANCIA_MINIMA 2.0

// ====================================================================
// ENUMERACIONES
// ====================================================================

enum Color { COLOR_ROJO, COLOR_VERDE, COLOR_AZUL, COLOR_NINGUNO };
enum EstadoPieza { ESTADO_ESPERANDO, ESTADO_EN_BANDA, ESTADO_EN_ESTACION, ESTADO_CLASIFICADO };
enum EstadoSistema { SISTEMA_DETENIDO, SISTEMA_PRODUCIENDO, SISTEMA_PAUSADO };
enum EstadoServo { SERVO_REPOSO, SERVO_MOVIENDO_ADELANTE, SERVO_REGRESANDO };

// ====================================================================
// ESTRUCTURAS
// ====================================================================

struct Pieza {
  uint16_t id;
  Color color;
  EstadoPieza estado;
  uint32_t tiempoLectura;
  bool activa;
  bool servoActivado;
  bool pasoRegistrado;
};

struct Motor {
  int velocidad;
  volatile long pulsos;
  volatile int direccion;
  unsigned long ultimoTiempo;
  float rpmActual;
  float rpmFiltrada;
  bool habilitado;
};

struct ServoControl {
  Servo servo;
  EstadoServo estado;
  unsigned long tiempoInicio;
  unsigned long tiempoDeteccion;
  bool activo;
  int direccion;
  uint16_t piezaId;
  bool esperandoActivacion;
  bool piezaLista;
  int tiempoRetraso;
};

struct FIFO {
  Pieza buffer[FIFO_MAX_PIEZAS];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
  uint16_t nextId;
};

struct ColorCalibracion {
  float r, g, b;
};

// ====================================================================
// CALIBRACIÓN DE COLORES
// ====================================================================

ColorCalibracion calibracion[] = {
  {190.0/255.0, 45.0/255.0, 42.0/255.0},
  {93.0/255.0, 113.0/255.0, 68.0/255.0},
  {97.0/255.0, 87.0/255.0, 93.0/255.0},
  {184.0/255.0, 138.0/255.0, 133.0/255.0}
};

// ====================================================================
// VARIABLES GLOBALES
// ====================================================================

// Banda transportadora
FIFO fifo;
ServoControl servoVerde;
ServoControl servoRojo;
Motor motor;

unsigned long ultimoTiempoColor = 0;
unsigned long ultimoTiempoSerial = 0;
unsigned long ultimoTiempoServos = 0;
unsigned long ultimoTiempoDeteccion = 0;
unsigned long ultimoTiempoPID = 0;

EstadoSistema estadoSistema = SISTEMA_DETENIDO;
uint16_t totalPiezasDetectadas = 0;
uint16_t totalPiezasClasificadas = 0;
uint16_t totalPiezasAzules = 0;

bool ultrasonidoVerdeActivo = false;
bool ultrasonidoRojoActivo = false;

bool arrancando = true;
unsigned long tiempoArranque = 0;
bool golpeInicialAplicado = false;
unsigned long tiempoGolpe = 0;

// ====================================================================
// VARIABLES DEL DISTRIBUIDOR - OPTIMIZADAS
// ====================================================================

Servo servoTarro;
bool piezaDetectada = false;
bool grupoEjecutado = false;
unsigned long tiempoDeteccion = 0;
unsigned long tiempoUltimaDeteccion = 0;
unsigned long tiempoUltimoConteo = 0;  // Para saber cuándo la pieza se fue

// Variables para filtro de confirmación
int contadorConfirmacion = 0;
unsigned long tiempoUltimaLectura = 0;
float ultimaDistanciaValida = 0;

int ultimaPosicionServo = -1;

// Grupo LeArm
byte grupo1[] = {
  0x55, 0x55, 0x05, 0x06, 0x01, 0x01, 0x00
};

// Calibración fondo
ColorCalibracion calibracionFondo = {0, 0, 0};
bool calibracionFondoInicializada = false;
unsigned long ultimoTiempoCalibracion = 0;

// ====================================================================
// PROTOTIPOS
// ====================================================================

void setup();
void loop();
void procesarBanda();
void procesarDistribuidor();

// Banda
void iniciarHardware();
void inicializarSensores();
void inicializarFIFO();
void inicializarMotor();
void controlarMotor(int velocidad);
void leerEncoderISR();
float calcularRPM();
float calcularPI(float setpoint, float entrada);
void leerYProcesarColor();
void verificarEstaciones();
void limpiarPiezasAntiguas();
void inicializarServos();
void activarServo(ServoControl* servo, int direccion, uint16_t piezaId);
void actualizarServos();
float leerDistancia(int trigPin, int echoPin);
void crearPieza(Color color);

// Color
bool leerColorTCS34725(uint16_t* clear, uint16_t* red, uint16_t* green, uint16_t* blue);
bool leerColorTCS34725Mejorado(uint16_t* clear, uint16_t* red, uint16_t* green, uint16_t* blue);
void normalizarRGB(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue, float* r, float* g, float* b);
float distanciaEuclidiana(float r1, float g1, float b1, float r2, float g2, float b2);
void actualizarCalibracionFondo(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue);
Color clasificarColorMejorado(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue);

// FIFO
void fifoInit(FIFO* f);
bool fifoPush(FIFO* f, Pieza p);
bool fifoPop(FIFO* f, Pieza* p);
bool fifoPeek(FIFO* f, Pieza* p);
void fifoPrint(FIFO* f);
bool fifoEstaVacia(FIFO* f);
bool fifoEstaLlena(FIFO* f);

// Comandos
void procesarComandoSerial(char comando);
void mostrarAyuda();
void mostrarEstadisticas();
void mostrarFIFO();
void iniciarProduccion();
void detenerProduccion();
void reanudarSistema();
void resetearSistema();
void pausarSistema();
void crearPiezaManual();
void pruebaServoManual(ServoControl* servo, int direccion, const char* nombre);
void imprimirSeparador();
void detenerTodo();

// Distribuidor
float medirDistanciaDistribuidor();
bool detectarPiezaConfirmada();
bool hayPiezaPresente();
void escribirServo(int angulo);

// ====================================================================
// SETUP
// ====================================================================

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial1.begin(SERIAL_LEARM_BAUDRATE);
  
  imprimirSeparador();
  Serial.println(F("   SISTEMA COMPLETO - BANDA + DISTRIBUIDOR"));
  imprimirSeparador();
  Serial.println(F(""));
  Serial.println(F("📊 PINES DEL SISTEMA:"));
  Serial.println(F(""));
  Serial.println(F("   BANDA TRANSPORTADORA:"));
  Serial.println(F("   - Motor: ENA=11, IN1=8, IN2=7"));
  Serial.println(F("   - Encoder: A=2, B=3"));
  Serial.println(F("   - US Verde: TRIG=4, ECHO=5"));
  Serial.println(F("   - US Rojo: TRIG=6, ECHO=12"));
  Serial.println(F("   - Servo Verde: 9"));
  Serial.println(F("   - Servo Rojo: 10"));
  Serial.println(F("   - Color: I2C (20, 21)"));
  Serial.println(F(""));
  Serial.println(F("   DISTRIBUIDOR:"));
  Serial.println(F("   - US TRIG: 22"));
  Serial.println(F("   - US ECHO: 24"));
  Serial.println(F("   - SERVO: 26"));
  Serial.println(F(""));
  Serial.println(F("📊 PUERTOS SERIALES:"));
  Serial.println(F("   Serial (USB):  115200 baud - Depuración"));
  Serial.println(F("   Serial1 (TX1): 9600 baud   - Distribuidor LeArm"));
  Serial.println(F(""));
  
  Serial.println(F("📊 PARÁMETROS DEL DISTRIBUIDOR:"));
  Serial.print(F("   Servo Girar: "));
  Serial.print(SERVO_GIRAR);
  Serial.println(F("° (buscando piezas)"));
  Serial.print(F("   Servo Detener: "));
  Serial.print(SERVO_DETENER);
  Serial.println(F("° (detenido completamente)"));
  Serial.print(F("   Distancia límite: "));
  Serial.print(DISTANCIA_LIMITE);
  Serial.println(F(" cm"));
  Serial.print(F("   Lecturas confirmación: "));
  Serial.print(LECTURAS_CONFIRMACION);
  Serial.println(F(" (consecutivas)"));
  Serial.print(F("   Tiempo confirmación: "));
  Serial.print(TIEMPO_CONFIRMACION);
  Serial.println(F(" ms"));
  Serial.println(F(""));
  
  Serial.println(F("📊 PARÁMETROS DEL CONTROLADOR:"));
  Serial.print(F("   Setpoint: "));
  Serial.print(RPM_SETPOINT);
  Serial.println(F(" RPM"));
  Serial.print(F("   KP: "));
  Serial.println(KP);
  Serial.print(F("   KI: "));
  Serial.println(KI);
  Serial.println(F(""));
  
  // Inicializar banda
  iniciarHardware();
  inicializarSensores();
  inicializarFIFO();
  inicializarMotor();
  inicializarServos();
  
  // Inicializar distribuidor
  servoTarro.attach(PIN_SERVO_DISTRIBUIDOR);
  escribirServo(SERVO_GIRAR);
  
  pinMode(PIN_TRIG_DISTRIBUIDOR, OUTPUT);
  pinMode(PIN_ECHO_DISTRIBUIDOR, INPUT);
  
  ultimoTiempoColor = millis();
  ultimoTiempoSerial = millis();
  ultimoTiempoServos = millis();
  ultimoTiempoDeteccion = millis();
  ultimoTiempoPID = millis();
  
  Serial.println(F("✅ SISTEMA INICIALIZADO"));
  Serial.println(F(""));
  Serial.println(F("COMANDOS:"));
  Serial.println(F("  O - Iniciar producción"));
  Serial.println(F("  S - Detener TODO (Banda + Brazo + Dispensador)"));
  Serial.println(F("  R - Reanudar"));
  Serial.println(F("  P - Reset"));
  Serial.println(F("  M - Crear pieza manual"));
  Serial.println(F("  C - Mostrar FIFO"));
  Serial.println(F("  E - Estadísticas"));
  Serial.println(F("  H - Ayuda"));
  Serial.println(F("  1 - Servo ROJO ADELANTE"));
  Serial.println(F("  2 - Servo VERDE ADELANTE"));
  Serial.println(F("  3 - Servo ROJO ATRÁS"));
  Serial.println(F("  4 - Servo VERDE ATRÁS"));
  Serial.println(F(""));
  Serial.println(F("Presione O para iniciar"));
  Serial.println(F(""));
}

// ====================================================================
// FUNCIÓN PARA ESCRIBIR EL SERVO SOLO CUANDO CAMBIA
// ====================================================================

void escribirServo(int angulo) {
  if (angulo != ultimaPosicionServo) {
    servoTarro.write(angulo);
    ultimaPosicionServo = angulo;
    delay(15);
  }
}

// ====================================================================
// FUNCIÓN PARA DETECTAR SI HAY PIEZA PRESENTE (SIN CONFIRMACIÓN)
// ====================================================================

bool hayPiezaPresente() {
    float d = medirDistanciaDistribuidor();
    return (d <= DISTANCIA_LIMITE && d > 0);
}

// ====================================================================
// FUNCIÓN DE DETECCIÓN CON CONFIRMACIÓN
// ====================================================================

bool detectarPiezaConfirmada() {
    float d = medirDistanciaDistribuidor();
    
    // Si la distancia es válida y está dentro del límite
    if (d <= DISTANCIA_LIMITE && d > 0) {
        contadorConfirmacion++;
        if (contadorConfirmacion >= LECTURAS_CONFIRMACION) {
            contadorConfirmacion = 0;
            ultimaDistanciaValida = d;
            return true;
        }
    } else {
        contadorConfirmacion = 0;
    }
    
    return false;
}

// ====================================================================
// MEDIR DISTANCIA DISTRIBUIDOR - OPTIMIZADA
// ====================================================================

float medirDistanciaDistribuidor() {
    // Solo 2 lecturas para más rapidez
    float suma = 0;
    int lecturasValidas = 0;
    
    for (int i = 0; i < 2; i++) {
        digitalWrite(PIN_TRIG_DISTRIBUIDOR, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIG_DISTRIBUIDOR, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG_DISTRIBUIDOR, LOW);
        
        long t = pulseIn(PIN_ECHO_DISTRIBUIDOR, HIGH, 30000);
        
        if (t > 0) {
            float distancia = t * 0.0343 / 2.0;
            if (distancia > 0 && distancia < 50) {
                suma += distancia;
                lecturasValidas++;
            }
        }
        delayMicroseconds(50);
    }
    
    if (lecturasValidas == 0) return 999;
    return suma / lecturasValidas;
}

// ====================================================================
// LOOP PRINCIPAL
// ====================================================================

void loop() {
  if (Serial.available() > 0) {
    char comando = Serial.read();
    if (comando != '\n' && comando != '\r') {
      procesarComandoSerial(comando);
    }
  }
  
  procesarBanda();
  procesarDistribuidor();
  
  unsigned long ahora = millis();
  if (ahora - ultimoTiempoServos >= 50) {
    ultimoTiempoServos = ahora;
    actualizarServos();
  }
  
  if (ahora - ultimoTiempoSerial >= 1000) {
    ultimoTiempoSerial = ahora;
    if (estadoSistema == SISTEMA_PRODUCIENDO && motor.habilitado) {
      Serial.print(F("⚡ RPM: "));
      Serial.print(motor.rpmFiltrada, 1);
      Serial.print(F(" | PWM: "));
      Serial.print(motor.velocidad);
      Serial.print(F(" | Error: "));
      Serial.print(RPM_SETPOINT - motor.rpmFiltrada, 1);
      Serial.print(F(" | 📦: "));
      Serial.print(fifo.count);
      Serial.print(F(" | ✅: "));
      Serial.print(totalPiezasClasificadas);
      Serial.print(F(" | 🔵: "));
      Serial.println(totalPiezasAzules);
    } else if (estadoSistema == SISTEMA_DETENIDO) {
      Serial.println(F("⏸️ Sistema DETENIDO"));
    }
  }
}

// ====================================================================
// PROCESAR BANDA TRANSPORTADORA
// ====================================================================

void procesarBanda() {
  unsigned long ahora = millis();
  
  if (estadoSistema != SISTEMA_PRODUCIENDO) return;
  
  if (motor.habilitado) {
    if (arrancando) {
      if (tiempoArranque == 0) {
        tiempoArranque = millis();
        tiempoGolpe = millis();
        golpeInicialAplicado = false;
      }
      
      unsigned long tiempoTranscurrido = millis() - tiempoArranque;
      unsigned long tiempoDesdeGolpe = millis() - tiempoGolpe;
      
      if (tiempoDesdeGolpe < TIEMPO_GOLPE_INICIAL && !golpeInicialAplicado) {
        motor.velocidad = PWM_GOLPE_INICIAL;
        digitalWrite(PIN_MOTOR_IN1, HIGH);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        analogWrite(PIN_MOTOR_ENA, PWM_GOLPE_INICIAL);
        
        if (tiempoDesdeGolpe >= TIEMPO_GOLPE_INICIAL) {
          golpeInicialAplicado = true;
          tiempoGolpe = millis();
        }
      }
      else if (golpeInicialAplicado && tiempoTranscurrido < TIEMPO_ARRANQUE) {
        unsigned long tiempoProgresivo = millis() - tiempoGolpe;
        float progreso = (float)tiempoProgresivo / (TIEMPO_ARRANQUE - TIEMPO_GOLPE_INICIAL);
        progreso = constrain(progreso, 0.0, 1.0);
        
        int pwmProgresivo = PWM_ARRANQUE + (int)(progreso * 50);
        pwmProgresivo = constrain(pwmProgresivo, PWM_MINIMO, PWM_MAXIMO);
        motor.velocidad = pwmProgresivo;
        
        digitalWrite(PIN_MOTOR_IN1, HIGH);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        analogWrite(PIN_MOTOR_ENA, motor.velocidad);
      }
      else {
        arrancando = false;
        tiempoArranque = 0;
        golpeInicialAplicado = false;
      }
    } else {
      if (ahora - ultimoTiempoPID >= PID_SAMPLE_TIME) {
        ultimoTiempoPID = ahora;
        
        float rpmActual = calcularRPM();
        float pwm = calcularPI(RPM_SETPOINT, rpmActual);
        
        if (pwm < PWM_MINIMO) pwm = PWM_MINIMO;
        
        motor.velocidad = (int)pwm;
        
        digitalWrite(PIN_MOTOR_IN1, HIGH);
        digitalWrite(PIN_MOTOR_IN2, LOW);
        analogWrite(PIN_MOTOR_ENA, motor.velocidad);
      }
    }
  }
  
  if (ahora - ultimoTiempoColor >= TIEMPO_LECTURA_COLOR) {
    ultimoTiempoColor = ahora;
    leerYProcesarColor();
  }
  
  verificarEstaciones();
  limpiarPiezasAntiguas();
}

// ====================================================================
// PROCESAR DISTRIBUIDOR - VERSIÓN FINAL OPTIMIZADA
// ====================================================================

void procesarDistribuidor() {
    unsigned long ahora = millis();
    
    // Si el sistema está detenido
    if (estadoSistema != SISTEMA_PRODUCIENDO) {
        escribirServo(SERVO_DETENER);
        piezaDetectada = false;
        grupoEjecutado = false;
        contadorConfirmacion = 0;
        return;
    }
    
    // Control de tiempo de lectura
    if (ahora - tiempoUltimaLectura < TIEMPO_ENTRE_LECTURAS) {
        return;
    }
    tiempoUltimaLectura = ahora;
    
    // Verificar si hay pieza presente (detección rápida)
    bool piezaPresente = hayPiezaPresente();
    
    // Si la pieza estaba presente y ahora ya no está
    if (!piezaPresente && piezaDetectada) {
        // Si ya pasó el tiempo de ausencia, resetear
        if (ahora - tiempoUltimoConteo > TIEMPO_AUSENCIA_PIEZA) {
            piezaDetectada = false;
            grupoEjecutado = false;
            contadorConfirmacion = 0;
            escribirServo(SERVO_GIRAR);
            Serial.println(F("🔄 Distribuidor: Pieza retirada - Buscando"));
        }
    }
    
    // Si hay pieza presente
    if (piezaPresente) {
        tiempoUltimoConteo = ahora;  // Actualizar tiempo de presencia
        
        // Confirmar detección con múltiples lecturas
        if (detectarPiezaConfirmada()) {
            // Detener el servo
            escribirServo(SERVO_DETENER);
            
            // Primera detección confirmada
            if (!piezaDetectada && (ahora - tiempoUltimaDeteccion > TIEMPO_ENTRE_DETECCIONES)) {
                piezaDetectada = true;
                tiempoDeteccion = ahora;
                tiempoUltimaDeteccion = ahora;
                Serial.println(F("📦 Distribuidor: Pieza CONFIRMADA - Servo DETENIDO"));
            }
            
            // Enviar comando al LeArm después del tiempo de confirmación
            if (piezaDetectada && !grupoEjecutado &&
                ahora - tiempoDeteccion >= TIEMPO_CONFIRMACION) {
                
                Serial1.write(grupo1, sizeof(grupo1));
                grupoEjecutado = true;
                Serial.println(F("📦 Distribuidor: Comando enviado al LeArm"));
                delay(50);
            }
        }
    }
}

// ====================================================================
// FUNCIONES DE LA BANDA - INICIALIZACIÓN
// ====================================================================

void iniciarHardware() {
  pinMode(PIN_MOTOR_ENA, OUTPUT);
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  
  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), leerEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), leerEncoderISR, CHANGE);
  
  pinMode(PIN_ECHO_VERDE, INPUT_PULLUP);
  pinMode(PIN_ECHO_ROJO, INPUT_PULLUP);
  pinMode(PIN_TRIG_VERDE, OUTPUT);
  pinMode(PIN_TRIG_ROJO, OUTPUT);
}

void inicializarSensores() {
  Wire.begin();
  
  Wire.beginTransmission(TCS34725_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("⚠️ Sensor TCS34725 no conectado"));
    return;
  }
  
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(0x81);
  Wire.write(TIEMPO_INTEGRACION_TCS);
  Wire.endTransmission();
  
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(0x8F);
  Wire.write(0x01);
  Wire.endTransmission();
  
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(0x80);
  Wire.write(0x03);
  Wire.endTransmission();
}

void inicializarFIFO() {
  fifoInit(&fifo);
}

void inicializarMotor() {
  motor.velocidad = 0;
  motor.pulsos = 0;
  motor.direccion = 0;
  motor.ultimoTiempo = millis();
  motor.rpmActual = 0;
  motor.rpmFiltrada = 0;
  motor.habilitado = false;
  
  arrancando = true;
  tiempoArranque = 0;
  golpeInicialAplicado = false;
  tiempoGolpe = 0;
  
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
  analogWrite(PIN_MOTOR_ENA, 0);
}

void inicializarServos() {
  servoRojo.servo.attach(PIN_SERVO_ROJO);
  servoRojo.estado = SERVO_REPOSO;
  servoRojo.activo = false;
  servoRojo.esperandoActivacion = false;
  servoRojo.piezaLista = false;
  servoRojo.tiempoRetraso = TIEMPO_RETRASO_ROJO;
  
  servoVerde.servo.attach(PIN_SERVO_VERDE);
  servoVerde.estado = SERVO_REPOSO;
  servoVerde.activo = false;
  servoVerde.esperandoActivacion = false;
  servoVerde.piezaLista = false;
  servoVerde.tiempoRetraso = TIEMPO_RETRASO_VERDE;
  
  servoRojo.servo.writeMicroseconds(SERVO_STOP);
  servoVerde.servo.writeMicroseconds(SERVO_STOP);
}

// ====================================================================
// ENCODER Y PI
// ====================================================================

void leerEncoderISR() {
  bool estadoA = digitalRead(PIN_ENCODER_A);
  bool estadoB = digitalRead(PIN_ENCODER_B);
  
  static bool ultimoA = LOW;
  static bool ultimoB = LOW;
  static unsigned long ultimoTiempo = 0;
  
  unsigned long ahora = micros();
  
  if (ahora - ultimoTiempo > 500) {
    if (estadoA != ultimoA || estadoB != ultimoB) {
      if (estadoA == estadoB) {
        motor.direccion = 1;
      } else {
        motor.direccion = -1;
      }
      motor.pulsos++;
      
      ultimoA = estadoA;
      ultimoB = estadoB;
      ultimoTiempo = ahora;
    }
  }
}

float calcularRPM() {
  unsigned long ahora = millis();
  unsigned long deltaT = ahora - motor.ultimoTiempo;
  
  if (deltaT < 100) {
    return motor.rpmFiltrada;
  }
  
  if (motor.pulsos == 0) {
    motor.ultimoTiempo = ahora;
    motor.rpmActual = 0;
    motor.rpmFiltrada = motor.rpmFiltrada * 0.8 + motor.rpmActual * 0.2;
    return motor.rpmFiltrada;
  }
  
  float pulsosPorSegundo = (float)motor.pulsos * 1000.0 / deltaT;
  float rpm = (pulsosPorSegundo * 60.0) / PULSOS_POR_VUELTA;
  
  if (rpm < 0 || rpm > 500) {
    rpm = motor.rpmActual;
  }
  
  motor.pulsos = 0;
  motor.ultimoTiempo = ahora;
  motor.rpmActual = rpm;
  motor.rpmFiltrada = motor.rpmFiltrada * 0.5 + rpm * 0.5;
  
  return motor.rpmFiltrada;
}

float calcularPI(float setpoint, float entrada) {
  static float integral = 0;
  static unsigned long ultimoTiempoPI = 0;
  static float ultimaSalida = PWM_MINIMO;
  
  unsigned long ahora = millis();
  float deltaT = (ahora - ultimoTiempoPI) / 1000.0;
  
  if (ultimoTiempoPI == 0) {
    ultimoTiempoPI = ahora;
    return PWM_MINIMO + 30;
  }
  
  if (deltaT < 0.005) {
    return ultimaSalida;
  }
  
  float error = setpoint - entrada;
  float P = KP * error;
  
  if (abs(error) < 35) {
    integral += error * deltaT;
  } else {
    integral = integral * 0.98;
  }
  integral = constrain(integral, -250, 250);
  float I = KI * integral;
  
  float salida = P + I;
  
  if (entrada < 20) {
    salida = max(salida, PWM_MINIMO + 80);
  } else if (entrada < 35) {
    salida = max(salida, PWM_MINIMO + 60);
  } else if (entrada < 45) {
    salida = max(salida, PWM_MINIMO + 40);
  } else if (entrada > setpoint * 1.1) {
    salida = min(salida, PWM_MAXIMO - 40);
  }
  
  if (error > 10) salida = max(salida, PWM_MINIMO + 60);
  if (error > 20) salida = max(salida, PWM_MINIMO + 80);
  
  float cambio = salida - ultimaSalida;
  if (abs(cambio) > 50) {
    salida = ultimaSalida + (cambio > 0 ? 50 : -50);
  }
  
  salida = constrain(salida, PWM_MINIMO, PWM_MAXIMO);
  
  ultimoTiempoPI = ahora;
  ultimaSalida = salida;
  
  return salida;
}

void controlarMotor(int velocidad) {
  velocidad = constrain(velocidad, -255, 255);
  
  if (velocidad > 0) {
    digitalWrite(PIN_MOTOR_IN1, HIGH);
    digitalWrite(PIN_MOTOR_IN2, LOW);
    analogWrite(PIN_MOTOR_ENA, velocidad);
    motor.habilitado = true;
    motor.velocidad = velocidad;
  } else {
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
    analogWrite(PIN_MOTOR_ENA, 0);
    motor.habilitado = false;
    motor.velocidad = 0;
  }
}

// ====================================================================
// SENSOR DE COLOR (funciones completas)
// ====================================================================

bool leerColorTCS34725(uint16_t* clear, uint16_t* red, uint16_t* green, uint16_t* blue) {
  Wire.beginTransmission(TCS34725_ADDRESS);
  Wire.write(0xB4);
  Wire.endTransmission();
  
  Wire.requestFrom(TCS34725_ADDRESS, 8);
  if (Wire.available() >= 8) {
    *clear = Wire.read() | (Wire.read() << 8);
    *red = Wire.read() | (Wire.read() << 8);
    *green = Wire.read() | (Wire.read() << 8);
    *blue = Wire.read() | (Wire.read() << 8);
    return true;
  }
  return false;
}

bool leerColorTCS34725Mejorado(uint16_t* clear, uint16_t* red, uint16_t* green, uint16_t* blue) {
  uint16_t c[LECTURAS_PROMEDIO], r[LECTURAS_PROMEDIO], g[LECTURAS_PROMEDIO], b[LECTURAS_PROMEDIO];
  
  for (int i = 0; i < LECTURAS_PROMEDIO; i++) {
    if (!leerColorTCS34725(&c[i], &r[i], &g[i], &b[i])) {
      return false;
    }
    delay(5);
  }
  
  uint32_t sumC = 0, sumR = 0, sumG = 0, sumB = 0;
  for (int i = 0; i < LECTURAS_PROMEDIO; i++) {
    sumC += c[i];
    sumR += r[i];
    sumG += g[i];
    sumB += b[i];
  }
  
  *clear = sumC / LECTURAS_PROMEDIO;
  *red = sumR / LECTURAS_PROMEDIO;
  *green = sumG / LECTURAS_PROMEDIO;
  *blue = sumB / LECTURAS_PROMEDIO;
  
  if (*clear < CLEAR_MINIMO || *clear > CLEAR_MAXIMO) {
    return false;
  }
  
  float variacionMax = 0;
  for (int i = 0; i < LECTURAS_PROMEDIO; i++) {
    float diffC = abs((float)c[i] - *clear) / *clear;
    float diffR = abs((float)r[i] - *red) / (*red > 0 ? *red : 1);
    float diffG = abs((float)g[i] - *green) / (*green > 0 ? *green : 1);
    float diffB = abs((float)b[i] - *blue) / (*blue > 0 ? *blue : 1);
    
    float maxDiff = max(max(diffC, diffR), max(diffG, diffB));
    if (maxDiff > variacionMax) variacionMax = maxDiff;
  }
  
  if (variacionMax > VARIACION_MAXIMA) {
    return false;
  }
  
  return true;
}

void normalizarRGB(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue, 
                   float* r, float* g, float* b) {
  if (clear > 0) {
    *r = (float)red / clear;
    *g = (float)green / clear;
    *b = (float)blue / clear;
  } else {
    *r = 0; *g = 0; *b = 0;
  }
}

float distanciaEuclidiana(float r1, float g1, float b1, float r2, float g2, float b2) {
  float dr = r1 - r2;
  float dg = g1 - g2;
  float db = b1 - b2;
  return sqrt(dr*dr + dg*dg + db*db);
}

void actualizarCalibracionFondo(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue) {
  unsigned long ahora = millis();
  
  if (ahora - ultimoTiempoCalibracion < TIEMPO_CALIBRACION_FONDO) {
    return;
  }
  
  if (clear < 300 || clear > 5000) {
    return;
  }
  
  float r, g, b;
  normalizarRGB(clear, red, green, blue, &r, &g, &b);
  
  if (!calibracionFondoInicializada) {
    calibracionFondo.r = r;
    calibracionFondo.g = g;
    calibracionFondo.b = b;
    calibracionFondoInicializada = true;
  } else {
    calibracionFondo.r = calibracionFondo.r * 0.9 + r * 0.1;
    calibracionFondo.g = calibracionFondo.g * 0.9 + g * 0.1;
    calibracionFondo.b = calibracionFondo.b * 0.9 + b * 0.1;
  }
  
  ultimoTiempoCalibracion = ahora;
}

Color clasificarColorMejorado(uint16_t clear, uint16_t red, uint16_t green, uint16_t blue) {
  float r, g, b;
  normalizarRGB(clear, red, green, blue, &r, &g, &b);
  
  if (clear < CLEAR_MINIMO) {
    return COLOR_NINGUNO;
  }
  
  if (calibracionFondoInicializada) {
    float distFondo = distanciaEuclidiana(r, g, b, 
                                          calibracionFondo.r, 
                                          calibracionFondo.g, 
                                          calibracionFondo.b);
    if (distFondo < 0.05) {
      return COLOR_NINGUNO;
    }
  }
  
  float distanciaMin = 999.0;
  Color colorMin = COLOR_NINGUNO;
  
  for (int i = 0; i < 3; i++) {
    float dist = distanciaEuclidiana(r, g, b, 
                                     calibracion[i].r, 
                                     calibracion[i].g, 
                                     calibracion[i].b);
    if (dist < distanciaMin) {
      distanciaMin = dist;
      colorMin = (Color)i;
    }
  }
  
  if (calibracionFondoInicializada) {
    float distFondo = distanciaEuclidiana(r, g, b, 
                                          calibracionFondo.r, 
                                          calibracionFondo.g, 
                                          calibracionFondo.b);
    if (distanciaMin > distFondo) {
      return COLOR_NINGUNO;
    }
  }
  
  if (distanciaMin > UMBRAL_COLOR) {
    return COLOR_NINGUNO;
  }
  
  return colorMin;
}

void leerYProcesarColor() {
  uint16_t clear, red, green, blue;
  
  if (leerColorTCS34725Mejorado(&clear, &red, &green, &blue)) {
    actualizarCalibracionFondo(clear, red, green, blue);
    
    Color color = clasificarColorMejorado(clear, red, green, blue);
    
    if (color != COLOR_NINGUNO && 
        (millis() - ultimoTiempoDeteccion > TIEMPO_ENTRE_DETECCIONES)) {
      
      bool piezaReciente = false;
      for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
        if (fifo.buffer[i].activa && 
            fifo.buffer[i].tiempoLectura > millis() - 500) {
          piezaReciente = true;
          break;
        }
      }
      
      if (!piezaReciente) {
        crearPieza(color);
        totalPiezasDetectadas++;
        ultimoTiempoDeteccion = millis();
        
        ultrasonidoVerdeActivo = false;
        ultrasonidoRojoActivo = false;
        
        switch(color) {
          case COLOR_ROJO:
            ultrasonidoRojoActivo = true;
            Serial.println(F("🔴 ROJO → US ROJO ACTIVADO"));
            break;
          case COLOR_VERDE:
            ultrasonidoVerdeActivo = true;
            Serial.println(F("🟢 VERDE → US VERDE ACTIVADO"));
            break;
          case COLOR_AZUL:
            ultrasonidoRojoActivo = true;
            Serial.println(F("🔵 AZUL → US ROJO ACTIVADO (solo registro)"));
            break;
          default:
            break;
        }
      }
    }
  }
}

// ====================================================================
// GESTIÓN DE PIEZAS
// ====================================================================

void crearPieza(Color color) {
  Pieza nuevaPieza;
  nuevaPieza.id = fifo.nextId++;
  nuevaPieza.color = color;
  nuevaPieza.estado = ESTADO_EN_BANDA;
  nuevaPieza.tiempoLectura = millis();
  nuevaPieza.activa = true;
  nuevaPieza.servoActivado = false;
  nuevaPieza.pasoRegistrado = false;
  
  if (!fifoPush(&fifo, nuevaPieza)) {
    Serial.println(F("⚠️ ERROR: FIFO llena"));
  }
}

void crearPiezaManual() {
  static Color colorPrueba = COLOR_ROJO;
  crearPieza(colorPrueba);
  
  ultrasonidoVerdeActivo = false;
  ultrasonidoRojoActivo = false;
  
  switch(colorPrueba) {
    case COLOR_ROJO:
      ultrasonidoRojoActivo = true;
      Serial.println(F("🔴 ROJO → US ROJO ACTIVADO (forzado)"));
      break;
    case COLOR_VERDE:
      ultrasonidoVerdeActivo = true;
      Serial.println(F("🟢 VERDE → US VERDE ACTIVADO (forzado)"));
      break;
    case COLOR_AZUL:
      ultrasonidoRojoActivo = true;
      Serial.println(F("🔵 AZUL → US ROJO ACTIVADO (solo registro)"));
      break;
    default:
      break;
  }
  
  Serial.print(F("🖐️ Pieza manual: "));
  Serial.println(colorPrueba == COLOR_ROJO ? "ROJO" : 
                colorPrueba == COLOR_VERDE ? "VERDE" : "AZUL");
  colorPrueba = (Color)((colorPrueba + 1) % 3);
}

// ====================================================================
// VERIFICAR ESTACIONES
// ====================================================================

float leerDistancia(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  unsigned long duracion = pulseIn(echoPin, HIGH, 30000);
  
  if (duracion == 0) {
    return -1;
  }
  
  float distancia = duracion * 0.034 / 2;
  return (distancia > 100.0) ? -1 : distancia;
}

void verificarEstaciones() {
  unsigned long ahora = millis();
  
  if (ultrasonidoRojoActivo) {
    float distancia = leerDistancia(PIN_TRIG_ROJO, PIN_ECHO_ROJO);
    
    if (distancia > DISTANCIA_MINIMA && distancia < DISTANCIA_DETECCION) {
      
      Pieza* piezaEncontrada = nullptr;
      
      for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
        if (fifo.buffer[i].activa && 
            fifo.buffer[i].estado == ESTADO_EN_BANDA &&
            !fifo.buffer[i].servoActivado &&
            !fifo.buffer[i].pasoRegistrado) {
          piezaEncontrada = &fifo.buffer[i];
          break;
        }
      }
      
      if (piezaEncontrada != nullptr) {
        piezaEncontrada->pasoRegistrado = true;
        
        if (piezaEncontrada->color == COLOR_ROJO) {
          if (!servoRojo.activo && !servoRojo.esperandoActivacion) {
            servoRojo.esperandoActivacion = true;
            servoRojo.tiempoDeteccion = ahora;
            servoRojo.piezaId = piezaEncontrada->id;
            servoRojo.piezaLista = true;
          }
        } else if (piezaEncontrada->color == COLOR_AZUL) {
          totalPiezasAzules++;
          piezaEncontrada->activa = false;
        }
      }
    }
  }
  
  if (ultrasonidoVerdeActivo) {
    float distancia = leerDistancia(PIN_TRIG_VERDE, PIN_ECHO_VERDE);
    
    if (distancia > DISTANCIA_MINIMA && distancia < DISTANCIA_DETECCION) {
      
      Pieza* piezaEncontrada = nullptr;
      
      for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
        if (fifo.buffer[i].activa && 
            fifo.buffer[i].estado == ESTADO_EN_BANDA &&
            !fifo.buffer[i].servoActivado &&
            !fifo.buffer[i].pasoRegistrado) {
          piezaEncontrada = &fifo.buffer[i];
          break;
        }
      }
      
      if (piezaEncontrada != nullptr) {
        piezaEncontrada->pasoRegistrado = true;
        
        if (piezaEncontrada->color == COLOR_VERDE) {
          if (!servoVerde.activo && !servoVerde.esperandoActivacion) {
            servoVerde.esperandoActivacion = true;
            servoVerde.tiempoDeteccion = ahora;
            servoVerde.piezaId = piezaEncontrada->id;
            servoVerde.piezaLista = true;
          }
        }
      }
    }
  }
  
  if (servoRojo.esperandoActivacion && servoRojo.piezaLista) {
    if (ahora - servoRojo.tiempoDeteccion >= servoRojo.tiempoRetraso) {
      Pieza* pieza = nullptr;
      for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
        if (fifo.buffer[i].activa && 
            fifo.buffer[i].id == servoRojo.piezaId &&
            !fifo.buffer[i].servoActivado) {
          pieza = &fifo.buffer[i];
          break;
        }
      }
      
      if (pieza != nullptr && pieza->color == COLOR_ROJO) {
        pieza->estado = ESTADO_EN_ESTACION;
        pieza->servoActivado = true;
        totalPiezasClasificadas++;
        activarServo(&servoRojo, 1, pieza->id);
        Serial.print(F("🔴 Pieza ROJA ID "));
        Serial.print(pieza->id);
        Serial.println(F(" clasificada"));
      }
      
      servoRojo.esperandoActivacion = false;
      servoRojo.piezaLista = false;
      servoRojo.piezaId = 0;
    }
  }
  
  if (servoVerde.esperandoActivacion && servoVerde.piezaLista) {
    if (ahora - servoVerde.tiempoDeteccion >= servoVerde.tiempoRetraso) {
      Pieza* pieza = nullptr;
      for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
        if (fifo.buffer[i].activa && 
            fifo.buffer[i].id == servoVerde.piezaId &&
            !fifo.buffer[i].servoActivado) {
          pieza = &fifo.buffer[i];
          break;
        }
      }
      
      if (pieza != nullptr && pieza->color == COLOR_VERDE) {
        pieza->estado = ESTADO_EN_ESTACION;
        pieza->servoActivado = true;
        totalPiezasClasificadas++;
        activarServo(&servoVerde, 1, pieza->id);
        Serial.print(F("🟢 Pieza VERDE ID "));
        Serial.print(pieza->id);
        Serial.println(F(" clasificada"));
      }
      
      servoVerde.esperandoActivacion = false;
      servoVerde.piezaLista = false;
      servoVerde.piezaId = 0;
    }
  }
}

void limpiarPiezasAntiguas() {
  for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
    if (fifo.buffer[i].activa) {
      if (fifo.buffer[i].estado == ESTADO_CLASIFICADO ||
          (millis() - fifo.buffer[i].tiempoLectura > 30000)) {
        fifo.buffer[i].activa = false;
        fifo.buffer[i].servoActivado = false;
      }
    }
  }
}

// ====================================================================
// SERVOS DE LA BANDA
// ====================================================================

void activarServo(ServoControl* servo, int direccion, uint16_t piezaId) {
  servo->estado = SERVO_MOVIENDO_ADELANTE;
  servo->tiempoInicio = millis();
  servo->activo = true;
  servo->direccion = direccion;
  servo->piezaId = piezaId;
  
  if (direccion > 0) {
    servo->servo.writeMicroseconds(SERVO_ADELANTE);
  } else {
    servo->servo.writeMicroseconds(SERVO_ATRAS);
  }
}

void actualizarServos() {
  unsigned long ahora = millis();
  
  if (servoRojo.activo) {
    if (servoRojo.estado == SERVO_MOVIENDO_ADELANTE) {
      if (ahora - servoRojo.tiempoInicio >= SERVO_TIEMPO_AVANCE) {
        servoRojo.estado = SERVO_REGRESANDO;
        servoRojo.tiempoInicio = ahora;
        servoRojo.servo.writeMicroseconds(SERVO_ATRAS);
      }
    } else if (servoRojo.estado == SERVO_REGRESANDO) {
      if (ahora - servoRojo.tiempoInicio >= SERVO_TIEMPO_RETROCESO) {
        servoRojo.estado = SERVO_REPOSO;
        servoRojo.activo = false;
        servoRojo.servo.writeMicroseconds(SERVO_STOP);
        servoRojo.piezaId = 0;
      }
    }
  }
  
  if (servoVerde.activo) {
    if (servoVerde.estado == SERVO_MOVIENDO_ADELANTE) {
      if (ahora - servoVerde.tiempoInicio >= SERVO_TIEMPO_AVANCE) {
        servoVerde.estado = SERVO_REGRESANDO;
        servoVerde.tiempoInicio = ahora;
        servoVerde.servo.writeMicroseconds(SERVO_ATRAS);
      }
    } else if (servoVerde.estado == SERVO_REGRESANDO) {
      if (ahora - servoVerde.tiempoInicio >= SERVO_TIEMPO_RETROCESO) {
        servoVerde.estado = SERVO_REPOSO;
        servoVerde.activo = false;
        servoVerde.servo.writeMicroseconds(SERVO_STOP);
        servoVerde.piezaId = 0;
      }
    }
  }
}

// ====================================================================
// FIFO
// ====================================================================

void fifoInit(FIFO* f) {
  f->head = 0; f->tail = 0; f->count = 0; f->nextId = 1;
  for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
    f->buffer[i].activa = false;
    f->buffer[i].servoActivado = false;
    f->buffer[i].pasoRegistrado = false;
  }
}

bool fifoPush(FIFO* f, Pieza p) {
  if (f->count >= FIFO_MAX_PIEZAS) return false;
  f->buffer[f->head] = p;
  f->head = (f->head + 1) % FIFO_MAX_PIEZAS;
  f->count++;
  return true;
}

bool fifoPop(FIFO* f, Pieza* p) {
  if (f->count == 0) return false;
  *p = f->buffer[f->tail];
  f->buffer[f->tail].activa = false;
  f->tail = (f->tail + 1) % FIFO_MAX_PIEZAS;
  f->count--;
  return true;
}

bool fifoPeek(FIFO* f, Pieza* p) {
  if (f->count == 0) return false;
  *p = f->buffer[f->tail];
  return true;
}

void fifoPrint(FIFO* f) {
  if (f->count == 0) { Serial.println(F("📭 FIFO vacía")); return; }
  Serial.println(F("===== FIFO ====="));
  for (int i = 0; i < f->count; i++) {
    int idx = (f->tail + i) % FIFO_MAX_PIEZAS;
    if (f->buffer[idx].activa) {
      Serial.print(F("ID: "));
      Serial.print(f->buffer[idx].id);
      Serial.print(F(" | Color: "));
      switch(f->buffer[idx].color) {
        case COLOR_ROJO: Serial.print(F("ROJO")); break;
        case COLOR_VERDE: Serial.print(F("VERDE")); break;
        case COLOR_AZUL: Serial.print(F("AZUL")); break;
        default: Serial.print(F("NINGUNO")); break;
      }
      Serial.println(F(""));
    }
  }
  Serial.println(F("================"));
}

bool fifoEstaVacia(FIFO* f) { return f->count == 0; }
bool fifoEstaLlena(FIFO* f) { return f->count >= FIFO_MAX_PIEZAS; }

// ====================================================================
// COMANDOS SERIALES
// ====================================================================

void procesarComandoSerial(char comando) {
  switch(comando) {
    case 'O': case 'o': iniciarProduccion(); break;
    case 'S': case 's': detenerTodo(); break;
    case 'R': case 'r': reanudarSistema(); break;
    case 'P': case 'p': resetearSistema(); break;
    case 'M': case 'm': crearPiezaManual(); break;
    case 'C': case 'c': mostrarFIFO(); break;
    case 'E': case 'e': mostrarEstadisticas(); break;
    case 'H': case 'h': mostrarAyuda(); break;
    case '1': pruebaServoManual(&servoRojo, 1, "ROJO (ADELANTE)"); break;
    case '2': pruebaServoManual(&servoVerde, 1, "VERDE (ADELANTE)"); break;
    case '3': pruebaServoManual(&servoRojo, -1, "ROJO (ATRÁS)"); break;
    case '4': pruebaServoManual(&servoVerde, -1, "VERDE (ATRÁS)"); break;
    default: Serial.println(F("❌ Comando no válido. H para ayuda.")); break;
  }
}

// ====================================================================
// DETENER TODO
// ====================================================================

void detenerTodo() {
  estadoSistema = SISTEMA_DETENIDO;
  controlarMotor(0);
  
  escribirServo(SERVO_DETENER);
  piezaDetectada = false;
  grupoEjecutado = false;
  contadorConfirmacion = 0;
  
  byte stopCommand[] = {0x55, 0x55, 0x03, 0x01, 0x00, 0x00};
  Serial1.write(stopCommand, sizeof(stopCommand));
  delay(50);
  
  servoVerde.servo.writeMicroseconds(SERVO_STOP);
  servoRojo.servo.writeMicroseconds(SERVO_STOP);
  servoVerde.activo = false;
  servoRojo.activo = false;
  servoVerde.estado = SERVO_REPOSO;
  servoRojo.estado = SERVO_REPOSO;
  
  Serial.println(F(""));
  Serial.println(F("⏹️ === SISTEMA DETENIDO COMPLETAMENTE ==="));
  Serial.println(F("   ✅ Banda: DETENIDA"));
  Serial.println(F("   ✅ Dispensador (pin 26): DETENIDO en 90°"));
  Serial.println(F("   ✅ Brazo Robótico: PARADO"));
  Serial.println(F(""));
}

// ====================================================================
// MOSTRAR AYUDA
// ====================================================================

void mostrarAyuda() {
  Serial.println(F(""));
  Serial.println(F("===== COMANDOS ====="));
  Serial.println(F("  O - Iniciar producción"));
  Serial.println(F("  S - Detener TODO (Banda + Brazo + Dispensador)"));
  Serial.println(F("  R - Reanudar"));
  Serial.println(F("  P - Reset"));
  Serial.println(F("  M - Crear pieza manual"));
  Serial.println(F("  C - Mostrar FIFO"));
  Serial.println(F("  E - Estadísticas"));
  Serial.println(F("  H - Ayuda"));
  Serial.println(F("  1 - Servo ROJO ADELANTE"));
  Serial.println(F("  2 - Servo VERDE ADELANTE"));
  Serial.println(F("  3 - Servo ROJO ATRÁS"));
  Serial.println(F("  4 - Servo VERDE ATRÁS"));
  Serial.println(F("==================="));
}

// ====================================================================
// MOSTRAR ESTADÍSTICAS
// ====================================================================

void mostrarEstadisticas() {
  imprimirSeparador();
  Serial.println(F("   ESTADÍSTICAS"));
  imprimirSeparador();
  Serial.print(F("Estado: "));
  switch(estadoSistema) {
    case SISTEMA_DETENIDO: Serial.println(F("DETENIDO")); break;
    case SISTEMA_PRODUCIENDO: Serial.println(F("PRODUCIENDO")); break;
    case SISTEMA_PAUSADO: Serial.println(F("PAUSADO")); break;
  }
  Serial.print(F("Motor: "));
  Serial.println(motor.habilitado ? "ON" : "OFF");
  Serial.print(F("RPM: "));
  Serial.println(motor.rpmFiltrada, 1);
  Serial.print(F("PWM: "));
  Serial.println(motor.velocidad);
  Serial.print(F("Error: "));
  Serial.println(RPM_SETPOINT - motor.rpmFiltrada, 1);
  Serial.print(F("Piezas detectadas: "));
  Serial.println(totalPiezasDetectadas);
  Serial.print(F("Piezas clasificadas: "));
  Serial.println(totalPiezasClasificadas);
  Serial.print(F("Piezas AZULES: "));
  Serial.println(totalPiezasAzules);
  Serial.print(F("Piezas en FIFO: "));
  Serial.println(fifo.count);
  imprimirSeparador();
}

void mostrarFIFO() { fifoPrint(&fifo); }
void imprimirSeparador() { Serial.println(F("========================================")); }

// ====================================================================
// CONTROL DEL SISTEMA
// ====================================================================

void iniciarProduccion() {
  if (estadoSistema == SISTEMA_DETENIDO || estadoSistema == SISTEMA_PAUSADO) {
    estadoSistema = SISTEMA_PRODUCIENDO;
    
    arrancando = true;
    tiempoArranque = 0;
    golpeInicialAplicado = false;
    tiempoGolpe = 0;
    motor.velocidad = 0;
    
    piezaDetectada = false;
    grupoEjecutado = false;
    contadorConfirmacion = 0;
    escribirServo(SERVO_GIRAR);
    Serial.println(F("🔄 Distribuidor: Buscando piezas (145°)"));
    
    controlarMotor(PWM_ARRANQUE);
    Serial.println(F(""));
    Serial.println(F("▶️ === PRODUCCIÓN INICIADA ==="));
    Serial.println(F(""));
  } else {
    Serial.println(F("⚠️ Ya está en producción"));
  }
}

void detenerProduccion() {
  detenerTodo();
}

void reanudarSistema() {
  if (estadoSistema == SISTEMA_PAUSADO) {
    estadoSistema = SISTEMA_PRODUCIENDO;
    arrancando = true;
    tiempoArranque = 0;
    golpeInicialAplicado = false;
    tiempoGolpe = 0;
    
    piezaDetectada = false;
    grupoEjecutado = false;
    contadorConfirmacion = 0;
    escribirServo(SERVO_GIRAR);
    
    controlarMotor(PWM_ARRANQUE);
    Serial.println(F("▶️ Sistema reanudado"));
  } else {
    Serial.println(F("⚠️ No está pausado"));
  }
}

void resetearSistema() {
  detenerTodo();
  for (int i = 0; i < FIFO_MAX_PIEZAS; i++) {
    fifo.buffer[i].activa = false;
    fifo.buffer[i].servoActivado = false;
    fifo.buffer[i].pasoRegistrado = false;
  }
  fifo.head = 0; fifo.tail = 0; fifo.count = 0; fifo.nextId = 1;
  totalPiezasDetectadas = 0;
  totalPiezasClasificadas = 0;
  totalPiezasAzules = 0;
  motor.pulsos = 0;
  motor.rpmActual = 0;
  motor.rpmFiltrada = 0;
  
  arrancando = true;
  tiempoArranque = 0;
  golpeInicialAplicado = false;
  tiempoGolpe = 0;
  
  ultrasonidoVerdeActivo = false;
  ultrasonidoRojoActivo = false;
  
  servoVerde.servo.writeMicroseconds(SERVO_STOP);
  servoRojo.servo.writeMicroseconds(SERVO_STOP);
  servoVerde.activo = false;
  servoRojo.activo = false;
  servoVerde.estado = SERVO_REPOSO;
  servoRojo.estado = SERVO_REPOSO;
  servoVerde.esperandoActivacion = false;
  servoRojo.esperandoActivacion = false;
  servoVerde.piezaLista = false;
  servoRojo.piezaLista = false;
  
  piezaDetectada = false;
  grupoEjecutado = false;
  contadorConfirmacion = 0;
  escribirServo(SERVO_GIRAR);
  
  calibracionFondoInicializada = false;
  calibracionFondo.r = 0;
  calibracionFondo.g = 0;
  calibracionFondo.b = 0;
  
  Serial.println(F(""));
  Serial.println(F("🔄 === SISTEMA RESETEADO ==="));
  Serial.println(F(""));
}

void pausarSistema() {
  if (estadoSistema == SISTEMA_PRODUCIENDO) {
    estadoSistema = SISTEMA_PAUSADO;
    controlarMotor(0);
    escribirServo(SERVO_DETENER);
    Serial.println(F("⏸️ Sistema pausado"));
  }
}

void pruebaServoManual(ServoControl* servo, int direccion, const char* nombre) {
  Serial.print(F("🔧 Probando servo "));
  Serial.print(nombre);
  Serial.println(F("..."));
  
  servo->servo.writeMicroseconds(SERVO_STOP);
  delay(200);
  
  if (direccion > 0) {
    Serial.println(F("   ADELANTE (2 segundos)"));
    servo->servo.writeMicroseconds(SERVO_ADELANTE);
  } else {
    Serial.println(F("   ATRÁS (2 segundos)"));
    servo->servo.writeMicroseconds(SERVO_ATRAS);
  }
  
  delay(2000);
  
  Serial.println(F("   STOP"));
  servo->servo.writeMicroseconds(SERVO_STOP);
  
  Serial.println(F("✅ Prueba completada"));
}

// ====================================================================
// FIN DEL CÓDIGO
// ====================================================================
