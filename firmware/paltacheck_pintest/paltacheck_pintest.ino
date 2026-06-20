/* ============================================================================
   PaltaCheck — PRUEBA DE FLUJO  (ESP32-S3 N16R8 + cámara OV3660)
   ----------------------------------------------------------------------------
   FLUJO DE PRUEBA (1 foto por CADA lote que abras):
     1) Espera a que se ABRA un lote desde el frontend (GET /api/lote/activo).
     2) Cuando hay lote -> espera 10 segundos.
     3) Gira AMBOS rodillos A LA VEZ, LENTO (por pulsos), durante 5 segundos.
     4) Para los rodillos y espera 5 segundos.
     5) Toma UNA foto y la sube al backend.
        -> Aparece sola en el "cuadrito" de Captura de cámara (LoteActivo.vue).
     6) Para los motores. Al CERRAR ese lote y abrir OTRO, repite el flujo.

   ----------------------------------------------------------------------------
   PINES: motores REUBICADOS a pines libres -> ya NO chocan con la cámara.
   Control de velocidad por PWM (LEDC). La cámara usa el canal LEDC 0, por eso
   los rodillos van en los canales 4 y 5 (no chocan).
   ----------------------------------------------------------------------------
   CABLEADO L298N (rodillos):
       Rodillo 1: ENA=GPIO42  IN1=GPIO41  IN2=GPIO40
       Rodillo 2: ENB=GPIO39  IN3=GPIO38  IN4=GPIO47
       GND L298N <-> GND ESP32 (masa común)  ·  motores -> fuente externa
   Programar por el USB-UART. Monitor serial a 115200.
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

// ─── CONFIG WiFi (HOTSPOT del celular) + Backend ────────────────────────────
//  >>> EDITA SOLO ESTOS 4 VALORES <<<
//    SSID / PASSWORD : nombre y clave del HOTSPOT de tu celular.
//    BACKEND_HOST    : IP que el CELULAR le asignó a tu LAPTOP (ahí corre el backend
//                      FastAPI). Conecta la laptop al hotspot del celular y, en
//                      Windows, abre 'cmd' -> 'ipconfig' -> copia la "Dirección
//                      IPv4" del adaptador Wi-Fi (Android suele dar 192.168.43.x;
//                      iPhone 172.20.10.x). ¡NO es 192.168.137.1 ni localhost!
//    BACKEND_PORT    : puerto del backend = 8000.
//  El ESP32 quedará en BUCLE esperando este hotspot hasta conectarse (ver setup()).
const char* SSID         = "TU_HOTSPOT";       // <-- nombre del hotspot del celular
const char* PASSWORD     = "TU_CONTRASENA";    // <-- clave del hotspot
const char* BACKEND_HOST = "10.207.55.254";    // <-- IPv4 del adaptador "Wi-Fi" (NO el de WSL 172.17.x)
const int   BACKEND_PORT = 8000;               // <-- puerto del backend

// ─── PINES MOTORES (libres, NO chocan con cámara ni sensores) ───────────────
#define ROD1_ENA   42
#define ROD1_IN1   41
#define ROD1_IN2   40
#define ROD2_ENB   39
#define ROD2_IN3   38
#define ROD2_IN4   47

// ─── PWM motores (ESP32 Arduino Core v3.x: ledcAttachChannel / ledcWrite) ───
//  Jumpers ENA/ENB QUITADOS -> el PWM regula la velocidad.
#define PWM_FREQ        1000   // Hz
#define PWM_RES         8      // bits -> duty 0..255
#define CH_ROD1         4      // canal LEDC del ENA rodillo 1 (la cámara usa el 0)
#define CH_ROD2         5      // canal LEDC del ENB rodillo 2
// Giro LENTO por PULSOS: un PWM bajo y CONTINUO se TRABA (no arranca); por eso
// damos pulsos cortos a buena potencia y el rodillo gira "a pasitos" lento.
#define GIRO_PULSO_VEL  140    // potencia del pulso (0..255). SUBE si no se mueve.
#define GIRO_PULSO_ON   80     // ms que gira en cada paso (más = gira más por paso)
#define GIRO_PULSO_OFF  450    // ms de pausa entre pasos (más = MÁS lento)

// ─── PINES CÁMARA (fijos del board, OV3660) ─────────────────────────────────
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D7     16
#define CAM_PIN_D6     17
#define CAM_PIN_D5     18
#define CAM_PIN_D4     12
#define CAM_PIN_D3     10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0     11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK   13

// ─── Tiempos del flujo ──────────────────────────────────────────────────────
#define ESPERA_INICIAL_MS   10000   // paso 2: espera tras abrir lote
#define GIRO_MS             5000    // paso 3: giro LENTO de rodillos
#define ESPERA_FOTO_MS      5000    // paso 4: espera antes de tomar la foto
#define POLL_LOTE_MS        3000    // cada cuánto consulta el lote activo

// ─── Estado ─────────────────────────────────────────────────────────────────
bool     hayCamara     = false;
uint32_t ultimoPollMs  = 0;
int      loteId        = -1;
int      loteProcesado = -1;   // último lote ya procesado: 1 foto por lote (re-arma al cerrarse)

// ════════════════════════════════════════════════════════════════════════════
//  MOTORES (L298N) — mismo control que tu test que SÍ gira (PWM + pulsos lentos)
// ════════════════════════════════════════════════════════════════════════════

// Detiene ambos rodillos (duty 0 + direcciones en LOW).
void detenerRodillos() {
  ledcWrite(ROD1_ENA, 0);  ledcWrite(ROD2_ENB, 0);
  digitalWrite(ROD1_IN1, LOW); digitalWrite(ROD1_IN2, LOW);
  digitalWrite(ROD2_IN3, LOW); digitalWrite(ROD2_IN4, LOW);
}

void initPinesMotor() {
  pinMode(ROD1_IN1, OUTPUT); pinMode(ROD1_IN2, OUTPUT);
  pinMode(ROD2_IN3, OUTPUT); pinMode(ROD2_IN4, OUTPUT);
  // PWM en los habilitadores ENA/ENB en canales propios (la cámara usa el 0).
  ledcAttachChannel(ROD1_ENA, PWM_FREQ, PWM_RES, CH_ROD1);
  ledcAttachChannel(ROD2_ENB, PWM_FREQ, PWM_RES, CH_ROD2);
  detenerRodillos();
}

// Ambos rodillos giran A LA VEZ, en el mismo sentido. 'vel' = duty 0..255 (PWM).
void rodillosGirar(int vel) {
  digitalWrite(ROD1_IN1, LOW); digitalWrite(ROD1_IN2, HIGH); ledcWrite(ROD1_ENA, vel);
  digitalWrite(ROD2_IN3, LOW); digitalWrite(ROD2_IN4, HIGH); ledcWrite(ROD2_ENB, vel);
}

// Diagnóstico: gira SOLO el rodillo 1 (para aislar fallas de motor/cable/canal).
void rodillo1Girar(int vel) {
  digitalWrite(ROD1_IN1, LOW); digitalWrite(ROD1_IN2, HIGH); ledcWrite(ROD1_ENA, vel);
}
// Diagnóstico: gira SOLO el rodillo 2.
void rodillo2Girar(int vel) {
  digitalWrite(ROD2_IN3, LOW); digitalWrite(ROD2_IN4, HIGH); ledcWrite(ROD2_ENB, vel);
}

// Giro LENTO por PULSOS durante 'tiempoTotal' ms: gira GIRO_PULSO_ON ms y luego
// descansa GIRO_PULSO_OFF ms, repetido. Así avanza lento "a pasitos" sin trabarse.
void girarRodillosLento(unsigned long tiempoTotal) {
  Serial.printf("[MOTOR] Rodillos girando LENTO por pulsos (vel=%d) %lu ms\n",
                GIRO_PULSO_VEL, (unsigned long)tiempoTotal);
  unsigned long inicio = millis();
  while (millis() - inicio < tiempoTotal) {
    rodillosGirar(GIRO_PULSO_VEL);   // pulso de giro
    delay(GIRO_PULSO_ON);
    detenerRodillos();               // pausa
    delay(GIRO_PULSO_OFF);
  }
  detenerRodillos();
  Serial.println("[MOTOR] Rodillos detenidos");
}

// ════════════════════════════════════════════════════════════════════════════
//  CÁMARA
// ════════════════════════════════════════════════════════════════════════════
bool iniciarCamara() {
  bool hayPSRAM = psramFound();
  Serial.printf("[CAM] PSRAM %s\n", hayPSRAM ? "detectada" : "NO encontrada (config conservadora)");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;
  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 12;

  if (hayPSRAM) {
    config.frame_size  = FRAMESIZE_SVGA;   // 800x600: más detalle de la palta (antes VGA)
    config.fb_count    = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode   = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size  = FRAMESIZE_QVGA;
    config.fb_count    = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[!!] ERROR camara: 0x%x\n", err);
    return false;
  }

  // Ajustes de imagen (OV3660): controlar exposición/ganancia para que la palta
  // NO salga "quemada" (blanca) y mejorar color/nitidez aparente.
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_whitebal(s, 1);                  // balance de blancos auto
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);             // exposición auto (AEC)
    s->set_aec2(s, 1);                      // AEC de precisión
    s->set_gain_ctrl(s, 1);                 // ganancia auto (AGC)
    s->set_gainceiling(s, GAINCEILING_2X);  // limita ganancia -> menos "quemado"
    s->set_brightness(s, -1);               // un punto más oscuro (evita el blanco)
    s->set_saturation(s, 1);                // color un poco más vivo
    s->set_lenc(s, 1);                      // corrección de lente
  }

  Serial.println("[OK] Camara inicializada");
  return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  BACKEND
// ════════════════════════════════════════════════════════════════════════════

// GET /api/lote/activo -> devuelve loteId si hay lote abierto, -1 si no.
int consultarLoteActivo() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/lote/activo";
  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();
  int id = -1;
  if (code == 200) {
    String body = http.getString();
    if (body.indexOf("\"activo\":true") >= 0) {
      int i = body.indexOf("\"id\":");
      if (i >= 0) id = body.substring(i + 5).toInt();
    }
  }
  http.end();
  return id;
}

// POST /api/palta -> devuelve palta_id
int enviarPalta(int lote) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  char body[160];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":null,\"confianza\":null,"
    "\"ir_detectado\":true,\"lecturas\":1}", lote);

  int paltaId = -1;
  int code = http.POST((uint8_t*)body, strlen(body));
  if (code == 200) {
    String resp = http.getString();
    int i = resp.indexOf("\"palta_id\":");
    if (i >= 0) paltaId = resp.substring(i + 11).toInt();
    Serial.printf("[NET] POST /api/palta OK  palta_id=%d\n", paltaId);
  } else {
    Serial.printf("[NET] POST /api/palta HTTP %d\n", code);
  }
  http.end();
  return paltaId;
}

// POST /api/captura/foto?palta_id=..&lote_id=..  (JPEG crudo en el cuerpo)
void enviarFoto(camera_fb_t* fb, int paltaId, int lote) {
  if (!fb || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/captura/foto?palta_id=" + paltaId + "&lote_id=" + lote;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);
  if (code == 200) Serial.printf("[NET] POST foto OK (%u bytes)\n", fb->len);
  else             Serial.printf("[NET] POST foto HTTP %d\n", code);
  http.end();
}

// Conexión WiFi en BUCLE INFINITO: no avanza hasta engancharse al hotspot del
// celular. Reintenta WiFi.begin() cada 8 s por si el hotspot aún no estaba
// encendido cuando arrancó el ESP32. Es seguro llamarla también para reconectar.
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // mejor estabilidad con hotspot de celular
  Serial.printf("[WiFi] Esperando hotspot \"%s\" (bucle hasta conectar)...\n", SSID);

  bool     primera      = true;
  uint32_t ultimoInicio = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (primera || millis() - ultimoInicio > 8000) {
      WiFi.disconnect();
      WiFi.begin(SSID, PASSWORD);
      ultimoInicio = millis();
      primera = false;
      Serial.print("\n[WiFi] intentando conectar");
    }
    delay(500);
    Serial.print(".");
  }

  Serial.print("\n[WiFi] CONECTADO  IP ESP32: "); Serial.println(WiFi.localIP());
  Serial.printf("[WiFi] Backend -> http://%s:%d\n", BACKEND_HOST, BACKEND_PORT);
}

// ════════════════════════════════════════════════════════════════════════════
//  FLUJO DE PRUEBA (se ejecuta una sola vez al detectar lote)
// ════════════════════════════════════════════════════════════════════════════
void ejecutarFlujo(int lote) {
  loteId = lote;
  Serial.printf("\n>>> LOTE ACTIVO id=%d — arrancando flujo\n", lote);

  // Paso 2: esperar 10 s
  Serial.println("[1/4] Esperando 10 segundos...");
  delay(ESPERA_INICIAL_MS);

  // Paso 3: girar AMBOS rodillos LENTO (por pulsos) durante 5 s
  Serial.println("[2/4] Girando rodillos LENTO 5 s...");
  girarRodillosLento(GIRO_MS);

  // Paso 4: esperar 5 s antes de la foto
  Serial.println("[3/4] Esperando 5 segundos antes de la foto...");
  delay(ESPERA_FOTO_MS);

  // Paso 5: tomar foto (la cámara ya está inicializada desde el setup)
  Serial.println("[4/4] Tomando foto...");
  if (!hayCamara) {
    Serial.println("[!!] Sin camara — no se puede tomar la foto. Flujo abortado.");
    return;
  }
  // Descartar varios frames para que el auto-exposición y el balance de blancos
  // se estabilicen (si solo se descarta uno, suele salir quemada/oscura).
  for (int i = 0; i < 4; i++) {
    camera_fb_t* prev = esp_camera_fb_get();
    if (prev) esp_camera_fb_return(prev);
    delay(150);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] ERROR al capturar frame");
    return;
  }
  Serial.printf("[CAM] foto capturada: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);

  // Subir al backend: primero la palta, luego la foto con el palta_id
  int paltaId = enviarPalta(lote);
  if (paltaId > 0) enviarFoto(fb, paltaId, lote);
  else             Serial.println("[NET] sin palta_id — no se sube la foto");
  esp_camera_fb_return(fb);

  Serial.println("[OK] Listo. Foto enviada al dashboard.");
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP / LOOP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= PaltaCheck — PRUEBA DE FLUJO (motores + 1 foto) =========");

  // Cámara PRIMERO: reserva el canal LEDC 0 / timer 0 para el XCLK.
  hayCamara = iniciarCamara();

  // Motores DESPUÉS: PWM en canales 4 y 5, así no los pisa la cámara.
  initPinesMotor();
  Serial.println("[OK] Pines de motor en STOP");

  // ── AUTO-TEST de arranque: prueba cada rodillo POR SEPARADO ───────────────
  // Sirve para saber por qué "solo gira uno". Power alto (200) para que la
  // fricción NO sea excusa:
  //   · Rodillo que NO gira SOLO  -> hardware de ESE rodillo (motor, cable, o su
  //     canal del L298N: ENB/IN3/IN4).
  //   · Ambos giran solos pero NO juntos -> la fuente no da para los dos
  //     (sube el voltaje/corriente de la fuente de motores).
  // (Comenta este bloque cuando ya esté resuelto.)
  Serial.println("[TEST] Rodillo 1 SOLO (2 s)...");
  rodillo1Girar(200); delay(2000); detenerRodillos(); delay(600);
  Serial.println("[TEST] Rodillo 2 SOLO (2 s)...");
  rodillo2Girar(200); delay(2000); detenerRodillos(); delay(600);
  Serial.println("[TEST] AMBOS juntos (2 s)...");
  rodillosGirar(200);  delay(2000); detenerRodillos();
  Serial.println("[TEST] Fin. Mira cuál NO giró SOLO -> ese es el problema de hardware.");

  conectarWiFi();
  Serial.println("[i] Esperando que se abra un lote desde el dashboard...\n");
}

void loop() {
  // Si se cae el hotspot del celular, reconecta (bucle) antes de seguir.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] conexión perdida — reconectando...");
    conectarWiFi();
  }

  if (millis() - ultimoPollMs > POLL_LOTE_MS) {
    ultimoPollMs = millis();
    int lote = consultarLoteActivo();
    if (lote > 0 && lote != loteProcesado) {
      // Lote NUEVO -> corre el flujo UNA vez para este lote.
      ejecutarFlujo(lote);
      loteProcesado = lote;
      Serial.println("\n===== Flujo completo. Cierra y abre OTRO lote para tomar otra foto. =====");
    } else if (lote <= 0) {
      // No hay lote activo -> re-armar para el próximo que abras.
      if (loteProcesado != -1) Serial.println("[i] Lote cerrado. Esperando uno nuevo...");
      loteProcesado = -1;
    }
    // Si lote == loteProcesado (mismo lote ya hecho), espera sin repetir.
  }
}
