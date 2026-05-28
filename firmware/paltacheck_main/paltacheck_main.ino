/* ============================================================================
   PaltaCheck — Firmware unificado  (ESP32-S3-CAM / WROOM-1 N16R8)
   ----------------------------------------------------------------------------
   Máquina de estados ESCALABLE controlada desde el frontend:

     Frontend "Abrir Lote"  →  POST /api/lote        →  backend marca lote activo
     ESP32 (este sketch)    →  GET  /api/lote/activo  →  arranca cinta/servos
                            →  IR detecta palta       →  ciclo de 3 vueltas
                            →  POST /api/palta + foto  →  guarda en TimescaleDB
     Frontend "Cerrar Lote" →  POST /api/lote/{id}/cerrar → ESP32 lo detecta y PARA

   FASE ACTUAL (de a poco): se LEEN de verdad los sensores ya probados
   (DHT22, TCS34725, FC-51) y la CÁMARA. LEDs, motores, servo, buzzer, OLED y
   los modelos TFLite se SIMULAN con Serial.println + delays, para ver toda la
   secuencia por el monitor serial sin depender de hardware aún no integrado.

   ============================================================================
   ⚠️  CONFLICTO DE PINES — LEER ANTES DE CONECTAR  ⚠️
   ----------------------------------------------------------------------------
   La cámara del ESP32-S3-CAM OCUPA de forma FIJA (no se pueden mover):
       15 (XCLK) · 16 (D7) · 17 (D6) · 18 (D5) · 12 (D4) · 10 (D3) ·
        8 (D2)  ·  9 (D1) · 11 (D0) ·  4 (SIOD) · 5 (SIOC) ·
        6 (VSYNC) · 7 (HREF) · 13 (PCLK)
   Tu doc de sensores usaba 16 (DHT22), 15 (TCS-SCL) y 17 (IR): CHOCAN con la
   cámara. Por eso aquí los REASIGNÉ a pines libres:
       DHT22  -> GPIO 2     (antes 16)
       TCS    -> SDA 14 / SCL 21   (SDA seguía libre; SCL movido de 15 -> 21)
       FC-51  -> GPIO 1     (antes 17)
   👉 Verifica que esos GPIO estén expuestos y libres en TU placa. Si tu placa
      no los expone, cámbialos aquí en el bloque "PINES SENSORES".
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Wire.h>
#include <DHT.h>
#include "Adafruit_TCS34725.h"

// ─── CONFIG WiFi + Backend (laptop) ─────────────────────────────────────────
const char* SSID         = "TU_RED_WIFI";
const char* PASSWORD     = "TU_CONTRASENA";
const char* BACKEND_HOST = "10.150.244.60";   // IP IPv4 de tu laptop en la red WiFi
const int   BACKEND_PORT = 8000;

// ─── PINES CÁMARA (fijos del board ESP32-S3-CAM, no tocar) ──────────────────
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

// ─── PINES SENSORES (REASIGNADOS para no chocar con la cámara) ──────────────
#define PIN_DHT22   2     // DHT22 DATA  (antes 16)
#define I2C_SDA     14    // TCS34725 SDA
#define I2C_SCL     21    // TCS34725 SCL (antes 15)
#define PIN_IR      1     // FC-51 OUT   (antes 17). LOW = objeto detectado

// ─── PINES ACTUADORES (SIMULADOS por Serial en esta fase — ajustar luego) ───
#define PIN_LED_VERDE  39
#define PIN_LED_ROJO   40
#define PIN_LED_AZUL   38
#define PIN_BUZZER     41
#define PIN_SERVO      42
// L298N (cinta + rotación): solo se anuncian por Serial en esta fase.

// ─── Parámetros del ciclo ───────────────────────────────────────────────────
#define VUELTAS_POR_PALTA   3
#define UMBRAL_VOTO_SANA    0.6f
#define POLL_LOTE_MS        5000     // cada cuánto consulta el lote activo
#define DELAY_ROTACION_MS   1200     // delay simulado de cada giro de 120°
#define DELAY_ESTABILIZA_MS 400      // delay para que la palta quede quieta

// IR (FC-51): LOW = objeto presente. Para no re-disparar con la misma palta:
//   1) confirmamos la lectura tras IR_DEBOUNCE_MS  (rechaza picos eléctricos)
//   2) tras procesarla, ESPERAMOS a que el sensor vuelva a HIGH (palta salió)
//   3) cooldown extra antes de aceptar la próxima
#define IR_DEBOUNCE_MS         50
#define IR_WAIT_RELEASE_MS  10000    // timeout para que la palta abandone la ranura
#define IR_COOLDOWN_MS        500    // pausa antiparpadeo despues de release

// ─── Sensores ───────────────────────────────────────────────────────────────
DHT dht(PIN_DHT22, DHT22);
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
    TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

// ─── Estado del sistema ─────────────────────────────────────────────────────
enum Estado { SIN_LOTE, ESPERANDO_PALTA, PROCESANDO };
Estado   estado          = SIN_LOTE;
bool     loteActivo      = false;
int      loteId          = -1;
uint32_t ultimoPollMs    = 0;
uint32_t ultimoParpadeo  = 0;
bool     ledAzulEstado   = false;
bool     backendConectado = false;   // ¿el ESP32 logra hablar con el backend?

// ════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n================ PaltaCheck — firmware unificado ================");

  pinMode(PIN_IR,        INPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_LED_AZUL,  OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);

  iniciarCamara();

  dht.begin();
  Wire.begin(I2C_SDA, I2C_SCL);
  if (tcs.begin()) Serial.println("[OK] TCS34725 detectado");
  else             Serial.println("[!!] TCS34725 NO encontrado (revisa I2C)");

  conectarWiFi();
  if (WiFi.status() == WL_CONNECTED) esperarBackend();   // confirma que el backend responde
  Serial.println("[i] Sistema en espera de lote (abre uno desde el dashboard)\n");
}

// ════════════════════════════════════════════════════════════════════════════
//  LOOP — máquina de estados
// ════════════════════════════════════════════════════════════════════════════
void loop() {
  // 1) Consultar periódicamente si hay lote activo (señal del frontend)
  if (millis() - ultimoPollMs > POLL_LOTE_MS) {
    ultimoPollMs = millis();
    bool antes = loteActivo;
    consultarLoteActivo();

    if (loteActivo && !antes) {            // se abrió un lote
      Serial.printf("\n>>> LOTE ACTIVO id=%d  →  arrancando cinta\n", loteId);
      arrancarCinta();
      estado = ESPERANDO_PALTA;
    }
    if (!loteActivo && antes) {            // se cerró el lote
      Serial.println("\n<<< LOTE CERRADO  →  deteniendo todo el sistema");
      detenerTodo();
      estado = SIN_LOTE;
    }
  }

  // 2) Comportamiento según estado
  switch (estado) {
    case SIN_LOTE:
      parpadeoEspera();                    // LED azul parpadeo lento
      break;

    case ESPERANDO_PALTA:
      if (digitalRead(PIN_IR) == LOW) {                    // FC-51: LOW = palta detectada
        // Debounce: confirmar que sigue en LOW tras un instante
        delay(IR_DEBOUNCE_MS);
        if (digitalRead(PIN_IR) != LOW) { parpadeoEspera(); break; }

        Serial.println("\n[IR] Palta detectada en la ranura");
        procesarPalta();

        // Esperar a que la palta SALGA de la ranura (IR vuelve a HIGH).
        // Con timeout para no quedar atascado si el sensor falla en LOW.
        Serial.println("[IR] esperando a que la palta deje la ranura...");
        uint32_t t0 = millis();
        while (digitalRead(PIN_IR) == LOW && millis() - t0 < IR_WAIT_RELEASE_MS) {
          delay(20);
        }
        if (digitalRead(PIN_IR) == LOW) {
          Serial.println("[IR] timeout esperando release — sigo igual (revisa el FC-51)");
        }
        delay(IR_COOLDOWN_MS);              // cooldown contra rebotes finales
        estado = ESPERANDO_PALTA;           // listo para la siguiente
      } else {
        parpadeoEspera();
      }
      break;

    case PROCESANDO:
      break;                               // se maneja dentro de procesarPalta()
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  CICLO DE UNA PALTA (3 vueltas)
// ════════════════════════════════════════════════════════════════════════════
void procesarPalta() {
  estado = PROCESANDO;
  Serial.println("──────────────────────────────────────────────");
  detenerCinta();
  digitalWrite(PIN_LED_AZUL, HIGH);
  Serial.println("[LED AZUL] ON — procesando palta");

  // Acumuladores
  float sumR = 0, sumG = 0, sumB = 0, sumLux = 0, sumT = 0, sumHR = 0;
  int   lecturasValidas = 0;
  int   votosSana = 0, votosAntrac = 0;          // se llenarán con los modelos (fase futura)
  camera_fb_t* ultimaFoto = nullptr;

  for (int v = 1; v <= VUELTAS_POR_PALTA; v++) {
    Serial.printf("\n--- Vuelta %d/%d ---\n", v, VUELTAS_POR_PALTA);

    // Motor rota ~120° (SIMULADO)
    Serial.printf("[MOTOR] rotando palta ~120°  (delay %d ms)\n", DELAY_ROTACION_MS);
    delay(DELAY_ROTACION_MS);
    Serial.println("[MOTOR] detenido — palta quieta");
    delay(DELAY_ESTABILIZA_MS);

    // Cámara (REAL)
    camera_fb_t* fb = capturarFoto();
    if (fb) {
      Serial.printf("[CAM] foto_%d capturada: %u bytes\n", v, fb->len);
      // [MODELO TFLite] — pendiente de integrar (fase futura)
      Serial.println("[ML] clasificacion pendiente (modelos TFLite aun no integrados)");
      if (ultimaFoto) esp_camera_fb_return(ultimaFoto);
      ultimaFoto = fb;                     // guardamos la última para subirla
    } else {
      Serial.println("[CAM] ERROR al capturar frame");
    }

    // TCS34725 (REAL)
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);
    float lux = tcs.calculateLux(r, g, b);
    Serial.printf("[TCS] R=%u G=%u B=%u  Lux=%.1f\n", r, g, b, lux);

    // DHT22 (REAL)
    float t  = dht.readTemperature();
    float hr = dht.readHumidity();
    if (isnan(t) || isnan(hr)) {
      Serial.println("[DHT] lectura invalida (NaN) — revisa pull-up");
    } else {
      Serial.printf("[DHT] Temp=%.1f°C  HR=%.1f%%\n", t, hr);
    }

    // Acumular (escalamos RGB crudo 16-bit a 0-255 para encajar con la BD)
    sumR += map8(r, c);
    sumG += map8(g, c);
    sumB += map8(b, c);
    sumLux += lux;
    if (!isnan(t) && !isnan(hr)) { sumT += t; sumHR += hr; lecturasValidas++; }

    delay(200);
  }

  // Promedios del ciclo
  int   rProm = (int)(sumR / VUELTAS_POR_PALTA);
  int   gProm = (int)(sumG / VUELTAS_POR_PALTA);
  int   bProm = (int)(sumB / VUELTAS_POR_PALTA);
  float luxProm = sumLux / VUELTAS_POR_PALTA;
  float tProm  = lecturasValidas ? sumT  / lecturasValidas : NAN;
  float hrProm = lecturasValidas ? sumHR / lecturasValidas : NAN;

  Serial.println("\n[PROMEDIOS]");
  Serial.printf("  RGB=(%d,%d,%d)  Lux=%.1f  Temp=%.1f  HR=%.1f\n",
                rProm, gProm, bProm, luxProm, tProm, hrProm);

  // Veredicto (SIMULADO — sin modelos todavía)
  Serial.println("[VEREDICTO] pendiente (se decidira cuando entren los modelos TFLite)");
  Serial.println("[SERVO] (simulado) — la palta caera al cesto segun el veredicto");
  Serial.println("[BUZZER] (simulado) beep corto");

  // Enviar al backend: primero la palta, luego la foto con el palta_id
  int paltaId = enviarPalta(rProm, gProm, bProm, luxProm, tProm, hrProm,
                            votosSana, votosAntrac);
  if (ultimaFoto) {
    enviarFoto(ultimaFoto, paltaId);
    esp_camera_fb_return(ultimaFoto);
  }

  Serial.printf("[OLED] (simulado) actualizar contador del lote %d\n", loteId);

  // Fin del ciclo
  digitalWrite(PIN_LED_AZUL, LOW);
  Serial.println("[LED] apagado");
  arrancarCinta();
  Serial.println("[i] Esperando siguiente palta...\n");
}

// ════════════════════════════════════════════════════════════════════════════
//  COMUNICACIÓN CON EL BACKEND
// ════════════════════════════════════════════════════════════════════════════

// GET /api/lote/activo  → actualiza loteActivo / loteId
void consultarLoteActivo() {
  if (WiFi.status() != WL_CONNECTED) {
    if (backendConectado) Serial.println("[Backend] WiFi caido — sin conexion (mantengo ultimo estado)");
    backendConectado = false;
    return;                                  // un corte de WiFi NO cierra el lote
  }
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/lote/activo";
  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();
  if (code == 200) {
    if (!backendConectado) {                 // se recuperó la conexión
      backendConectado = true;
      Serial.println("[Backend] CONECTADO de nuevo");
    }
    String body = http.getString();
    if (body.indexOf("\"activo\":true") >= 0) {
      loteActivo = true;
      int i = body.indexOf("\"id\":");
      if (i >= 0) loteId = body.substring(i + 5).toInt();
    } else {
      loteActivo = false;                    // solo un "activo:false" explícito cierra
      loteId = -1;
    }
  } else {
    // Sin respuesta del backend: avisamos pero NO cambiamos loteActivo (evita
    // detener la línea por un bache de red transitorio).
    if (backendConectado) Serial.printf("[Backend] sin respuesta (HTTP %d) — mantengo ultimo estado\n", code);
    backendConectado = false;
  }
  http.end();
}

// POST /api/palta  (JSON armado a mano, sin ArduinoJson) → devuelve palta_id
int enviarPalta(int r, int g, int b, float lux, float t, float hr,
                int votosSana, int votosAntrac) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // temp/humedad como cadenas: número válido o literal "null" (JSON correcto)
  char tStr[16], hrStr[16];
  if (isnan(t))  strcpy(tStr,  "null"); else snprintf(tStr,  sizeof(tStr),  "%.1f", t);
  if (isnan(hr)) strcpy(hrStr, "null"); else snprintf(hrStr, sizeof(hrStr), "%.1f", hr);

  char body[320];
  // clasificacion/confianza van null mientras no haya modelos
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":null,\"confianza\":null,"
    "\"votos_sana\":%d,\"votos_antracnosis\":%d,"
    "\"r\":%d,\"g\":%d,\"b\":%d,\"lux\":%.1f,"
    "\"temp\":%s,\"humedad\":%s,"
    "\"ir_detectado\":true,\"lecturas\":%d}",
    loteId, votosSana, votosAntrac, r, g, b, lux,
    tStr, hrStr, VUELTAS_POR_PALTA);

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
void enviarFoto(camera_fb_t* fb, int paltaId) {
  if (!fb || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT +
               "/api/captura/foto?palta_id=" + paltaId + "&lote_id=" + loteId;
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(fb->buf, fb->len);
  if (code == 200) Serial.printf("[NET] POST foto OK (%u bytes)\n", fb->len);
  else             Serial.printf("[NET] POST foto HTTP %d\n", code);
  http.end();
}

// ════════════════════════════════════════════════════════════════════════════
//  HARDWARE / HELPERS
// ════════════════════════════════════════════════════════════════════════════
camera_fb_t* capturarFoto() {
  return esp_camera_fb_get();
}

// Convierte un canal crudo (0-65535) de la TCS a 0-255 normalizado por Clear
uint8_t map8(uint16_t canal, uint16_t clear) {
  if (clear == 0) return 0;
  long v = (long)canal * 255 / clear;
  if (v > 255) v = 255;
  return (uint8_t)v;
}

void arrancarCinta() { Serial.println("[CINTA] (simulado) ARRANCA"); }
void detenerCinta()  { Serial.println("[CINTA] (simulado) DETENIDA"); }

void detenerTodo() {
  detenerCinta();
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO,  LOW);
  digitalWrite(PIN_LED_AZUL,  LOW);
  Serial.println("[SISTEMA] todo detenido — esperando nuevo lote");
}

void parpadeoEspera() {
  if (millis() - ultimoParpadeo > 800) {
    ultimoParpadeo = millis();
    ledAzulEstado = !ledAzulEstado;
    digitalWrite(PIN_LED_AZUL, ledAzulEstado);
  }
}

void iniciarCamara() {
  // Ajustamos la config según haya o no PSRAM detectable. Si la placa no expone
  // PSRAM (o no está habilitada en Tools->PSRAM del IDE), pedir framebuffers en
  // PSRAM hace que el malloc reviente con "frame buffer malloc failed".
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
    config.frame_size  = FRAMESIZE_VGA;          // 640x480
    config.fb_count    = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode   = CAMERA_GRAB_LATEST;
  } else {
    // Sin PSRAM cabe muy poco en DRAM: bajamos resolución y único framebuffer.
    config.frame_size  = FRAMESIZE_QVGA;         // 320x240
    config.fb_count    = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[!!] ERROR camara: 0x%x — sigo SIN camara (resto del sistema funciona)\n", err);
  } else {
    Serial.println("[OK] Camara inicializada");
  }
}

void conectarWiFi() {
  WiFi.begin(SSID, PASSWORD);
  Serial.print("[WiFi] conectando a la red \""); Serial.print(SSID); Serial.print("\"");
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500); Serial.print("."); intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n[WiFi] CONECTADO  IP del ESP32: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[!!] WiFi NO conectado — revisa SSID/PASSWORD");
  }
}

// Hace un GET /health al backend; true si responde 200.
bool pingBackend() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/health";
  http.begin(url);
  http.setTimeout(3000);
  int code = http.GET();
  http.end();
  return (code == 200);
}

// Espera (hasta ~30s) a que el backend responda antes de empezar el flujo.
void esperarBackend() {
  Serial.printf("\n[Backend] esperando conexion con http://%s:%d ...", BACKEND_HOST, BACKEND_PORT);
  for (int intentos = 0; intentos < 30; intentos++) {
    if (pingBackend()) {
      backendConectado = true;
      Serial.println("\n[Backend] CONECTADO — el ESP32 ya habla con el backend de tu laptop");
      return;
    }
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\n[Backend] aun no responde — reintentare automaticamente mientras corre");
  Serial.println("          (revisa que el docker este arriba y la IP/puerto del backend)");
}
