/* ============================================================================
   PaltaCheck — PRUEBA: RODILLOS + CÁMARA + DASHBOARD   (ESP32-S3-CAM N16R8)
   ----------------------------------------------------------------------------
   Usa TU lógica de rotación con pulsos. La CINTA queda FUERA de esta prueba
   (tus pines de cinta 46/48/35 incluyen el 35 = PSRAM, que rompe la cámara).
   Solo: rodillos + cámara + envío al backend para verlo en LoteActivo.vue.

   Flujo (por cada lote abierto desde el dashboard):
     1) WiFi (while hasta conectar) + cámara.
     2) Pollea GET /api/lote/activo hasta que haya lote ABIERTO.
     3) Por cada vuelta (x3):
          - gira la palta en los rodillos con pulsos (3 s)
          - para los rodillos, deja asentar y TOMA foto
          - POST /api/palta  ->  palta_id     +     POST /api/captura/foto
            => aparece en el dashboard
     4) "Bota" la palta (rodillos en sentido contrario) y termina ese lote.

   ⚠️ ALIMENTACIÓN 13 V — LEER:
     - El L298N a 13 V: QUITA el jumper del regulador 5V de la placa y alimenta
       su lógica con 5V aparte. El regulador interno se sobrecalienta sobre ~12V.
     - Los motores TT amarillos son de 3–6 V. A 13 V solo se salvan porque usas
       DUTY BAJO (giro=45/255, botar=120/255). No subas a 255 ni los dejes fijos
       a full o se queman. Pulsos cortos = bien.
     - GND COMÚN obligatorio: GND ESP32 = GND de los dos L298N = (–) de la fuente.
     - NO conectes los 13 V al ESP32 (va por USB). La fuente es de red: cuidado.
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

// ─── CONFIG WiFi + Backend ──────────────────────────────────────────────────
const char* SSID         = "TU_RED_WIFI";
const char* PASSWORD     = "TU_CONTRASENA";
const char* BACKEND_HOST = "10.150.244.60";   // IP de tu laptop
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

// ─── PINES RODILLOS (L298N #2, jumpers ENA/ENB quitados) ────────────────────
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;   // (8 era de la cámara; usamos 47)

// ─── PWM ────────────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;     // 0..255

// ─── Velocidades / tiempos (de tu código) ───────────────────────────────────
const int potenciaGiro  = 45;
const int potenciaBotar = 120;
const unsigned long tiempoGiroRodillos     = 3000;
const unsigned long tiempoBotar            = 4000;
const int cantidadVueltas                  = 3;
const int tiempoPulsoGiroEncendido         = 50;
const int tiempoPulsoGiroApagado           = 1000;
const unsigned long tiempoAsientaFoto       = 600;   // que la palta quede quieta

// ─── WiFi/Backend ───────────────────────────────────────────────────────────
const unsigned long POLL_LOTE_MS = 5000;

bool loteActivo  = false;
int  loteId      = -1;
bool yaProcesado = false;        // un ciclo por lote

// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== PRUEBA Rodillos + Camara + Dashboard =====");

  pinMode(IN1_RODILLO1, OUTPUT);
  pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT);
  pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerRodillos();

  iniciarCamara();
  conectarWiFi();
}

// ════════════════════════════════════════════════════════════════════════════
void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();

  consultarLoteActivo();

  if (loteActivo && !yaProcesado) {
    procesarLote();
    yaProcesado = true;          // ya hicimos el ciclo de este lote
  }
  if (!loteActivo) yaProcesado = false;   // se cerró: rearmar para el próximo

  delay(POLL_LOTE_MS);
}

// ════════════════════════════════════════════════════════════════════════════
//  CICLO POR LOTE
// ════════════════════════════════════════════════════════════════════════════
void procesarLote() {
  Serial.printf("\n>>> LOTE %d ACTIVO\n", loteId);

  for (int i = 1; i <= cantidadVueltas; i++) {
    Serial.printf("\n[Vuelta %d/%d] girando palta en rodillos\n", i, cantidadVueltas);
    girarPaltaEnRodillos(tiempoGiroRodillos);

    Serial.println("  stop + asentar + foto");
    detenerRodillos();
    delay(tiempoAsientaFoto);

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("  [CAM] %u bytes\n", fb->len);
      int paltaId = enviarPalta();          // POST /api/palta
      enviarFoto(fb, paltaId);              // POST /api/captura/foto
      esp_camera_fb_return(fb);
    } else {
      Serial.println("  [CAM] ERROR: sin frame");
    }
  }

  Serial.println("\n[Botar] expulsando palta");
  botarPalta(tiempoBotar);
  detenerRodillos();
  Serial.println("[OK] lote procesado — revisa el dashboard LoteActivo.vue\n");
}

// ════════════════════════════════════════════════════════════════════════════
//  RODILLOS (tu lógica con pulsos)
// ════════════════════════════════════════════════════════════════════════════
void rodillosParaGirarPalta(int v1, int v2) {
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, LOW);  digitalWrite(IN4_RODILLO2, HIGH);
  ledcWrite(ENB_RODILLO2, v2);
}
void rodillosParaBotarPalta(int v1, int v2) {
  digitalWrite(IN1_RODILLO1, LOW);  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, v1);
  digitalWrite(IN3_RODILLO2, HIGH); digitalWrite(IN4_RODILLO2, LOW);
  ledcWrite(ENB_RODILLO2, v2);
}
void girarPaltaEnRodillos(unsigned long tiempoTotal) {
  unsigned long inicio = millis();
  while (millis() - inicio < tiempoTotal) {
    rodillosParaGirarPalta(potenciaGiro, potenciaGiro);
    delay(tiempoPulsoGiroEncendido);
    detenerRodillos();
    delay(tiempoPulsoGiroApagado);
  }
}
void botarPalta(unsigned long tiempoTotal) {
  rodillosParaBotarPalta(potenciaBotar, potenciaBotar);
  delay(tiempoTotal);
  detenerRodillos();
}
void detenerRodillos() {
  ledcWrite(ENA_RODILLO1, 0);
  ledcWrite(ENB_RODILLO2, 0);
  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, LOW);
  digitalWrite(IN3_RODILLO2, LOW);
  digitalWrite(IN4_RODILLO2, LOW);
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

int enviarPalta() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  char body[200];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":null,\"confianza\":null,"
    "\"r\":null,\"g\":null,\"b\":null,\"lux\":null,"
    "\"temp\":null,\"humedad\":null,\"ir_detectado\":true,\"lecturas\":1}",
    loteId);
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
  while (WiFi.status() != WL_CONNECTED) {   // while pedido
    delay(500); Serial.print(".");
  }
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
