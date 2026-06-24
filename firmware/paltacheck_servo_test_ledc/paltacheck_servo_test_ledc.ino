/* ============================================================================
   PaltaCheck — TEST del SERVO (compuerta) por LEDC directo
   ESP32-S3 · solo el servo (sin cámara/motores).
   ----------------------------------------------------------------------------
   PINES:
     Señal -> GPIO 3
     VIN(+) -> 5 V  (ideal 5 V aparte con reductor; para probar, 5V de la placa)
     GND(-) -> GND COMÚN (placa + fuente + servo)

   Qué hace: cada 2 s mueve la compuerta 0° -> 90° -> 0° y la MANTIENE quieta.
   Comandos por Monitor Serial (115200):  a = abrir (90)   c = cerrar (0)
   Usa LEDC directo (no ESP32Servo) para evitar el tembleque de timers.
   ============================================================================ */

const int PIN_SERVO  = 3;
const int SERVO_FREQ = 50;     // Hz (servo estándar 180°)
const int SERVO_RES  = 16;     // bits de resolución del PWM
const int ANGULO_ABRE = 90;    // cuánto abre la compuerta

int posicionActual = 0;
unsigned long ultimo = 0;
bool abierto = false;

// Escribe un ángulo (0-180) al servo: pulso 500-2400 us dentro de 20 ms.
void servoEscribir(int ang) {
  ang = constrain(ang, 0, 180);
  int us = map(ang, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << SERVO_RES) - 1) / 20000UL);
  ledcWrite(PIN_SERVO, duty);
  posicionActual = ang;
  Serial.printf("[SERVO] -> %d grados\n", ang);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== TEST SERVO (LEDC, GPIO 3) =====");
  ledcAttach(PIN_SERVO, SERVO_FREQ, SERVO_RES);
  servoEscribir(0);    // cerrado
  Serial.println("Comandos: a = abrir (90)   c = cerrar (0)");
}

void loop() {
  // Comandos manuales
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'a' || cmd == 'A') servoEscribir(ANGULO_ABRE);
    else if (cmd == 'c' || cmd == 'C') servoEscribir(0);
  }

  // Prueba automática cada 2 s: alterna 90 / 0 y MANTIENE la posición
  if (millis() - ultimo > 2000) {
    ultimo = millis();
    abierto = !abierto;
    servoEscribir(abierto ? ANGULO_ABRE : 0);
  }
}
