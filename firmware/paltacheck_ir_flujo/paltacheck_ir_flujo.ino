/* ============================================================================
   PaltaCheck — FLUJO con IR + SENSORES + FOTO   (ESP32-S3 N16R8)
   Script independiente. SIN servo (todo pasa BUENA). FOTO CUADRADA.
   ----------------------------------------------------------------------------
   Flujo real (mientras haya lote activo):
     1) La CINTA avanza HASTA que el IR (FC-51) detecta la fruta.
     2) Cinta PARA -> espera 3 s (la fruta pasa a los rodillos).
     3) RODILLOS (AMBOS) x3 vueltas: giro -> para -> lee TCS(RGB/lux) y DHT(temp/hum).
     4) Toma UNA foto, promedia sensores y manda al backend (palta "sana" + foto + IR).
     5) BOTE final y espera a que la fruta salga del IR.
     6) Si se CIERRA el lote en cualquier momento, FRENA todo y vuelve a esperar.

   Notas de esta versión:
     · Foto CUADRADA 240x240 (FRAMESIZE_240X240).
     · Se toma UNA sola foto por fruta (antes fallaba "sin frame" en las vueltas
       2 y 3 porque con 1 framebuffer no se puede retener la foto entre vueltas).
     · Rodillos con más fuerza (duty más alto) para que giren AMBOS.
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
const char* BACKEND_HOST = "10.207.55.254";   // IP de tu laptop (ipconfig -> IPv4 del Wi-Fi)
const int   BACKEND_PORT = 8000;

// ─── PINES CÁMARA (modelo EYE, fijos — NO tocar) ────────────────────────────
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
#define PIN_IR      1     // FC-51 OUT (LOW = detectado)

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

// ─── PWM motores ────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;

// ─── Velocidades / tiempos (MÁS FUERZA al rotar) ────────────────────────────
const int velocidadCinta            = 100;
const int dutyArranqueCinta         = 180;
const int tiempoArranqueCintaMs     = 150;
const int potenciaBotar             = 255;   // MAXIMO para expulsar
const int dutyArranque              = 200;   // patada — vence fricción
const int dutyLento                 = 120;   // giro sostenido con fuerza
const int tiempoArranqueMs          = 200;
const unsigned long tiempoGiroRodillos = 3000;   // cuánto giran los rodillos cada vuelta
const unsigned long tiempoEsperaFoto    = 5000;  // 5 s tras girar, antes de la foto
const unsigned long tiempoEntreVueltas  = 5000;  // 5 s tras la foto, antes de volver a girar
const unsigned long tiempoBotar         = 4000;  // expulsión final
const int cantidadVueltas               = 3;
// IR
const int IR_DEBOUNCE_MS        = 60;
const unsigned long IR_RELEASE_TIMEOUT = 10000;
const unsigned long tiempoPasoARodillos = 3000;  // 3 s: la fruta pasa de la cinta a los rodillos

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
  Serial.println("\n===== FLUJO IR + Sensores + Foto (cuadrada) =====");

  pinMode(IN1_CINTA, OUTPUT);
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  pinMode(IN1_RODILLO1, OUTPUT); pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT); pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerTodo();

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

  if (!cintaHastaDetectar()) return;   // avanza cinta hasta IR (o lote cerrado)
  procesarPalta();                     // rodillos + sensores + foto (siempre sana)
  if (loteActivo) esperarLiberacionIR();
}

// ── Duerme 'ms' vigilando el lote. Devuelve false si se CIERRA el lote. ──────
bool dormirVigilando(unsigned long ms) {
  unsigned long t0 = millis(), lastChk = 0;
  while (millis() - t0 < ms) {
    if (millis() - lastChk > 800) {          // revisa el backend ~cada 0.8 s
      lastChk = millis();
      consultarLoteActivo();
      if (!loteActivo) return false;         // lote cerrado -> abortar
    }
    delay(40);
  }
  return true;
}

// ── La cinta avanza hasta que el IR detecte. false si se cierra el lote. ─────
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
        return dormirVigilando(tiempoPasoARodillos);   // 3 s interrumpibles
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
  Serial.println("[!] Lote cerrado -> FRENO todo y vuelvo a esperar\n");
}

// ── Procesa la fruta: rodillos x3 + sensores -> 1 foto -> backend -> bote ────
void procesarPalta() {
  Serial.println("\n=== Procesando fruta (BUENA) ===");

  for (int i = 1; i <= cantidadVueltas; i++) {
    // 1) Giran los rodillos
    Serial.printf("\n[Vuelta %d/%d] girando (ambos rodillos)\n", i, cantidadVueltas);
    if (!girarPaltaEnRodillos(tiempoGiroRodillos)) { abortarPorLoteCerrado(); return; }
    detenerRodillos();

    // 2) Espera 5 s y toma foto
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

    camera_fb_t* fb = capturarFresca();   // foto ACTUAL (descarta frames viejos)
    if (fb) Serial.printf("  [CAM] foto %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    else    Serial.println("  [CAM] ERROR: sin frame");

    // Cada vuelta crea su palta con su foto -> el dashboard muestra las 3
    int paltaId = enviarPalta(r,g,b,lux,t,h);
    if (fb) { enviarFoto(fb, paltaId); esp_camera_fb_return(fb); }

    // 3) Espera 3 s antes de volver a girar (no en la última vuelta)
    if (i < cantidadVueltas) {
      Serial.println("  espera 3 s para volver a girar");
      if (!dormirVigilando(tiempoEntreVueltas)) { abortarPorLoteCerrado(); return; }
    }
  }

  // 4) Expulsar: ambos rodillos al MAXIMO en el MISMO sentido
  if (!loteActivo) { abortarPorLoteCerrado(); return; }
  Serial.println("\n[Botar] expulsando al MAXIMO (ambos mismo sentido)");
  if (!botarPalta(tiempoBotar)) { abortarPorLoteCerrado(); return; }
  detenerTodo();
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

// AMBOS rodillos giran (canal A = rodillo 1, canal B = rodillo 2)
void rodillosParaGirarPalta(int v1, int v2) {
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH); ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH); ledcWrite(ENB_RODILLO2, v2);
}
void rodillosParaBotarPalta(int v1, int v2) {
  // Ambos rodillos en el MISMO sentido (igual que el giro) a máxima potencia
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH); ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH); ledcWrite(ENB_RODILLO2, v2);
}
// Devuelve false si se cierra el lote mientras gira.
bool girarPaltaEnRodillos(unsigned long tiempoTotal) {
  rodillosParaGirarPalta(dutyArranque, dutyArranque);          // patada (ambos)
  if (!dormirVigilando(tiempoArranqueMs)) { detenerRodillos(); return false; }
  rodillosParaGirarPalta(dutyLento, dutyLento);                // giro sostenido (ambos)
  unsigned long resto = (tiempoTotal > (unsigned long)tiempoArranqueMs)
                        ? tiempoTotal - tiempoArranqueMs : 0;
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

// Devuelve un frame ACTUAL: descarta los frames viejos del buffer primero
// (si no, la 1ra captura tras una espera sale con una imagen anterior).
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
    "{\"lote_id\":%d,\"clasificacion\":\"sana\",\"confianza\":1.0,"
    "\"r\":%s,\"g\":%s,\"b\":%s,\"lux\":%s,"
    "\"temp\":%s,\"humedad\":%s,\"ir_detectado\":true,\"lecturas\":%d}",
    loteId, rS,gS,bS,luxS, tS,hrS, cantidadVueltas);

  int paltaId = -1;
  int code = http.POST((uint8_t*)body, strlen(body));
  if (code == 200) {
    String resp = http.getString();
    int i = resp.indexOf("\"palta_id\":");
    if (i >= 0) paltaId = resp.substring(i + 11).toInt();
    Serial.printf("  [NET] POST /api/palta OK  palta_id=%d\n", paltaId);
  } else {
    Serial.printf("  [NET] POST /api/palta HTTP %d\n", code);
  }
  http.end();
  return paltaId;
}

void enviarFoto(camera_fb_t* fb, int paltaId) {
  if (!fb || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/captura/foto?palta_id=" + paltaId + "&lote_id=" + loteId;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);
  if (code == 200) Serial.printf("  [NET] POST foto OK (%u bytes)\n", fb->len);
  else             Serial.printf("  [NET] POST foto HTTP %d\n", code);
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
  // FOTO CUADRADA 240x240. Entra con o sin PSRAM (mejor con PSRAM=OPI).
  c.frame_size = FRAMESIZE_240X240;
  if (hayPSRAM) {
    c.fb_count=2; c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_LATEST;
  } else {
    c.fb_count=1; c.fb_location=CAMERA_FB_IN_DRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
  }
  esp_err_t err = esp_camera_init(&c);
  Serial.println(err == ESP_OK ? "[CAM] inicializada (240x240 cuadrada)" : "[CAM] ERROR init");
}
