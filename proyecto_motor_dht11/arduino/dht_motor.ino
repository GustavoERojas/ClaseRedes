#include <DHT.h>

#define DHTPIN 2
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// Pines puente H
const int IN1 = 8;
const int IN2 = 9;
const int ENA = 10; // PWM

float temperatura = 0;
float humedad = 0;

void setup() {
  Serial.begin(9600);
  dht.begin();

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  // Dirección del motor (siempre igual)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void loop() {
  humedad = dht.readHumidity();
  temperatura = dht.readTemperature();

  if (isnan(humedad) || isnan(temperatura)) {
    Serial.println("ERROR");
    return;
  }

  int velocidadPWM = 0;
  String rango = "";

  // 💧 RANGOS DE HUMEDAD (VERSIÓN INVERSA)
  // Mientras más humedad, menos velocidad
  if (humedad < 40) {
    velocidadPWM = 255;  // alta velocidad (seco)
    rango = "SECO";
  } 
  else if (humedad >= 40 && humedad < 70) {
    velocidadPWM = 160;  // media velocidad (moderado)
    rango = "MODERADO";
  } 
  else {
    velocidadPWM = 80;   // baja velocidad (húmedo)
    rango = "HUMEDO";
  }

  analogWrite(ENA, velocidadPWM);

  // Enviar datos a Python
  Serial.print("TEMP:");
  Serial.print(temperatura);
  Serial.print(",HUM:");
  Serial.print(humedad);
  Serial.print(",PWM:");
  Serial.print(velocidadPWM);
  Serial.print(",RANGO:");
  Serial.println(rango);

  delay(1000);
}