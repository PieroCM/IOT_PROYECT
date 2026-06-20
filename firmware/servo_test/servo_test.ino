#include <ESP32Servo.h>

Servo servoCompuerta;

// Servo conectado al GPIO 3
const int PIN_SERVO = 3;

// Ángulo de reposo (evitamos 0 grados para que no se trabe el engranaje)
const int ANGULO_REPOSO = 15;

// Ángulo de descarte (45 grados de recorrido desde el reposo: 15 + 45 = 60)
const int ANGULO_DESCARTE = 60;

// Posición actual
int posicionActual = ANGULO_REPOSO;

// Velocidad de movimiento suave (delay en ms por grado)
// 10ms da un recorrido suave y permite al servo responder a los pulsos de 50Hz
const int velocidadMovimiento = 10; 

void moverCompuerta(int nuevaPos);

void setup() {
  Serial.begin(115200);
  delay(500);

  // Configuración estándar recomendada para SG90 en ESP32 (544 a 2400 microsegundos)
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 544, 2400);

  // Forzar inicio en ángulo de reposo (15 grados)
  servoCompuerta.write(posicionActual);
  delay(500);

  Serial.println("\n=== Test de Servo Mejorado (Subir y Bajar Automático) ===");
  Serial.printf("Servo listo en reposo: %d grados\n", posicionActual);
  Serial.println("Comandos:");
  Serial.println("A = Ejecutar ciclo completo (Subir a 60° -> Esperar 2s -> Bajar a 15°)");
  Serial.println("R = Forzar reset manual a posición de reposo (15°)");
  Serial.println("F = Test rápido sin suavizado (Mueve directo 60° -> 15°)");
}

void loop() {
  if (Serial.available() > 0) {
    char comando = Serial.read();

    if (comando == 'A' || comando == 'a') {
      Serial.println("\n>>> Iniciando ciclo automático de compuerta...");
      
      // 1) Subir suavemente a 60 grados (descarte)
      moverCompuerta(ANGULO_DESCARTE);
      
      // 2) Mantener arriba por 2 segundos
      Serial.println("[WAIT] Esperando 2 segundos arriba...");
      delay(2000);
      
      // 3) Bajar suavemente a 15 grados (reposo)
      moverCompuerta(ANGULO_REPOSO);
      
      Serial.println(">>> Ciclo automático finalizado.");
    } 
    else if (comando == 'R' || comando == 'r') {
      Serial.println("\n>>> Forzando reset manual a posición de reposo (15°)...");
      moverCompuerta(ANGULO_REPOSO);
    } 
    else if (comando == 'F' || comando == 'f') {
      Serial.println("\n>>> Ejecutando test rápido (sin suavizar)...");
      servoCompuerta.write(ANGULO_DESCARTE);
      delay(1500);
      servoCompuerta.write(ANGULO_REPOSO);
      posicionActual = ANGULO_REPOSO;
      Serial.println(">>> Test rápido finalizado.");
    }
  }
}

// Función para mover el servo de forma suave grado a grado
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 10, 170); // Seguridad para no forzar límites del engranaje

  Serial.printf("Moviendo: %d grados -> %d grados\n", posicionActual, nuevaPos);

  if (nuevaPos > posicionActual) {
    for (int pos = posicionActual; pos <= nuevaPos; pos++) {
      servoCompuerta.write(pos);
      delay(velocidadMovimiento);
    }
  } 
  else {
    for (int pos = posicionActual; pos >= nuevaPos; pos--) {
      servoCompuerta.write(pos);
      delay(velocidadMovimiento);
    }
  }

  posicionActual = nuevaPos;
  Serial.printf("Listo en: %d grados\n", posicionActual);
}
