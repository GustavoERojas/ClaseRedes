const int LED_PIN = 13;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(9600);
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "LED_ON") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("OK:LED_ON");
    } else if (cmd == "LED_OFF") {
      digitalWrite(LED_PIN, LOW);
      Serial.println("OK:LED_OFF");
    } else {
      Serial.println("ERR:CMD");
    }
  }
}

