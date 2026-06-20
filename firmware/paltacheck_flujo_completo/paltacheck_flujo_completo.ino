/* ============================================================================
   PaltaCheck — FLUJO COMPLETO: CINTA + RODILLOS + FOTO + SERVO + BOTE
   (ESP32-S3 N16R8)  ·  script independiente (no toca los anteriores)
   ----------------------------------------------------------------------------
   Por cada lote abierto desde el dashboard:
     1) WiFi + cámara
     2) CINTA adelante 5 s -> para
     3) Espera 2 s
     4) RODILLOS x3: giro lento -> para -> FOTO -> manda al dashboard
     5) DECISIÓN (compuerta servo, GPIO 3):
          · palta BUENA  -> NO mueve el servo (compuerta cerrada)
          · palta MALA   -> servo abre la compuerta para que CAIGA la palta
     6) BOTE final (rodillos expulsan) y vuelve la compuerta a 0.

   PRUEBA DE 2 FLUJOS (se alternan solos):
     · 1er lote procesado  -> BUENA  (sin servo)
     · 2do lote procesado  -> MALA   (con servo)
     · (sigue alternando: 3ro buena, 4to mala, ...)

   Requisito: instalar la librería **ESP32Servo** en el Arduino IDE.
   ⚠️ El servo se alimenta con 5 V APARTE (no del 3V3 del ESP32) y GND común.
   ⚠️ 13 V: jumper 5V del L298N fuera + 5V lógica aparte; GND común; nunca 13V al ESP32.
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <ESP32Servo.h>

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

// ─── PINES CINTA (L298N #1) ─────────────────────────────────────────────────
const int ENA_CINTA = 46;     // PWM velocidad
const int IN1_CINTA = 48;     // dirección (HIGH = adelante). IN2 -> GND directo.

// ─── PINES RODILLOS (L298N #2) ──────────────────────────────────────────────
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;

// ─── SERVO compuerta (lógica de tu compañero) ──────────────────────────────
const int PIN_SERVO          = 3;
const int ANGULO_COMPUERTA   = 45;   // cuánto abre la compuerta
const int velocidadMovimiento = 3;   // ms por grado (menor = más rápido)
Servo servoCompuerta;
int  posicionActual = 0;

// ─── PWM motores ────────────────────────────────────────────────────────────
const int freqPWM       = 1000;
const int resolucionPWM = 8;     // 0..255

// ─── Velocidades / tiempos ──────────────────────────────────────────────────
// Cinta
const int velocidadCinta            = 100;
const int dutyArranqueCinta         = 180;
const int tiempoArranqueCintaMs     = 150;
const unsigned long tiempoCinta     = 5000;   // 5 s de cinta
const unsigned long tiempoEspera    = 2000;   // 2 s de espera
// Rodillos
const int potenciaBotar             = 120;
const int dutyArranque              = 160;
const int dutyLento                 = 70;
const int tiempoArranqueMs          = 150;
const unsigned long tiempoGiroRodillos = 3000;
const unsigned long tiempoBotar         = 4000;
const int cantidadVueltas               = 3;
const unsigned long tiempoAsientaFoto   = 600;

// ─── WiFi/Backend ───────────────────────────────────────────────────────────
const unsigned long POLL_LOTE_MS = 5000;
bool loteActivo  = false;
int  loteId      = -1;
bool yaProcesado = false;
int  contadorPaltas = 0;        // para alternar BUENA / MALA

// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== FLUJO COMPLETO: Cinta + Rodillos + Foto + Servo + Bote =====");

  // Cinta
  pinMode(IN1_CINTA, OUTPUT);
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  // Rodillos
  pinMode(IN1_RODILLO1, OUTPUT);
  pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT);
  pinMode(IN4_RODILLO2, OUTPUT);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);
  detenerTodo();

  // Servo (compuerta cerrada en 0)
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(0);
  posicionActual = 0;

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
//  CICLO POR LOTE
// ════════════════════════════════════════════════════════════════════════════
void procesarLote() {
  Serial.printf("\n>>> LOTE %d ACTIVO — probando los 2 flujos seguidos\n", loteId);

  procesarPalta(true);    // 1ra palta: BUENA (sin servo)

  Serial.println("\n--- Siguiente palta en 3 s ---");
  delay(3000);

  procesarPalta(false);   // 2da palta: MALA (con servo)

  Serial.println("\n[OK] LOTE COMPLETO (buena + mala) — revisa el dashboard\n");
}

// Procesa UNA palta completa: cinta -> rodillos+fotos -> servo -> bote.
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

  // 3) Rodillos: 3 vueltas con foto
  for (int i = 1; i <= cantidadVueltas; i++) {
    Serial.printf("\n[Vuelta %d/%d] girando palta en rodillos\n", i, cantidadVueltas);
    girarPaltaEnRodillos(tiempoGiroRodillos);
    Serial.println("  stop + asentar + foto");
    detenerRodillos();
    delay(tiempoAsientaFoto);

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("  [CAM] %u bytes\n", fb->len);
      int paltaId = enviarPalta(esBuena);     // POST /api/palta
      enviarFoto(fb, paltaId);                // POST /api/captura/foto
      esp_camera_fb_return(fb);
    } else {
      Serial.println("  [CAM] ERROR: sin frame");
    }
  }

  // 4) DECISIÓN: compuerta
  if (esBuena) {
    Serial.println("\n[SERVO] palta BUENA -> compuerta CERRADA (no mueve)");
  } else {
    Serial.println("\n[SERVO] palta MALA -> ABRE compuerta para que caiga");
    moverCompuerta(ANGULO_COMPUERTA);
    delay(800);                               // deja caer la palta
  }

  // 5) Bote final (rodillos expulsan)
  Serial.println("[Botar] expulsando palta");
  botarPalta(tiempoBotar);
  detenerTodo();

  // 6) Vuelve la compuerta a 0 para la siguiente
  if (!esBuena) {
    moverCompuerta(0);
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SERVO (movimiento suave, lógica de tu compañero)
// ════════════════════════════════════════════════════════════════════════════
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 0, 180);
  Serial.printf("  [SERVO] %d -> %d grados\n", posicionActual, nuevaPos);
  if (nuevaPos > posicionActual) {
    for (int p = posicionActual; p <= nuevaPos; p++) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  } else {
    for (int p = posicionActual; p >= nuevaPos; p--) { servoCompuerta.write(p); delay(velocidadMovimiento); }
  }
  posicionActual = nuevaPos;
}

// ════════════════════════════════════════════════════════════════════════════
//  CINTA
// ════════════════════════════════════════════════════════════════════════════
void cintaAdelante(int vel) {
  digitalWrite(IN1_CINTA, HIGH);
  ledcWrite(ENA_CINTA, dutyArranqueCinta);
  delay(tiempoArranqueCintaMs);
  ledcWrite(ENA_CINTA, vel);
}
void detenerCinta() {
  ledcWrite(ENA_CINTA, 0);
  digitalWrite(IN1_CINTA, LOW);
}

// ════════════════════════════════════════════════════════════════════════════
//  RODILLOS
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
  rodillosParaGirarPalta(dutyArranque, dutyArranque);
  delay(tiempoArranqueMs);
  rodillosParaGirarPalta(dutyLento, dutyLento);
  if (tiempoTotal > (unsigned long)tiempoArranqueMs)
    delay(tiempoTotal - tiempoArranqueMs);
  detenerRodillos();
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
void detenerTodo() {
  detenerCinta();
  detenerRodillos();
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

// POST /api/palta. Mandamos la clasificación de prueba: BUENA="sana", MALA="antracnosis".
int enviarPalta(bool esBuena) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  const char* clase = esBuena ? "\"sana\"" : "\"antracnosis\"";
  char body[220];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":%s,\"confianza\":1.0,"
    "\"r\":null,\"g\":null,\"b\":null,\"lux\":null,"
    "\"temp\":null,\"humedad\":null,\"ir_detectado\":true,\"lecturas\":1}",
    loteId, clase);
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
  while (WiFi.status() != WL_CONNECTED) {
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
