/* ============================================================================
   TEST SERVO con la LIBRERÍA ESP32Servo  (GPIO 3 y GPIO 0)
   ----------------------------------------------------------------------------
   Para comparar contra el test por LEDC y ver cuál anda mejor en TU servo.

   CABLEADO:  amarillo -> pin de prueba (3 o 0) · rojo -> 5V · marrón -> GND
   COMANDOS (Serial 115200):  3=GPIO3  0=GPIO0  t=barrido  a=abrir90  c=cerrar0

   Requiere la librería ESP32Servo instalada.
   ============================================================================ */

#include <ESP32Servo.h>

Servo servo;
int pinActual = 3;

void reattach(int p) {
  servo.detach();
  pinActual = p;
  servo.setPeriodHertz(50);
  servo.attach(pinActual, 500, 2400);
  servo.write(0);
  Serial.printf("\n>>> Probando en GPIO %d (conecta el amarillo a este pin)\n", pinActual);
}

void barrido() {
  Serial.printf("[TEST] barrido en GPIO %d ...\n", pinActual);
  for (int a = 0;   a <= 180; a += 5) { servo.write(a); delay(20); }
  delay(300);
  for (int a = 180; a >= 0;   a -= 5) { servo.write(a); delay(20); }
  Serial.println("[TEST] si se movio suave y se quedo quieto -> servo VIVO y pin OK");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== TEST SERVO con LIBRERIA (GPIO 3 / 0) =====");
  // Reserva timers libres para el servo (no choca con nada en este test simple)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servo.setPeriodHertz(50);
  servo.attach(pinActual, 500, 2400);
  servo.write(0);
  Serial.println("Comandos:  3=GPIO3  0=GPIO0  t=barrido  a=abrir  c=cerrar");
  barrido();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if      (c == '3') reattach(3);
    else if (c == '0') reattach(0);
    else if (c == 't' || c == 'T') barrido();
    else if (c == 'a' || c == 'A') { servo.write(90); Serial.println("[SERVO] 90"); }
    else if (c == 'c' || c == 'C') { servo.write(0);  Serial.println("[SERVO] 0"); }
  }
}
