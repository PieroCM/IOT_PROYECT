/* ============================================================================
   TEST SERVO en GPIO 3 y GPIO 0  (ESP32-S3) — por LEDC directo
   ----------------------------------------------------------------------------
   Sirve para: 1) ver en qué pin responde el servo, 2) comprobar que NO se quemó.

   CABLEADO del servo:  amarillo -> pin de prueba (3 o 0) · rojo -> 5V · marrón -> GND
   (5V y GND comunes con el ESP32)

   COMANDOS por Monitor Serial (115200):
     3 = usar GPIO 3      0 = usar GPIO 0
     t = test (barrido 0->90->180->0, para ver si se mueve / no se quemó)
     a = abrir 90         c = cerrar 0

   Al arrancar usa GPIO 3 y hace un test automático.
   ============================================================================ */

const int SERVO_FREQ = 50;
const int SERVO_RES  = 16;
int pinActual = 3;

void servoEscribir(int ang) {
  ang = constrain(ang, 0, 180);
  int us = map(ang, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << SERVO_RES) - 1) / 20000UL);
  ledcWrite(pinActual, duty);
}

void usarPin(int p) {
  ledcDetach(pinActual);          // suelta el pin anterior
  pinActual = p;
  ledcAttach(pinActual, SERVO_FREQ, SERVO_RES);
  servoEscribir(0);
  Serial.printf("\n>>> Ahora probando en GPIO %d  (conecta el cable amarillo a este pin)\n", pinActual);
}

void testBarrido() {
  Serial.printf("[TEST] barrido en GPIO %d ...\n", pinActual);
  for (int a = 0;   a <= 180; a += 5) { servoEscribir(a); delay(20); }
  delay(300);
  for (int a = 180; a >= 0;   a -= 5) { servoEscribir(a); delay(20); }
  Serial.println("[TEST] listo. Si se movio -> servo VIVO y este pin FUNCIONA.");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== TEST SERVO (GPIO 3 / GPIO 0) =====");
  ledcAttach(pinActual, SERVO_FREQ, SERVO_RES);
  servoEscribir(0);
  Serial.println("Comandos:  3=GPIO3  0=GPIO0  t=test  a=abrir  c=cerrar");
  testBarrido();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if      (c == '3') usarPin(3);
    else if (c == '0') usarPin(0);
    else if (c == 't' || c == 'T') testBarrido();
    else if (c == 'a' || c == 'A') { servoEscribir(90); Serial.println("[SERVO] 90"); }
    else if (c == 'c' || c == 'C') { servoEscribir(0);  Serial.println("[SERVO] 0"); }
  }
}
