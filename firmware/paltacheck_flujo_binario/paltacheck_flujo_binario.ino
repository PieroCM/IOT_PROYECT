/* ============================================================================
   PaltaCheck — FLUJO 3 CLASES (sana/antracnosis/scab) + SERVO   (ESP32-S3 N16R8)
   ----------------------------------------------------------------------------
   El MODELO corre en el BACKEND (gate palta/no_palta + enfermedad). El ESP32
   manda 3 fotos por fruta y el backend VOTA; luego lee el veredicto AGREGADO
   (campo clasificacion_agregada) y decide la compuerta:
       sana        -> PASA (compuerta cerrada, solo rodillos hacia adelante)
       antracnosis -> EXPULSA (enferma)
       scab        -> EXPULSA (enferma)
       no_es_palta -> EXPULSA (no es palta)
       (sin respuesta de red -> NO expulsa, la deja pasar y loguea)
   Flujo (mientras haya lote activo):
     1) Cinta avanza HASTA que el IR detecta la fruta (LOW, con debounce).
     2) Al detectar, la cinta sigue 5 s MÁS (cae a rodillos) y RECIÉN para.
     3) Espera 2 s con la fruta en los rodillos.
     4) 3 rondas: gira rodillos 4 s -> para -> ~0.5 s estabiliza -> foto fresca ->
        el GATE decide: si ES palta toma sensores (TCS/DHT) y corre enfermedad;
        si NO es palta, OMITE sensores+enfermedad (eficiencia, no promediable).
     5) Decide con el veredicto AGREGADO: EXPULSA (servo 90° 10 s + rodillos 5 s)
        si no_es_palta/antracnosis/scab; PASA si sana; sin respuesta -> deja pasar.
     6) Si se cierra el lote, frena todo.
   Requiere ESP32Servo. Servo en GPIO 3 (5 V aparte + GND común).
   El backend debe estar reconstruido con el modelo:  docker compose up -d --build backend
   ============================================================================ */
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Wire.h>
#include <DHT.h>
#include "Adafruit_TCS34725.h"
#include <ESP32Servo.h>
// ─── CONFIG WiFi + Backend ──────────────────────────────────────────────────
const char* SSID         = "Aifon 19 pro max DE TEMU";
const char* PASSWORD     = "142536879ANA";
const char* BACKEND_HOST = "10.95.38.254";   // IPv4 del adaptador Wi-Fi de la laptop (ipconfig)
const int   BACKEND_PORT = 8000;
// ─── PINES CÁMARA (modelo EYE, fijos) ───────────────────────────────────────
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_D7    16
#define CAM_PIN_D6    17
#define CAM_PIN_D5    18
#define CAM_PIN_D4    12
#define CAM_PIN_D3    10
#define CAM_PIN_D2     8
#define CAM_PIN_D1     9
#define CAM_PIN_D0    11
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK  13
// ─── PINES SENSORES ─────────────────────────────────────────────────────────
#define I2C_SDA    14
#define I2C_SCL    21
#define PIN_DHT22   2
#define PIN_IR      1
// ─── PINES CINTA (L298N #1) ─────────────────────────────────────────────────
const int ENA_CINTA = 46;
const int IN1_CINTA = 48;     // IN2 -> GND directo
// ─── PINES RODILLOS (L298N #2) ──────────────────────────────────────────────
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;
// ─── SERVO compuerta ────────────────────────────────────────────────────────
const int PIN_SERVO           = 3;   // señal del servo (cable amarillo)
const int ANGULO_COMPUERTA    = 90;
const int velocidadMovimiento = 3;   // ms por grado (movimiento suave)
Servo servoCompuerta;
int  posicionActual = 0;
// ─── PWM motores ────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;
// ─── Velocidades / tiempos ──────────────────────────────────────────────────
const int velocidadCinta            = 100;
const int dutyArranqueCinta         = 180;
const int tiempoArranqueCintaMs     = 150;
const int potenciaBotar             = 255;
// Sentido de EXPULSIÓN. Invertido (false) -> ambos rodillos giran al contrario.
// Si la fruta sale hacia el lado equivocado, vuelve a ponerlo en true.
const bool EXPULSION_NORMAL         = false;
const int dutyArranque              = 200;
const int dutyLento                 = 120;
const int tiempoArranqueMs          = 200;
// ─── TIEMPOS del flujo (todos aquí arriba, fáciles de tunear) ───────────────
const unsigned long tiempoCintaExtra    = 5000;  // tras detectar IR, la cinta sigue 5 s y RECIÉN para
const unsigned long tiempoEnRodillos    = 2000;  // 2 s con la fruta ya en los rodillos
const unsigned long tiempoGiroRodillos  = 4000;  // gira ambos rodillos 4 s por ronda
const unsigned long tiempoEstabilizacion = 500;  // ~0.5 s quieto antes de la foto (sin motion blur)
const unsigned long tiempoServoArriba   = 10000; // 10 s con la compuerta levantada (90°)
const unsigned long tiempoExpulsion     = 5000;  // 5 s de rodillos de expulsión con fuerza
const int cantidadVueltas               = 3;     // 3 fotos por fruta (el backend vota)
const int IR_DEBOUNCE_MS        = 60;
const unsigned long IR_RELEASE_TIMEOUT = 10000;
// ─── Sensores ───────────────────────────────────────────────────────────────
DHT dht(PIN_DHT22, DHT22);
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
bool tcsOK = false;
// ─── WiFi/Backend ───────────────────────────────────────────────────────────
const unsigned long POLL_LOTE_MS = 5000;
bool loteActivo  = false;
int  loteId      = -1;
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== FLUJO BINARIO: Palta / No-palta + Servo =====");
  pinMode(IN1_CINTA, OUTPUT);
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  pinMode(IN1_RODILLO1, OUTPUT); pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT); pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerTodo();
  // Servo con la librería ESP32Servo (como cuando funcionaba). Le damos timers
  // LEDC altos (2 y 3) para que no choque con la cámara (timer 0) ni los motores.
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(0);   // compuerta cerrada
  posicionActual = 0;
  pinMode(PIN_IR, INPUT);
  dht.begin();
  Wire.begin(I2C_SDA, I2C_SCL);
  tcsOK = tcs.begin();
  Serial.println(tcsOK ? "[OK] TCS34725 detectado" : "[!!] TCS34725 NO encontrado");
  iniciarCamara();
  conectarWiFi();
  imprimirConfig();     // muestra todos los pines y estados en el Serial
}
// Imprime en el Serial qué pin usa cada cosa y su estado (para revisar).
void imprimirConfig() {
  Serial.println("\n=============== CONFIGURACION (pines y estado) ===============");
  Serial.println("-- SENSORES --");
  Serial.printf("   RGB TCS34725 (I2C): SDA=%d  SCL=%d   [%s]\n",
                I2C_SDA, I2C_SCL, tcsOK ? "OK" : "NO detectado");
  Serial.printf("   Humedad DHT22     : GPIO %d\n", PIN_DHT22);
  Serial.printf("   IR FC-51          : GPIO %d  (ahora=%d, LOW=detecta)\n",
                PIN_IR, digitalRead(PIN_IR));
  Serial.println("-- CINTA (L298N #1) --");
  Serial.printf("   ENA=%d  IN1=%d  IN2->GND\n", ENA_CINTA, IN1_CINTA);
  Serial.println("-- RODILLOS (L298N #2) --");
  Serial.printf("   R1: ENA=%d  IN1=%d  IN2=%d\n", ENA_RODILLO1, IN1_RODILLO1, IN2_RODILLO1);
  Serial.printf("   R2: ENB=%d  IN3=%d  IN4=%d\n", ENB_RODILLO2, IN3_RODILLO2, IN4_RODILLO2);
  Serial.println("-- SERVO --");
  Serial.printf("   Senal=GPIO %d   (VIN=5V, GND comun)\n", PIN_SERVO);
  Serial.println("-- CAMARA (pines fijos 4-18) --");
  Serial.printf("   PSRAM: %s\n", psramFound() ? "OK" : "NO (Tools->PSRAM=OPI)");
  Serial.println("-- BACKEND / WiFi --");
  Serial.printf("   http://%s:%d\n", BACKEND_HOST, BACKEND_PORT);
  Serial.printf("   WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "CONECTADO" : "NO conectado");
  if (WiFi.status() == WL_CONNECTED) { Serial.print("   IP ESP32: "); Serial.println(WiFi.localIP()); }
  Serial.println("==============================================================\n");
}
// ════════════════════════════════════════════════════════════════════════════
void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  consultarLoteActivo();
  if (!loteActivo) { detenerTodo(); delay(POLL_LOTE_MS); return; }
  if (!cintaHastaDetectar()) return;
  procesarPalta();
  if (loteActivo) esperarLiberacionIR();
}
bool dormirVigilando(unsigned long ms) {
  unsigned long t0 = millis(), lastChk = 0;
  while (millis() - t0 < ms) {
    if (millis() - lastChk > 800) {
      lastChk = millis();
      consultarLoteActivo();
      if (!loteActivo) return false;
    }
    delay(40);
  }
  return true;
}
bool cintaHastaDetectar() {
  Serial.println("\n[CINTA] avanzando hasta detectar fruta (IR)...");
  cintaAdelante(velocidadCinta);
  unsigned long lastPoll = millis();
  while (true) {
    if (digitalRead(PIN_IR) == LOW) {
      delay(IR_DEBOUNCE_MS);
      if (digitalRead(PIN_IR) == LOW) {
        // NO paramos aún: la cinta AVANZA 5 s más para que la fruta caiga a los
        // rodillos, y RECIÉN ahí se detiene.
        Serial.printf("[IR] fruta detectada -> la cinta sigue %lu ms y para\n", tiempoCintaExtra);
        bool ok = dormirVigilando(tiempoCintaExtra);   // la cinta sigue moviéndose
        detenerCinta();
        if (ok) Serial.println("[CINTA] detenida (fruta en los rodillos)");
        return ok;
      }
    }
    if (millis() - lastPoll > POLL_LOTE_MS) {
      lastPoll = millis();
      consultarLoteActivo();
      if (!loteActivo) { detenerCinta(); Serial.println("[CINTA] lote cerrado"); return false; }
    }
    delay(20);
  }
}
void esperarLiberacionIR() {
  Serial.println("[IR] esperando que la fruta salga...");
  unsigned long t0 = millis();
  while (digitalRead(PIN_IR) == LOW && millis() - t0 < IR_RELEASE_TIMEOUT) delay(20);
  delay(500);
}
void abortarPorLoteCerrado() {
  detenerTodo();
  Serial.println("[!] Lote cerrado -> FRENO todo\n");
}
// ── Procesa la fruta: 2 s en rodillos + 3 rondas. En cada ronda: foto -> el GATE
//    decide; si ES palta toma sensores (+ el backend corre enfermedad), si NO es
//    palta se OMITE (eficiencia, no promediable). Decide con el veredicto AGREGADO ─
void procesarPalta() {
  Serial.println("\n=== Procesando fruta (3 fotos, el backend vota) ===");
  // (3) Espera con la fruta ya en los rodillos antes de empezar a girar.
  Serial.printf("[...] %lu ms con la fruta en los rodillos\n", tiempoEnRodillos);
  if (!dormirVigilando(tiempoEnRodillos)) { abortarPorLoteCerrado(); return; }

  String veredicto = "";        // veredicto AGREGADO (lo calcula el backend votando)
  int    paltaId   = -1;        // UNA palta por fruta; las 3 fotos van a la MISMA
  for (int i = 1; i <= cantidadVueltas; i++) {
    // (4a) gira ambos rodillos y detiene
    Serial.printf("\n[Ronda %d/%d] girando rodillos %lu ms\n", i, cantidadVueltas, tiempoGiroRodillos);
    if (!girarPaltaEnRodillos(tiempoGiroRodillos)) { abortarPorLoteCerrado(); return; }
    // (4b) pausa de estabilización (foto sin movimiento)
    if (!dormirVigilando(tiempoEstabilizacion)) { abortarPorLoteCerrado(); return; }
    // (4c) captura foto fresca
    camera_fb_t* fb = capturarFresca();
    if (!fb) { Serial.println("  [CAM] ERROR: sin frame -> salto ronda"); continue; }
    Serial.printf("  [CAM] foto %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    // (4d) crea la palta la 1a vez (SIN sensores: aún no sabemos si es palta)
    if (paltaId < 0) paltaId = enviarPalta();
    // (4e) sube SOLO la foto -> el GATE decide. Devuelve el veredicto de ESTA foto
    //      y (por referencia) el AGREGADO.
    String agregada = "";
    String perfoto = enviarFoto(fb, paltaId, agregada);
    esp_camera_fb_return(fb);
    if (agregada.length()) veredicto = agregada;
    // (4f) SOLO si el gate confirma que ES palta: toma sensores (el backend además
    //      corre el modelo de enfermedad). Si NO es palta -> se OMITE todo.
    bool esPaltaRonda = (perfoto.length() && perfoto != "no_es_palta");
    if (esPaltaRonda) {
      int r=-1, g=-1, b=-1; float lux=NAN, t=NAN, h=NAN;
      if (tcsOK) {
        uint16_t R,G,B,C; tcs.getRawData(&R,&G,&B,&C);
        lux = tcs.calculateLux(R,G,B); r=map8(R,C); g=map8(G,C); b=map8(B,C);
        Serial.printf("  [TCS] R=%u G=%u B=%u Lux=%.1f\n", R,G,B,lux);
      }
      t = dht.readTemperature(); h = dht.readHumidity();
      if (!isnan(t) && !isnan(h)) Serial.printf("  [DHT] T=%.1fC HR=%.1f%%\n", t, h);
      enviarSensores(paltaId, r,g,b,lux,t,h);
    } else {
      Serial.printf("  [GATE] foto='%s' -> NO es palta: OMITO sensores y enfermedad\n",
                    perfoto.length() ? perfoto.c_str() : "sin respuesta");
    }
  }
  if (!loteActivo) { abortarPorLoteCerrado(); return; }

  // ── DECISIÓN con el veredicto AGREGADO (voto de las fotos) ──────────────────
  // EXPULSA si es no_es_palta / antracnosis / scab. PASA solo si es 'sana'.
  // Sin respuesta (veredicto vacío) -> NO expulsar (no rechazar fruta buena por red).
  bool noPalta  = (veredicto == "no_es_palta");
  bool enferma  = (veredicto == "antracnosis" || veredicto == "scab");
  bool expulsar = (noPalta || enferma);
  Serial.printf("\n[MODELO] veredicto agregado = %s\n",
                veredicto.length() ? veredicto.c_str() : "(sin respuesta)");

  if (expulsar) {
    Serial.printf("[EXPULSA] %s -> compuerta 90 por %lu s + rodillos de expulsion %lu s\n",
                  noPalta ? "NO ES PALTA" : veredicto.c_str(),
                  tiempoServoArriba / 1000, tiempoExpulsion / 1000);
    moverCompuerta(ANGULO_COMPUERTA);
    if (!dormirVigilando(tiempoServoArriba)) { abortarPorLoteCerrado(); return; }
    if (!botarPalta(tiempoExpulsion)) { abortarPorLoteCerrado(); return; }
    detenerTodo();
    moverCompuerta(0);   // baja la compuerta para la siguiente
  } else {
    // 'sana' o sin respuesta -> PASA (solo rodillos hacia adelante, sin servo)
    Serial.println(veredicto.length() ? "[PASA] SANA -> la deja pasar (sin servo)"
                                       : "[PASA] sin veredicto -> la deja pasar (no rechazo por red)");
    if (!botarPalta(tiempoExpulsion)) { abortarPorLoteCerrado(); return; }
    detenerTodo();
  }
}
// ════════════════════════════════════════════════════════════════════════════
//  SERVO
// ════════════════════════════════════════════════════════════════════════════
// Movimiento suave grado a grado con la librería ESP32Servo (como antes).
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 0, 180);
  Serial.printf("  [SERVO] %d -> %d grados\n", posicionActual, nuevaPos);
  if (nuevaPos > posicionActual)
    for (int p = posicionActual; p <= nuevaPos; p++) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  else
    for (int p = posicionActual; p >= nuevaPos; p--) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  posicionActual = nuevaPos;
}
// ════════════════════════════════════════════════════════════════════════════
//  CINTA / RODILLOS
// ════════════════════════════════════════════════════════════════════════════
void cintaAdelante(int vel) {
  digitalWrite(IN1_CINTA, HIGH);
  ledcWrite(ENA_CINTA, dutyArranqueCinta);
  delay(tiempoArranqueCintaMs);
  ledcWrite(ENA_CINTA, vel);
}
void detenerCinta() { ledcWrite(ENA_CINTA, 0); digitalWrite(IN1_CINTA, LOW); }
void rodillosParaGirarPalta(int v1, int v2) {
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH); ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH); ledcWrite(ENB_RODILLO2, v2);
}
void rodillosParaBotarPalta(int v1, int v2) {
  // EXPULSIÓN = los dos rodillos en sentido OPUESTO ENTRE SÍ: así sus superficies
  // empujan la fruta en la MISMA dirección lineal (la EXPULSAN) en vez de girarla
  // en su sitio. (En evaluación los dos van IGUAL -> gira en el sitio.)
  // Si sale hacia el lado equivocado, pon EXPULSION_NORMAL = true arriba.
  if (EXPULSION_NORMAL) {
    digitalWrite(IN1_RODILLO1, HIGH); digitalWrite(IN2_RODILLO1, LOW);   // R1 ->
    digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH);  // R2 <- (opuesto a R1)
  } else {
    digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH);  // R1 <-
    digitalWrite(IN3_RODILLO2, HIGH); digitalWrite(IN4_RODILLO2, LOW);   // R2 -> (opuesto a R1)
  }
  ledcWrite(ENA_RODILLO1, v1);
  ledcWrite(ENB_RODILLO2, v2);
}
bool girarPaltaEnRodillos(unsigned long tiempoTotal) {
  rodillosParaGirarPalta(dutyArranque, dutyArranque);
  if (!dormirVigilando(tiempoArranqueMs)) { detenerRodillos(); return false; }
  rodillosParaGirarPalta(dutyLento, dutyLento);
  unsigned long resto = (tiempoTotal > (unsigned long)tiempoArranqueMs) ? tiempoTotal - tiempoArranqueMs : 0;
  bool ok = dormirVigilando(resto);
  detenerRodillos();
  return ok;
}
bool botarPalta(unsigned long tiempoTotal) {
  rodillosParaBotarPalta(potenciaBotar, potenciaBotar);
  bool ok = dormirVigilando(tiempoTotal);
  detenerRodillos();
  return ok;
}
void detenerRodillos() {
  ledcWrite(ENA_RODILLO1, 0); ledcWrite(ENB_RODILLO2, 0);
  digitalWrite(IN1_RODILLO1, LOW); digitalWrite(IN2_RODILLO1, LOW);
  digitalWrite(IN3_RODILLO2, LOW); digitalWrite(IN4_RODILLO2, LOW);
}
void detenerTodo() { detenerCinta(); detenerRodillos(); }
uint8_t map8(uint16_t canal, uint16_t clear) {
  if (clear == 0) return 0;
  long v = (long)canal * 255 / clear;
  return v > 255 ? 255 : (uint8_t)v;
}
camera_fb_t* capturarFresca() {
  for (int i = 0; i < 2; i++) {
    camera_fb_t* viejo = esp_camera_fb_get();
    if (viejo) esp_camera_fb_return(viejo);
    delay(60);
  }
  return esp_camera_fb_get();
}
// ════════════════════════════════════════════════════════════════════════════
//  BACKEND
// ════════════════════════════════════════════════════════════════════════════
void consultarLoteActivo() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/lote/activo";
  http.begin(url); http.setTimeout(4000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    if (body.indexOf("\"activo\":true") >= 0) {
      loteActivo = true;
      int i = body.indexOf("\"id\":");
      if (i >= 0) loteId = body.substring(i + 5).toInt();
    } else { loteActivo = false; loteId = -1; }
  } else {
    Serial.printf("[NET] /api/lote/activo HTTP %d\n", code);
  }
  http.end();
}
// POST /api/palta -> crea la palta (SIN sensores: aún no sabemos si es palta; los
// sensores se mandan luego con enviarSensores SOLO si el gate confirma que es palta).
// clasificacion = null: la decide el MODELO en el backend con las fotos.
int enviarPalta() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  char body[160];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":null,\"confianza\":null,"
    "\"ir_detectado\":true,\"lecturas\":%d}",
    loteId, cantidadVueltas);
  int paltaId = -1;
  int code = http.POST((uint8_t*)body, strlen(body));
  if (code == 200) {
    String resp = http.getString();
    int i = resp.indexOf("\"palta_id\":");
    if (i >= 0) paltaId = resp.substring(i + 11).toInt();
    Serial.printf("  [NET] POST /api/palta OK palta_id=%d\n", paltaId);
  } else {
    Serial.printf("  [NET] POST /api/palta HTTP %d\n", code);
  }
  http.end();
  return paltaId;
}
// POST /api/captura/foto -> sube SOLO la foto. DEVUELVE el veredicto de ESTA foto
// (per-foto, para el gate de la ronda) y por REFERENCIA el AGREGADO (voto).
String enviarFoto(camera_fb_t* fb, int paltaId, String& agregadaOut) {
  String perfoto = "";
  agregadaOut = "";
  if (!fb || WiFi.status() != WL_CONNECTED) return perfoto;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/captura/foto?palta_id=" + paltaId + "&lote_id=" + loteId;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);
  if (code == 200) {
    String resp = http.getString();
    // veredicto de ESTA foto (decide si tomamos sensores en la ronda)
    int i = resp.indexOf("\"clasificacion\":\"");
    if (i >= 0) { int s = i + 17; int e = resp.indexOf("\"", s); if (e > s) perfoto = resp.substring(s, e); }
    // veredicto AGREGADO (voto de las fotos) -> decisión final tras la 3a
    int j = resp.indexOf("\"clasificacion_agregada\":\"");
    if (j >= 0) { int s = j + 26; int e = resp.indexOf("\"", s); if (e > s) agregadaOut = resp.substring(s, e); }
    Serial.printf("  [NET] POST foto OK (%u bytes) foto=%s agregado=%s\n",
                  fb->len, perfoto.length() ? perfoto.c_str() : "?",
                  agregadaOut.length() ? agregadaOut.c_str() : "?");
  } else {
    Serial.printf("  [NET] POST foto HTTP %d\n", code);
  }
  http.end();
  return perfoto;
}
// POST /api/palta/{id}/sensores -> manda las lecturas de la ronda. Se llama SOLO
// cuando el gate confirmó que es palta (los frames no_es_palta no mandan sensores).
void enviarSensores(int paltaId, int r, int g, int b, float lux, float t, float hr) {
  if (WiFi.status() != WL_CONNECTED || paltaId < 0) return;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/palta/" + paltaId + "/sensores";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  char rS[8],gS[8],bS[8],luxS[16],tS[16],hrS[16];
  if (r < 0) strcpy(rS, "null"); else snprintf(rS, sizeof(rS), "%d", r);
  if (g < 0) strcpy(gS, "null"); else snprintf(gS, sizeof(gS), "%d", g);
  if (b < 0) strcpy(bS, "null"); else snprintf(bS, sizeof(bS), "%d", b);
  if (isnan(lux)) strcpy(luxS, "null"); else snprintf(luxS, sizeof(luxS), "%.1f", lux);
  if (isnan(t))   strcpy(tS, "null");   else snprintf(tS, sizeof(tS), "%.1f", t);
  if (isnan(hr))  strcpy(hrS, "null");  else snprintf(hrS, sizeof(hrS), "%.1f", hr);
  char body[200];
  snprintf(body, sizeof(body),
    "{\"r\":%s,\"g\":%s,\"b\":%s,\"lux\":%s,\"temp\":%s,\"humedad\":%s}",
    rS,gS,bS,luxS,tS,hrS);
  int code = http.POST((uint8_t*)body, strlen(body));
  Serial.printf("  [NET] POST sensores palta %d HTTP %d\n", paltaId, code);
  http.end();
}
// ════════════════════════════════════════════════════════════════════════════
//  WiFi + Cámara
// ════════════════════════════════════════════════════════════════════════════
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.printf("[WiFi] conectando a \"%s\"", SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.print("\n[WiFi] CONECTADO  IP: "); Serial.println(WiFi.localIP());
}
void iniciarCamara() {
  bool hayPSRAM = psramFound();
  Serial.printf("[CAM] PSRAM %s\n", hayPSRAM ? "OK" : "NO (Tools->PSRAM=OPI!)");
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0=CAM_PIN_D0; c.pin_d1=CAM_PIN_D1; c.pin_d2=CAM_PIN_D2; c.pin_d3=CAM_PIN_D3;
  c.pin_d4=CAM_PIN_D4; c.pin_d5=CAM_PIN_D5; c.pin_d6=CAM_PIN_D6; c.pin_d7=CAM_PIN_D7;
  c.pin_xclk=CAM_PIN_XCLK; c.pin_pclk=CAM_PIN_PCLK;
  c.pin_vsync=CAM_PIN_VSYNC; c.pin_href=CAM_PIN_HREF;
  c.pin_sccb_sda=CAM_PIN_SIOD; c.pin_sccb_scl=CAM_PIN_SIOC;
  c.pin_pwdn=CAM_PIN_PWDN; c.pin_reset=CAM_PIN_RESET;
  c.xclk_freq_hz=20000000; c.pixel_format=PIXFORMAT_JPEG; c.jpeg_quality=12;
  c.frame_size = FRAMESIZE_240X240;
  if (hayPSRAM) {
    c.fb_count=2; c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_LATEST;
  } else {
    c.fb_count=1; c.fb_location=CAMERA_FB_IN_DRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
  }
  esp_err_t err = esp_camera_init(&c);
  Serial.println(err == ESP_OK ? "[CAM] inicializada (240x240)" : "[CAM] ERROR init");
}
