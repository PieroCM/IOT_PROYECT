// ============================================================================
// PaltaCheck - Test Unitario Completo (Motores + Servo Integrado)
// ESP32-S3 + 2 L298N + 3 motores + Servo SG90 (Pines Físicos Seguros)
// Flujo: Cinta -> 3 Vueltas (Giro Rodillos) -> Servo Compuerta -> Botar
// ============================================================================

#include <ESP32Servo.h>

// ==========================================
// L298N #1 - Cinta
// ==========================================
const int ENA_CINTA = 46;
const int IN1_CINTA = 48;
const int IN2_CINTA = 35;

// ==========================================
// L298N #2 - Rodillos
// ==========================================
// Rodillo 1
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;

// Rodillo 2
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;

// ==========================================
// SERVO SG90
// ==========================================
Servo servoCompuerta;
const int PIN_SERVO = 3;           // Conectado al GPIO 3

// Ángulo de reposo (evitamos 0 grados para que no se trabe el engranaje)
const int ANGULO_REPOSO = 15;

// Ángulo de descarte (45 grados de recorrido desde el reposo: 15 + 45 = 60)
const int ANGULO_DESCARTE = 60;

// Posición actual del servo
int posicionActual = ANGULO_REPOSO;

// Velocidad de movimiento suave (delay en ms por grado)
const int velocidadMovimiento = 10; 

// ==========================================
// CONFIGURACIÓN PWM
// ==========================================
const int freqPWM = 1000;
const int resolucionPWM = 8;

// ==========================================
// VELOCIDADES
// ==========================================
const int velocidadCinta = 100;
const int potenciaGiro = 45;
const int potenciaBotar = 120;

// ==========================================
// TIEMPOS DEL CICLO
// ==========================================
const unsigned long tiempoCinta = 10000;
const unsigned long tiempoGiroRodillos = 3000;
const unsigned long tiempoFoto = 5000;
const unsigned long tiempoBotar = 4000;

const int cantidadVueltas = 3;

// Pulsos para las vueltas
const int tiempoPulsoGiroEncendido = 50;
const int tiempoPulsoGiroApagado = 1000;

// Declaración de funciones
void cintaAdelante(int velocidad);
void detenerCinta();
void rodillosParaGirarPalta(int velocidad1, int velocidad2);
void rodillosParaBotarPalta(int velocidad1, int velocidad2);
void darSaltoRodillos(int duracionMs);
void botarPalta(unsigned long tiempoTotal);
void detenerRodillos();
void detenerTodo();
void moverCompuerta(int nuevaPos);

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- Sistema iniciado - PaltaCheck Test Completo ---");

  // Cinta
  pinMode(IN1_CINTA, OUTPUT);
  pinMode(IN2_CINTA, OUTPUT);

  // Rodillos
  pinMode(IN1_RODILLO1, OUTPUT);
  pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT);
  pinMode(IN4_RODILLO2, OUTPUT);

  // Adjuntar PWM a pines habilitadores usando ledcAttach (ESP32 Core v3.x)
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);

  // Configuración recomendada para servo en ESP32
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 544, 2400);

  // Forzar inicio en ángulo de reposo (15 grados)
  servoCompuerta.write(posicionActual);
  delay(500);
  Serial.println("[OK] Servo listo en reposo: " + String(posicionActual) + " grados");

  detenerTodo();
  Serial.println("El test comenzará en 3 segundos...");
  delay(3000);
}

