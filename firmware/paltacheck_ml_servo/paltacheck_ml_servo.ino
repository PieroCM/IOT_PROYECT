/* ============================================================================
   PaltaCheck — FLUJO FINAL con MODELO (IA) + SERVO   (ESP32-S3 N16R8)
   ----------------------------------------------------------------------------
   El MODELO corre en el BACKEND. El ESP32 manda la foto y LEE el veredicto
   que devuelve /api/captura/foto, y con eso decide la compuerta:
       enfermedad (antracnosis/scab) -> SERVO ABRE (rechazo)
       healthy (sana)                -> compuerta cerrada, la deja pasar

   Flujo (mientras haya lote activo):
     1) Cinta avanza HASTA que el IR detecta la fruta -> para -> 3 s.
     2) RODILLOS x3: giran -> 5 s -> FOTO ACTUAL -> backend clasifica -> 5 s.
     3) Según el veredicto del modelo: abre servo (enferma) o no (healthy).
     4) Expulsa al MAXIMO y (si abrió) cierra la compuerta.
     5) Si se cierra el lote, frena todo.

   Requiere ESP32Servo. Servo en GPIO 3 (5 V aparte + GND común).
   El backend debe estar reconstruido con el modelo:  docker compose up -d --build backend
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Wire.h>
#include <DHT.h>
#include "Adafruit_TCS34725.h"

// ─── CONFIG WiFi + Backend ──────────────────────────────────────────────────
const char* SSID         = "Piero";
const char* PASSWORD     = "12345678";
const char* BACKEND_HOST = "10.116.108.254";
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
const int PIN_SERVO        = 3;     // señal del servo (coincide con el cableado actual)
const int ANGULO_COMPUERTA = 90;
const int SERVO_FREQ       = 50;    // Hz (servo estándar)
const int SERVO_RES        = 16;    // bits de resolución del PWM del servo
int  posicionActual = 0;

// ─── PWM motores ────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;

// ─── Velocidades / tiempos ──────────────────────────────────────────────────
const int velocidadCinta            = 100;
const int dutyArranqueCinta         = 180;
const int tiempoArranqueCintaMs     = 150;
const int potenciaBotar             = 255;
const int dutyArranque              = 200;
const int dutyLento                 = 120;
const int tiempoArranqueMs          = 200;
const unsigned long tiempoGiroRodillos = 3000;
const unsigned long tiempoEsperaFoto    = 5000;
const unsigned long tiempoEntreVueltas  = 5000;
const unsigned long tiempoServoArriba   = 10000; // 10 s con la compuerta levantada (90°)
const unsigned long tiempoExpulsion     = 5000;  // 5 s de rodillos de expulsión con fuerza
const int cantidadVueltas               = 3;
const int IR_DEBOUNCE_MS        = 60;
const unsigned long IR_RELEASE_TIMEOUT = 10000;
const unsigned long tiempoPasoARodillos = 3000;

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
  Serial.println("\n===== FLUJO FINAL: Modelo IA + Servo =====");

  pinMode(IN1_CINTA, OUTPUT);
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  pinMode(IN1_RODILLO1, OUTPUT); pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT); pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerTodo();

  // Servo por LEDC DIRECTO (50 Hz), igual que los motores. Esto evita el
  // conflicto de timers de ESP32Servo con la cámara, que hacía temblar/girar
  // loco al servo. Así solo se posiciona y se MANTIENE quieto.
  ledcAttach(PIN_SERVO, SERVO_FREQ, SERVO_RES);
  servoEscribir(0);          // compuerta cerrada
  posicionActual = 0;

  pinMode(PIN_IR, INPUT);
  dht.begin();
  Wire.begin(I2C_SDA, I2C_SCL);
  tcsOK = tcs.begin();
  Serial.println(tcsOK ? "[OK] TCS34725 detectado" : "[!!] TCS34725 NO encontrado");

  iniciarCamara();
  conectarWiFi();
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
        detenerCinta();
        Serial.println("[IR] fruta detectada -> cinta PARA");
        Serial.println("[...] 3 s para que la fruta pase a los rodillos");
        return dormirVigilando(tiempoPasoARodillos);
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

// ── Procesa la fruta: rodillos x3 + foto -> el MODELO decide -> servo/bote ───
void procesarPalta() {
  Serial.println("\n=== Procesando fruta (el modelo decide) ===");
  String ultimoVeredicto = "";   // veredicto AGREGADO (mayoría de las 3 fotos)
  int   paltaId = -1;            // UNA sola palta por fruta; las 3 fotos van a ella

  for (int i = 1; i <= cantidadVueltas; i++) {
    Serial.printf("\n[Vuelta %d/%d] girando (ambos rodillos)\n", i, cantidadVueltas);
    if (!girarPaltaEnRodillos(tiempoGiroRodillos)) { abortarPorLoteCerrado(); return; }
    detenerRodillos();

    Serial.println("  espera 5 s, luego foto");
    if (!dormirVigilando(tiempoEsperaFoto)) { abortarPorLoteCerrado(); return; }

    int r=-1, g=-1, b=-1; float lux=NAN, t=NAN, h=NAN;
    if (tcsOK) {
      uint16_t R,G,B,C; tcs.getRawData(&R,&G,&B,&C);
      lux = tcs.calculateLux(R,G,B); r=map8(R,C); g=map8(G,C); b=map8(B,C);
      Serial.printf("  [TCS] R=%u G=%u B=%u Lux=%.1f\n", R,G,B,lux);
    }
    t = dht.readTemperature(); h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) Serial.printf("  [DHT] T=%.1fC HR=%.1f%%\n", t, h);

    camera_fb_t* fb = capturarFresca();
    if (fb) Serial.printf("  [CAM] foto %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    else    Serial.println("  [CAM] ERROR: sin frame");

    if (paltaId < 0) paltaId = enviarPalta(r,g,b,lux,t,h);  // crea la palta UNA vez
    if (fb) {
      String v = enviarFoto(fb, paltaId);              // sube esta foto a la MISMA palta
      if (v.length()) ultimoVeredicto = v;             // veredicto agregado por votos
      esp_camera_fb_return(fb);
    }

    if (i < cantidadVueltas) {
      Serial.println("  espera 5 s para volver a girar");
      if (!dormirVigilando(tiempoEntreVueltas)) { abortarPorLoteCerrado(); return; }
    }
  }

  if (!loteActivo) { abortarPorLoteCerrado(); return; }

  // DECISIÓN según el modelo (veredicto de la última foto)
  bool enferma = (ultimoVeredicto == "antracnosis");
  Serial.printf("\n[MODELO] veredicto = %s\n", ultimoVeredicto.length() ? ultimoVeredicto.c_str() : "(sin respuesta)");

  if (enferma) {
    // Rechazo: levanta la compuerta 90° por 10 s, luego expulsa 5 s con fuerza
    Serial.println("[SERVO] ENFERMA -> levanta compuerta 90 por 10 s");
    moverCompuerta(ANGULO_COMPUERTA);
    if (!dormirVigilando(tiempoServoArriba)) { abortarPorLoteCerrado(); return; }

    Serial.println("[Botar] rodillos de expulsion 5 s con fuerza");
    if (!botarPalta(tiempoExpulsion)) { abortarPorLoteCerrado(); return; }
    detenerTodo();

    moverCompuerta(0);   // baja la compuerta para la siguiente
  } else {
    // Healthy: la deja pasar (sin servo)
    Serial.println("[OK] HEALTHY -> la deja pasar (expulsa sin servo)");
    if (!botarPalta(tiempoExpulsion)) { abortarPorLoteCerrado(); return; }
    detenerTodo();
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SERVO
// ════════════════════════════════════════════════════════════════════════════
// Escribe un ángulo (0-180) al servo vía LEDC (pulso 500-2400 us dentro de 20 ms).
void servoEscribir(int ang) {
  ang = constrain(ang, 0, 180);
  int us = map(ang, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << SERVO_RES) - 1) / 20000UL);
  ledcWrite(PIN_SERVO, duty);
}

// Posiciona la compuerta y la MANTIENE ahí (sin barrido, sin oscilar).
void moverCompuerta(int nuevaPos) {
  Serial.printf("  [SERVO] -> %d grados\n", nuevaPos);
  servoEscribir(nuevaPos);
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
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH); ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH); ledcWrite(ENB_RODILLO2, v2);
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

// POST /api/palta. clasificacion = null: la decide el MODELO en el backend.
int enviarPalta(int r, int g, int b, float lux, float t, float hr) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  char rS[8],gS[8],bS[8],luxS[16],tS[16],hrS[16];
  if (r < 0) strcpy(rS, "null"); else snprintf(rS, sizeof(rS), "%d", r);
  if (g < 0) strcpy(gS, "null"); else snprintf(gS, sizeof(gS), "%d", g);
  if (b < 0) strcpy(bS, "null"); else snprintf(bS, sizeof(bS), "%d", b);
  if (isnan(lux)) strcpy(luxS, "null"); else snprintf(luxS, sizeof(luxS), "%.1f", lux);
  if (isnan(t))   strcpy(tS, "null");   else snprintf(tS, sizeof(tS), "%.1f", t);
  if (isnan(hr))  strcpy(hrS, "null");  else snprintf(hrS, sizeof(hrS), "%.1f", hr);

  char body[300];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":null,\"confianza\":null,"
    "\"r\":%s,\"g\":%s,\"b\":%s,\"lux\":%s,"
    "\"temp\":%s,\"humedad\":%s,\"ir_detectado\":true,\"lecturas\":%d}",
    loteId, rS,gS,bS,luxS, tS,hrS, cantidadVueltas);

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

// POST /api/captura/foto -> devuelve el veredicto del modelo ("sana"/"antracnosis")
String enviarFoto(camera_fb_t* fb, int paltaId) {
  String veredicto = "";
  if (!fb || WiFi.status() != WL_CONNECTED) return veredicto;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/captura/foto?palta_id=" + paltaId + "&lote_id=" + loteId;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);
  if (code == 200) {
    String resp = http.getString();
    int i = resp.indexOf("\"clasificacion\":\"");
    if (i >= 0) {
      int s = i + 17;                       // largo de  "clasificacion":"
      int e = resp.indexOf("\"", s);
      if (e > s) veredicto = resp.substring(s, e);
    }
    Serial.printf("  [NET] POST foto OK (%u bytes) veredicto=%s\n",
                  fb->len, veredicto.length() ? veredicto.c_str() : "?");
  } else {
    Serial.printf("  [NET] POST foto HTTP %d\n", code);
  }
  http.end();
  return veredicto;
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
