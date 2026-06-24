/* ============================================================================
   PaltaCheck — PRUEBA SOLO DEL SERVO (compuerta de rechazo)
   ESP32-S3 · script aparte, sin motores/cámara/WiFi.
   ----------------------------------------------------------------------------
   Simula que llega una palta MALA: cada cierto tiempo ABRE la compuerta (90°)
   y luego la CIERRA (0°), para verificar que el servo responde bien.

   También puedes controlarlo a mano por el Monitor Serial (115200):
     'a' = abrir (mala)      'c' = cerrar       'm' = simular palta mala (ciclo)

   Servo en GPIO 3, alimentado con 5 V aparte y GND común con el ESP32.
   Requiere la librería ESP32Servo.
   ============================================================================ */

#include <ESP32Servo.h>

const int PIN_SERVO          = 3;
const int ANGULO_COMPUERTA   = 90;   // cuánto abre cuando es MALA
const int velocidadMovimiento = 3;   // ms por grado (menor = más rápido)

// Ciclo automático: cada cuánto simula una palta mala
const unsigned long INTERVALO_PRUEBA_MS = 5000;  // cada 5 s
const unsigned long TIEMPO_ABIERTA_MS   = 2000;  // 2 s abierta

Servo servoCompuerta;
int  posicionActual = 0;
unsigned long ultimaPrueba = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== PRUEBA SERVO (compuerta de rechazo) =====");

  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(0);
  posicionActual = 0;

  Serial.println("Servo listo en 0 grados (compuerta CERRADA).");
  Serial.println("Comandos: a=abrir  c=cerrar  m=simular palta mala");
  Serial.println("Tambien simula una palta mala automaticamente cada 5 s.\n");
}

void loop() {
  // 1) Comandos manuales por Serial
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'a' || cmd == 'A') moverCompuerta(ANGULO_COMPUERTA);
    else if (cmd == 'c' || cmd == 'C') moverCompuerta(0);
    else if (cmd == 'm' || cmd == 'M') simularPaltaMala();
  }

  // 2) Simulación automática cada INTERVALO_PRUEBA_MS
  if (millis() - ultimaPrueba > INTERVALO_PRUEBA_MS) {
    ultimaPrueba = millis();
    simularPaltaMala();
  }
}

// Simula el flujo de una palta MALA: abre la compuerta, espera y cierra.
void simularPaltaMala() {
  Serial.println("\n[SIM] Palta MALA detectada -> ABRE compuerta");
  moverCompuerta(ANGULO_COMPUERTA);
  delay(TIEMPO_ABIERTA_MS);
  Serial.println("[SIM] Cierra compuerta");
  moverCompuerta(0);
}

// Movimiento suave grado a grado.
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 0, 180);
  Serial.printf("  [SERVO] %d -> %d grados\n", posicionActual, nuevaPos);
  if (nuevaPos > posicionActual)
    for (int p = posicionActual; p <= nuevaPos; p++) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  else
    for (int p = posicionActual; p >= nuevaPos; p--) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  posicionActual = nuevaPos;
}