// ==========================================
// LOOP PRINCIPAL
// ==========================================
void loop() {
  // 1) CINTA TRANSPORTADORA
  Serial.println("\n1) Cinta iniciando");
  cintaAdelante(velocidadCinta);
  delay(tiempoCinta);

  Serial.println("-> Deteniendo cinta");
  detenerCinta();
  delay(1000);

  // 2) PALTA EN LOS RODILLOS
  for (int i = 1; i <= cantidadVueltas; i++) {
    Serial.print("2) Vuelta / Foto ");
    Serial.println(i);

    Serial.println("Palta en rodillos - dando un salto para rotar");
    darSaltoRodillos(500); // Salto de giro de 500 ms

    Serial.println("Esperando 3 segundos a que la palta se estabilice...");
    delay(3000); // Demora 3 segundos

    Serial.println("3) Tomando foto simulada");
  }

  // 3) MOVER COMPUERTA DEL SERVO (Descarte)
  Serial.println("4) Moviendo compuerta del servo a " + String(ANGULO_DESCARTE) + " grados...");
  moverCompuerta(ANGULO_DESCARTE);
  delay(1000); // Esperar a que se posicione bien

  // 4) BOTAR PALTA
  Serial.println("5) Botando la palta");
  botarPalta(tiempoBotar);

  // 5) REGRESAR COMPUERTA DEL SERVO
  Serial.println("6) Regresando compuerta del servo a " + String(ANGULO_REPOSO) + " grados...");
  moverCompuerta(ANGULO_REPOSO);
  delay(1000);

  // 6) STOP FINAL
  Serial.println("7) Proceso terminado");
  detenerTodo();

  Serial.println("\nPrueba completada. Pulsa RESET en tu ESP32 para reiniciar.");
  while (true) {
    delay(1000);
  }
}

// ==========================================
// FUNCIONES CINTA
// ==========================================
void cintaAdelante(int velocidad) {
  digitalWrite(IN1_CINTA, HIGH);
  digitalWrite(IN2_CINTA, LOW);
  ledcWrite(ENA_CINTA, velocidad);
}

void detenerCinta() {
  ledcWrite(ENA_CINTA, 0);
  digitalWrite(IN1_CINTA, LOW);
  digitalWrite(IN2_CINTA, LOW);
}

// ==========================================
// RODILLOS CUANDO LA PALTA ESTÁ AHÍ
// ==========================================
void rodillosParaGirarPalta(int velocidad1, int velocidad2) {
  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, velocidad1);

  digitalWrite(IN3_RODILLO2, LOW);
  digitalWrite(IN4_RODILLO2, HIGH);
  ledcWrite(ENB_RODILLO2, velocidad2);
}

// ==========================================
// RODILLOS PARA BOTAR PALTA
// ==========================================
void rodillosParaBotarPalta(int velocidad1, int velocidad2) {
  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, velocidad1);

  digitalWrite(IN3_RODILLO2, HIGH);
  digitalWrite(IN4_RODILLO2, LOW);
  ledcWrite(ENB_RODILLO2, velocidad2);
}

// ==========================================
// GIRO DE LA PALTA CON SALTOS (PULSO ÚNICO Y CORTO)
// ==========================================
void darSaltoRodillos(int duracionMs) {
  rodillosParaGirarPalta(potenciaGiro, potenciaGiro);
  delay(duracionMs);
  detenerRodillos();
}

// ==========================================
// BOTAR PALTA SIN PULSOS
// ==========================================
void botarPalta(unsigned long tiempoTotal) {
  rodillosParaBotarPalta(potenciaBotar, potenciaBotar);
  delay(tiempoTotal);
  detenerRodillos();
}

// ==========================================
// DETENER RODILLOS
// ==========================================
void detenerRodillos() {
  ledcWrite(ENA_RODILLO1, 0);
  ledcWrite(ENB_RODILLO2, 0);

  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, LOW);

  digitalWrite(IN3_RODILLO2, LOW);
  digitalWrite(IN4_RODILLO2, LOW);
}

// ==========================================
// STOP GENERAL
// ==========================================
void detenerTodo() {
  detenerCinta();
  detenerRodillos();
}

// ==========================================
// FUNCIÓN DE CONTROL DEL SERVO (Movimiento suave)
// ==========================================
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 10, 170); // Margen de seguridad

  Serial.println("Moviendo: " + String(posicionActual) + " grados -> " + String(nuevaPos) + " grados");

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
  Serial.println("Listo en: " + String(posicionActual) + " grados");
}
