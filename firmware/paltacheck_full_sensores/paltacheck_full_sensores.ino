/* ============================================================================
   PaltaCheck — FLUJO COMPLETO + SENSORES
   CINTA + RODILLOS + (RGB/DHT22/IR) + FOTO + SERVO + BOTE   (ESP32-S3 N16R8)
   Script independiente (no toca los anteriores).
   ----------------------------------------------------------------------------
   Por cada lote abierto desde el dashboard procesa 2 paltas seguidas:
       Palta 1 = BUENA (no mueve servo)   ·   Palta 2 = MALA (abre compuerta)

   Por cada palta:
     1) CINTA adelante 5 s -> para
     2) Espera 2 s
     3) RODILLOS x3 vueltas: giro lento -> para -> FOTO + lee sensores
        (acumula RGB/lux/temp/humedad de las 3 vueltas y los PROMEDIA)
     4) Manda al backend UNA palta con los sensores promediados + la última foto
     5) DECISIÓN compuerta (servo GPIO 3): BUENA cerrada / MALA abre 90°
     6) BOTE final

   LIBRERÍAS a instalar en el Arduino IDE (Administrar Bibliotecas):
     · ESP32Servo
     · Adafruit TCS34725
     · DHT sensor library (Adafruit)   + Adafruit Unified Sensor (dependencia)

   ⚠️ Servo y sensores con 5V/3V3 estables y GND común. 13V solo a los motores.
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <ESP32Servo.h>
#include <Wire.h>
#include <DHT.h>
#include "Adafruit_TCS34725.h"

// ─── CONFIG WiFi + Backend ──────────────────────────────────────────────────
const char* SSID         = "Piero";
const char* PASSWORD     = "12345678";
const char* BACKEND_HOST = "10.207.55.254";
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
#define I2C_SDA    14     // TCS34725
#define I2C_SCL    21
#define PIN_DHT22   2     // DHT22 DATA (temp + humedad)
#define PIN_IR      1     // FC-51 OUT (LOW = objeto detectado)

// ─── PINES CINTA (L298N #1) ─────────────────────────────────────────────────
const int ENA_CINTA = 46;
const int IN1_CINTA = 48;     // IN2 -> GND directo (cinta solo adelante)

// ─── PINES RODILLOS (L298N #2) ──────────────────────────────────────────────
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;

// ─── SERVO compuerta ────────────────────────────────────────────────────────
const int PIN_SERVO          = 3;
const int ANGULO_COMPUERTA   = 90;
const int velocidadMovimiento = 3;
Servo servoCompuerta;
int  posicionActual = 0;

// ─── PWM motores ────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;

// ─── Velocidades / tiempos ──────────────────────────────────────────────────
const int velocidadCinta            = 100;
const int dutyArranqueCinta         = 180;
const int tiempoArranqueCintaMs     = 150;
const unsigned long tiempoCinta     = 5000;
const unsigned long tiempoEspera    = 2000;
const int potenciaBotar             = 120;
const int dutyArranque              = 160;
const int dutyLento                 = 70;
const int tiempoArranqueMs          = 150;
const unsigned long tiempoGiroRodillos = 3000;
const unsigned long tiempoBotar         = 4000;
const int cantidadVueltas               = 3;
const unsigned long tiempoAsientaFoto   = 600;

// ─── Sensores ───────────────────────────────────────────────────────────────
DHT dht(PIN_DHT22, DHT22);
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
bool tcsOK = false;

// ─── WiFi/Backend ───────────────────────────────────────────────────────────
const unsigned long POLL_LOTE_MS = 5000;
bool loteActivo  = false;
int  loteId      = -1;
bool yaProcesado = false;

// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== FLUJO COMPLETO + SENSORES =====");

  // Cinta
  pinMode(IN1_CINTA, OUTPUT);
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  // Rodillos
  pinMode(IN1_RODILLO1, OUTPUT); pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT); pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerTodo();

  // Servo
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(0);
  posicionActual = 0;

  // Sensores
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

  if (loteActivo && !yaProcesado) {
    procesarLote();
    yaProcesado = true;
  }
  if (!loteActivo) yaProcesado = false;

  delay(POLL_LOTE_MS);
}

// ════════════════════════════════════════════════════════════════════════════
void procesarLote() {
  Serial.printf("\n>>> LOTE %d ACTIVO — 2 flujos seguidos\n", loteId);
  procesarPalta(true);    // BUENA
  Serial.println("\n--- Siguiente palta en 3 s ---");
  delay(3000);
  procesarPalta(false);   // MALA
  Serial.println("\n[OK] LOTE COMPLETO — revisa el dashboard\n");
}

// Una palta: cinta -> rodillos+fotos+sensores -> backend -> servo -> bote.
void procesarPalta(bool esBuena) {
  Serial.printf("\n=== Palta %s ===\n", esBuena ? "BUENA" : "MALA");

  // 1) Cinta 5 s
  Serial.println("[1] Cinta adelante 5 s");
  cintaAdelante(velocidadCinta);
  delay(tiempoCinta);
  detenerCinta();

  // 2) Espera 2 s
  Serial.println("[2] Espera 2 s");
  delay(tiempoEspera);

  // 3) Rodillos x3 con foto + lectura de sensores (acumular para promediar)
  float sumR=0, sumG=0, sumB=0, sumLux=0, sumT=0, sumHR=0;
  int   nColor=0, nDHT=0;
  camera_fb_t* ultimaFoto = nullptr;

  for (int i = 1; i <= cantidadVueltas; i++) {
    Serial.printf("\n[Vuelta %d/%d] girando palta\n", i, cantidadVueltas);
    girarPaltaEnRodillos(tiempoGiroRodillos);
    detenerRodillos();
    delay(tiempoAsientaFoto);

    // Foto (guardamos la última para subirla)
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("  [CAM] %u bytes\n", fb->len);
      if (ultimaFoto) esp_camera_fb_return(ultimaFoto);
      ultimaFoto = fb;
    } else {
      Serial.println("  [CAM] ERROR: sin frame");
    }

    // TCS34725 (RGB + lux)
    if (tcsOK) {
      uint16_t r,g,b,c;
      tcs.getRawData(&r,&g,&b,&c);
      float lux = tcs.calculateLux(r,g,b);
      sumR += map8(r,c); sumG += map8(g,c); sumB += map8(b,c); sumLux += lux;
      nColor++;
      Serial.printf("  [TCS] R=%u G=%u B=%u Lux=%.1f\n", r,g,b,lux);
    }
    // DHT22 (temp + humedad)
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) { sumT += t; sumHR += h; nDHT++;
      Serial.printf("  [DHT] T=%.1fC HR=%.1f%%\n", t, h);
    } else {
      Serial.println("  [DHT] lectura invalida (NaN)");
    }
  }

  // Promedios
  int   rP = nColor ? (int)(sumR/nColor) : -1;
  int   gP = nColor ? (int)(sumG/nColor) : -1;
  int   bP = nColor ? (int)(sumB/nColor) : -1;
  float luxP = nColor ? sumLux/nColor : NAN;
  float tP   = nDHT ? sumT/nDHT  : NAN;
  float hrP  = nDHT ? sumHR/nDHT : NAN;
  bool  ir   = (digitalRead(PIN_IR) == LOW);   // FC-51: LOW = detectado

  Serial.printf("\n[PROMEDIO] RGB=(%d,%d,%d) Lux=%.1f T=%.1f HR=%.1f IR=%d\n",
                rP,gP,bP,luxP,tP,hrP,ir);

  // 4) Backend: UNA palta con sensores + la última foto
  int paltaId = enviarPalta(esBuena, rP,gP,bP,luxP,tP,hrP,ir);
  if (ultimaFoto) { enviarFoto(ultimaFoto, paltaId); esp_camera_fb_return(ultimaFoto); }

  // 5) Compuerta
  if (esBuena) {
    Serial.println("[SERVO] BUENA -> compuerta cerrada");
  } else {
    Serial.println("[SERVO] MALA -> abre compuerta");
    moverCompuerta(ANGULO_COMPUERTA);
    delay(800);
  }

  // 6) Bote final
  Serial.println("[Botar] expulsando palta");
  botarPalta(tiempoBotar);
  detenerTodo();
  if (!esBuena) moverCompuerta(0);
}

// ════════════════════════════════════════════════════════════════════════════
//  SERVO
// ════════════════════════════════════════════════════════════════════════════
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 0, 180);
  Serial.printf("  [SERVO] %d -> %d grados\n", posicionActual, nuevaPos);
  if (nuevaPos > posicionActual)
    for (int p=posicionActual; p<=nuevaPos; p++){ servoCompuerta.write(p); delay(velocidadMovimiento);}
  else
    for (int p=posicionActual; p>=nuevaPos; p--){ servoCompuerta.write(p); delay(velocidadMovimiento);}
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
  digitalWrite(IN3_RODILLO2, HIGH); digitalWrite(IN4_RODILLO2, LOW);  ledcWrite(ENB_RODILLO2, v2);
}
void girarPaltaEnRodillos(unsigned long tiempoTotal) {
  rodillosParaGirarPalta(dutyArranque, dutyArranque);
  delay(tiempoArranqueMs);
  rodillosParaGirarPalta(dutyLento, dutyLento);
  if (tiempoTotal > (unsigned long)tiempoArranqueMs) delay(tiempoTotal - tiempoArranqueMs);
  detenerRodillos();
}
void botarPalta(unsigned long tiempoTotal) {
  rodillosParaBotarPalta(potenciaBotar, potenciaBotar);
  delay(tiempoTotal);
  detenerRodillos();
}
void detenerRodillos() {
  ledcWrite(ENA_RODILLO1, 0); ledcWrite(ENB_RODILLO2, 0);
  digitalWrite(IN1_RODILLO1, LOW); digitalWrite(IN2_RODILLO1, LOW);
  digitalWrite(IN3_RODILLO2, LOW); digitalWrite(IN4_RODILLO2, LOW);
}
void detenerTodo() { detenerCinta(); detenerRodillos(); }

// Convierte canal crudo TCS (0-65535) a 0-255 normalizado por Clear
uint8_t map8(uint16_t canal, uint16_t clear) {
  if (clear == 0) return 0;
  long v = (long)canal * 255 / clear;
  return v > 255 ? 255 : (uint8_t)v;
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

// POST /api/palta con sensores reales. -1 en r/g/b o NaN en floats => "null".
int enviarPalta(bool esBuena, int r, int g, int b, float lux, float t, float hr, bool ir) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  char rS[8], gS[8], bS[8], luxS[16], tS[16], hrS[16];
  if (r < 0) strcpy(rS, "null"); else snprintf(rS, sizeof(rS), "%d", r);
  if (g < 0) strcpy(gS, "null"); else snprintf(gS, sizeof(gS), "%d", g);
  if (b < 0) strcpy(bS, "null"); else snprintf(bS, sizeof(bS), "%d", b);
  if (isnan(lux)) strcpy(luxS, "null"); else snprintf(luxS, sizeof(luxS), "%.1f", lux);
  if (isnan(t))   strcpy(tS, "null");   else snprintf(tS, sizeof(tS), "%.1f", t);
  if (isnan(hr))  strcpy(hrS, "null");  else snprintf(hrS, sizeof(hrS), "%.1f", hr);
  const char* clase = esBuena ? "\"sana\"" : "\"antracnosis\"";

  char body[300];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":%s,\"confianza\":1.0,"
    "\"r\":%s,\"g\":%s,\"b\":%s,\"lux\":%s,"
    "\"temp\":%s,\"humedad\":%s,\"ir_detectado\":%s,\"lecturas\":%d}",
    loteId, clase, rS,gS,bS,luxS, tS,hrS, ir?"true":"false", cantidadVueltas);

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
  if (hayPSRAM) {
    c.frame_size=FRAMESIZE_VGA; c.fb_count=2;
    c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_LATEST;
  } else {
    c.frame_size=FRAMESIZE_QVGA; c.fb_count=1;
    c.fb_location=CAMERA_FB_IN_DRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
  }
  esp_err_t err = esp_camera_init(&c);
  Serial.println(err == ESP_OK ? "[CAM] inicializada" : "[CAM] ERROR init");
}
