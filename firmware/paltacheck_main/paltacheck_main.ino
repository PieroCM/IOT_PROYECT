/* ============================================================================
   PaltaCheck - Firmware unificado integrado (Motores + Sensores + Camara)
   ----------------------------------------------------------------------------
   Freenove ESP32-S3-WROOM N16R8 + L298N (Cinta y Rodillos) + Sensores
   ============================================================================ */

#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_TCS34725.h>
#include <ESP32Servo.h>

// --- CONFIG WiFi + Backend (laptop) -----------------------------------------
const char* SSID         = "LuO";
const char* PASSWORD     = "Jose030825";
const char* BACKEND_HOST = "10.47.144.60";   // IP IPv4 de tu laptop
const int   BACKEND_PORT = 8000;

// --- PINES CAMARA (fijos del board ESP32-S3-CAM, no tocar) ------------------
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

// --- PINES MOTORES (L298N - Reasignados sin conflicto con la camara) --------
// L298N #1 - Cinta
const int ENA_CINTA = 46; // Reasignado para evitar conflicto con IN4_RODILLO2 (47)
const int IN1_CINTA = 48;
const int IN2_CINTA = 35;

// L298N #2 - Rodillos (Giro y Descarte mecanico)
const int ENA_RODILLO1 = 42;
const int IN1_RODILLO1 = 41;
const int IN2_RODILLO1 = 40;
const int ENB_RODILLO2 = 39;
const int IN3_RODILLO2 = 38;
const int IN4_RODILLO2 = 47;

// --- PINES SENSORES Y ACTUADORES (Reasignados para evitar choque con Rodillos) --
#define PIN_DHT22   2     // DHT22 DATA
#define I2C_SDA     14    // TCS34725 SDA
#define I2C_SCL     21    // TCS34725 SCL
#define PIN_IR      1     // FC-51 OUT (LOW = objeto detectado)

#define PIN_SERVO      3  // GPIO 3 para el control de la compuerta (Servo)
#define PIN_LED_ROJO   36 // Reasignado al GPIO 36 (LED Rojo)
#define PIN_LED_VERDE  37 // Reasignado al GPIO 37 (LED Verde)
#define PIN_LED_AZUL   -1 // Deshabilitado físicamente para liberar pines de motores

// --- CONFIG PWM ESP32 (Core v3.x API) ---------------------------------------
const int freqPWM = 1000;
const int resolucionPWM = 8;

// Velocidades de motores
const int velocidadCinta = 100;
const int potenciaGiro   = 45;
const int potenciaBotar  = 120;

// Tiempos del ciclo y secuencia
const unsigned long tiempoGiroRodillos   = 3000;
const unsigned long tiempoBotar          = 4000;
const int           VUELTAS_POR_PALTA    = 3;
const int           tiempoPulsoGiroOn    = 50;
const int           tiempoPulsoGiroOff   = 1000;
const int           DELAY_ESTABILIZA_MS  = 400; // espera para foto quieta

#define POLL_LOTE_MS        5000     // consulta lote activo
#define IR_DEBOUNCE_MS        50     // debounce del sensor optico
#define IR_WAIT_RELEASE_MS 10000     // timeout para salida de palta
#define IR_COOLDOWN_MS       500     // cooldown final

// --- Sensores y Servo -------------------------------------------------------
DHT dht(PIN_DHT22, DHT22);
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
    TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

Servo servoCompuerta;
const int ANGULO_INICIAL  = 90;   // Compuerta en el centro
const int ANGULO_SANA     = 135;  // Compuerta para palta sana (+45 grados)
const int ANGULO_DESCARTE = 45;   // Compuerta para descarte (-45 grados)
int posicionActual        = 90;
const int velocidadMovimiento = 3; // Retardo en ms para movimiento suave

// --- Estado del sistema -----------------------------------------------------
enum Estado { SIN_LOTE, ESPERANDO_PALTA, PROCESANDO };
Estado   estado          = SIN_LOTE;
bool     loteActivo      = false;
int      loteId          = -1;
char     loteCodigo[64]  = "";       
uint32_t ultimoPollMs    = 0;
uint32_t ultimoParpadeo  = 0;
uint32_t ultimoEsperaMs  = 0;        
bool     ledAzulEstado   = false;
bool     backendConectado = false;   
bool     camaraOK         = false;

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n================ PaltaCheck - firmware unificado ================");

  // Salidas del puente H (L298N)
  pinMode(IN1_CINTA, OUTPUT);     pinMode(IN2_CINTA, OUTPUT);
  pinMode(IN1_RODILLO1, OUTPUT);  pinMode(IN2_RODILLO1, OUTPUT);
  pinMode(IN3_RODILLO2, OUTPUT);  pinMode(IN4_RODILLO2, OUTPUT);

  // Salidas y entradas de sensores / actuadores
  pinMode(PIN_IR,        INPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_LED_AZUL,  OUTPUT);

  // 1) Primero inicializamos la camara para asegurar sus recursos de LEDC (PWM)
  iniciarCamara();

  // Adjuntar PWM a pines habilitadores usando ledcAttach (ESP32 Core v3)
  ledcAttach(ENA_CINTA, freqPWM, resolucionPWM);
  ledcAttach(ENA_RODILLO1, freqPWM, resolucionPWM);
  ledcAttach(ENB_RODILLO2, freqPWM, resolucionPWM);

  // Inicializar servo en posicion de reposo (centro)
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(posicionActual);
  delay(200);

  detenerTodo();

  dht.begin();
  Wire.begin(I2C_SDA, I2C_SCL);

  if (tcs.begin()) Serial.println("[OK] TCS34725 detectado");
  else             Serial.println("[!!] TCS34725 NO encontrado (revisa I2C)");

  conectarWiFi();
  if (WiFi.status() == WL_CONNECTED) esperarBackend();   
  Serial.println("[i] Sistema en espera de lote (abre uno desde el dashboard)\n");
}

// ============================================================================
//  LOOP - maquina de estados
// ============================================================================
void loop() {
  // 1) Consultar periodicamente si hay lote activo (senal del frontend)
  if (millis() - ultimoPollMs > POLL_LOTE_MS) {
    ultimoPollMs = millis();
    bool antes = loteActivo;
    consultarLoteActivo();

    if (loteActivo && !antes) {            // se abrio un lote
      Serial.printf("\n>>> LOTE ACTIVO  #%s  (id=%d)  ->  arrancando cinta\n", loteCodigo, loteId);
      Serial.println("    coloca una palta frente al sensor IR para procesarla");
      cintaAdelante(velocidadCinta);
      estado = ESPERANDO_PALTA;
    }
    if (!loteActivo && antes) {            // se cerro el lote
      Serial.printf("\n<<< LOTE CERRADO  (#%s)  ->  deteniendo todo el sistema\n", loteCodigo);
      loteCodigo[0] = '\0';
      detenerTodo();
      estado = SIN_LOTE;
    }
  }

  // 2) Comportamiento segun estado
  switch (estado) {
    case SIN_LOTE:
      parpadeoEspera();                    // LED azul parpadeo lento
      break;

    case ESPERANDO_PALTA:
      if (digitalRead(PIN_IR) == LOW) {                    // FC-51: LOW = palta detectada
        delay(IR_DEBOUNCE_MS);
        if (digitalRead(PIN_IR) != LOW) { parpadeoEspera(); break; }

        Serial.println("\n[IR] Palta detectada en la ranura");
        procesarPalta();

        // Esperar a que la palta deje la ranura (eject)
        Serial.println("[IR] esperando a que la palta deje la ranura...");
        uint32_t t0 = millis();
        while (digitalRead(PIN_IR) == LOW && millis() - t0 < IR_WAIT_RELEASE_MS) {
          delay(20);
        }
        if (digitalRead(PIN_IR) == LOW) {
          Serial.println("[IR] timeout esperando release - sigo igual (revisa el FC-51)");
        }
        delay(IR_COOLDOWN_MS);              
        estado = ESPERANDO_PALTA;           // listo para la siguiente palta
        cintaAdelante(velocidadCinta);       // reanudar cinta transportadora
      } else {
        parpadeoEspera();
        if (millis() - ultimoEsperaMs > 5000) {
          ultimoEsperaMs = millis();
          Serial.printf("[ESPERA] lote #%s · IR=%s\n", loteCodigo,
                        digitalRead(PIN_IR) == LOW ? "LOW (detecta palta)" : "HIGH (sin palta)");
        }
      }
      break;

    case PROCESANDO:
      break;                               // se maneja dentro de procesarPalta()
  }
}

// ============================================================================
//  CICLO DE UNA PALTA (3 vueltas)
// ============================================================================
void procesarPalta() {
  estado = PROCESANDO;
  Serial.println("──────────────────────────────────────────────");
  detenerCinta();
  digitalWrite(PIN_LED_AZUL, HIGH);
  Serial.println("[LED AZUL] ON - procesando palta");

  // Acumuladores de sensores
  float sumR = 0, sumG = 0, sumB = 0, sumLux = 0, sumT = 0, sumHR = 0;
  int   lecturasValidas = 0;
  camera_fb_t* ultimaFoto = nullptr;

  // Votos de clasificacion por etiqueta: [0]=no_palta [1]=sana [2]=antracnosis [3]=scab
  int   votos[4]   = {0, 0, 0, 0};
  float sumConf[4] = {0, 0, 0, 0};   
  float maxConf[4] = {0, 0, 0, 0};   

  for (int v = 1; v <= VUELTAS_POR_PALTA; v++) {
    Serial.printf("\n--- Vuelta %d/%d ---\n", v, VUELTAS_POR_PALTA);

    // Motor de rodillos gira la palta con un salto corto
    Serial.println("Palta en rodillos - dando un salto para rotar");
    darSaltoRodillos(500); // Salto de giro de 500 ms
    
    Serial.println("Esperando 3 segundos a que la palta se estabilice...");
    delay(3000); // Demora de 3 segundos antes de la foto

    // Camara captura foto y la clasifica en servidor
    camera_fb_t* fb = capturarFoto();
    if (fb) {
      Serial.printf("[CAM] foto_%d capturada: %u bytes\n", v, fb->len);
      char etiqueta[20]; float conf = 0;
      if (clasificarFotoEnServidor(fb, etiqueta, sizeof(etiqueta), &conf)) {
        int idx = indiceEtiqueta(etiqueta);   // 0=no_palta 1=sana 2=antracnosis 3=scab
        if (idx >= 0) {
          votos[idx]++; sumConf[idx] += conf;
          if (conf > maxConf[idx]) maxConf[idx] = conf;
          Serial.printf("[ML] vuelta %d -> %s (%.0f%%)\n", v, etiqueta, conf * 100);
        }
      } else {
        Serial.println("[ML] no se pudo clasificar esta vuelta (sin voto)");
      }
      if (ultimaFoto) esp_camera_fb_return(ultimaFoto);
      ultimaFoto = fb;                     // guardamos la ultima para subirla
    } else {
      Serial.println("[CAM] ERROR al capturar frame (sin foto -> sin voto esta vuelta)");
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
      Serial.println("[DHT] lectura invalida (NaN) - revisa pull-up");
    } else {
      Serial.printf("[DHT] Temp=%.1f°C  HR=%.1f%%\n", t, hr);
    }

    // Acumular (RGB crudo a 0-255)
    sumR += map8(r, c);
    sumG += map8(g, c);
    sumB += map8(b, c);
    sumLux += lux;
    if (!isnan(t) && !isnan(hr)) { sumT += t; sumHR += hr; lecturasValidas++; }

    delay(100);
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

  // -- Veredicto por votacion (guardia de seguridad) --
  char  clasif[20] = "";          
  float confFinal  = -1;          
  int   nNoPalta = votos[0], nSana = votos[1], nAntrac = votos[2], nScab = votos[3];
  int   rondasPalta = nSana + nAntrac + nScab;

  if (nNoPalta >= 2) {
    strcpy(clasif, "no_es_palta");                       
    confFinal = nNoPalta ? sumConf[0] / nNoPalta : 0;
  } else if (rondasPalta == 0) {
    Serial.println("[VEREDICTO] sin clasificaciones validas -> se envia null");
  } else if (nSana >= 2 && nSana > nAntrac && nSana > nScab) {
    strcpy(clasif, "sana");
    confFinal = sumConf[1] / nSana;
  } else {
    if (nAntrac > 0 && (nScab == 0 || maxConf[2] >= maxConf[3])) {
      strcpy(clasif, "antracnosis");
      confFinal = sumConf[2] / nAntrac;
    } else {
      strcpy(clasif, "scab");
      confFinal = sumConf[3] / nScab;
    }
  }

  bool esSana = (strcmp(clasif, "sana") == 0);
  Serial.printf("[VEREDICTO] %s  (conf %.0f%%)  votos S=%d A=%d C=%d NP=%d\n",
                clasif[0] ? clasif : "null", confFinal >= 0 ? confFinal * 100 : 0,
                nSana, nAntrac, nScab, nNoPalta);
  
  // Retroalimentacion fisica
  digitalWrite(PIN_LED_VERDE, esSana ? HIGH : LOW);
  digitalWrite(PIN_LED_ROJO,  esSana ? LOW  : HIGH);

  // Enviar metadatos al backend: primero la palta, luego la foto
  int paltaId = enviarPalta(rProm, gProm, bProm, luxProm, tProm, hrProm,
                            clasif, confFinal, nSana, nAntrac, nScab);
  if (ultimaFoto) {
    if (paltaId > 0) {
      enviarFoto(ultimaFoto, paltaId);
    }
    esp_camera_fb_return(ultimaFoto);
    ultimaFoto = nullptr;
  }

  delay(1000); 
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO,  LOW);

  // Paso 4: Mover servo compuerta segun el resultado de la clasificacion
  Serial.println("4) Moviendo compuerta del servo");
  if (esSana) {
    moverCompuerta(ANGULO_SANA);
  } else {
    moverCompuerta(ANGULO_DESCARTE);
  }
  delay(400); // Esperar a que la compuerta se posicione bien

  // Paso 5: Eyectar y finalizar ciclo
  Serial.println("5) Botando la palta");
  botarPalta(tiempoBotar);

  // Regresar compuerta al centro (posicion inicial) para recibir la siguiente palta
  moverCompuerta(ANGULO_INICIAL);
  delay(300);

  Serial.println("6) Proceso terminado");
  digitalWrite(PIN_LED_AZUL, LOW);
}

// --- CONTROLADORES FISICOS (L298N) ------------------------------------------
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

void rodillosParaGirarPalta(int velocidad1, int velocidad2) {
  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, velocidad1);

  digitalWrite(IN3_RODILLO2, LOW);
  digitalWrite(IN4_RODILLO2, HIGH);
  ledcWrite(ENB_RODILLO2, velocidad2);
}

void rodillosParaBotarPalta(int velocidad1, int velocidad2) {
  digitalWrite(IN1_RODILLO1, LOW);
  digitalWrite(IN2_RODILLO1, HIGH);
  ledcWrite(ENA_RODILLO1, velocidad1);

  digitalWrite(IN3_RODILLO2, HIGH);
  digitalWrite(IN4_RODILLO2, LOW);
  ledcWrite(ENB_RODILLO2, velocidad2);
}

void darSaltoRodillos(int duracionMs) {
  rodillosParaGirarPalta(potenciaGiro, potenciaGiro);
  delay(duracionMs);
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

// --- COMUNICACION CON EL BACKEND --------------------------------------------
void consultarLoteActivo() {
  if (WiFi.status() != WL_CONNECTED) {
    if (backendConectado) Serial.println("[Backend] WiFi caido - sin conexion");
    backendConectado = false;
    return;
  }
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/lote/activo";
  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();
  if (code == 200) {
    if (!backendConectado) {
      backendConectado = true;
      Serial.println("[Backend] Conexion establecida");
    }
    String body = http.getString();
    if (body.indexOf("\"activo\":true") >= 0) {
      loteActivo = true;
      int i = body.indexOf("\"id\":");
      if (i >= 0) loteId = body.substring(i + 5).toInt();
      int ic = body.indexOf("\"codigo\":\"");
      if (ic >= 0) {
        ic += 10;
        int fin = body.indexOf('"', ic);
        if (fin > ic) body.substring(ic, fin).toCharArray(loteCodigo, sizeof(loteCodigo));
      }
    } else {
      loteActivo = false;
      loteId = -1;
      loteCodigo[0] = '\0';
    }
  } else {
    if (backendConectado) Serial.printf("[Backend] sin respuesta (HTTP %d)\n", code);
    backendConectado = false;
  }
  http.end();
}

int indiceEtiqueta(const char* e) {
  if (strcmp(e, "no_palta")    == 0) return 0;
  if (strcmp(e, "sana")        == 0) return 1;
  if (strcmp(e, "antracnosis") == 0) return 2;
  if (strcmp(e, "scab")        == 0) return 3;
  return -1;
}

bool clasificarFotoEnServidor(camera_fb_t* fb, char* etiquetaOut, size_t outSize, float* confOut) {
  if (!fb || WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/clasificar";
  http.begin(url);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(8000);
  int code = http.POST(fb->buf, fb->len);
  bool ok = false;
  if (code == 200) {
    String resp = http.getString();
    int ie = resp.indexOf("\"etiqueta\":\"");
    int ic = resp.indexOf("\"confianza\":");
    if (ie >= 0) {
      ie += 12;
      int fin = resp.indexOf('"', ie);
      if (fin > ie) {
        resp.substring(ie, fin).toCharArray(etiquetaOut, outSize);
        if (ic >= 0) *confOut = resp.substring(ic + 12).toFloat();
        ok = true;
      }
    }
  } else {
    Serial.printf("[NET] POST /api/clasificar HTTP %d\n", code);
  }
  http.end();
  return ok;
}

int enviarPalta(int r, int g, int b, float lux, float t, float hr,
                const char* clasif, float confFinal, int vS, int vA, int vC) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  HTTPClient http;
  String url = String("http://") + BACKEND_HOST + ":" + BACKEND_PORT + "/api/palta";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  char tStr[16], hrStr[16], clasStr[24], confStr[16];
  if (isnan(t))  strcpy(tStr,  "null"); else snprintf(tStr,  sizeof(tStr),  "%.1f", t);
  if (isnan(hr)) strcpy(hrStr, "null"); else snprintf(hrStr, sizeof(hrStr), "%.1f", hr);
  if (clasif && clasif[0]) snprintf(clasStr, sizeof(clasStr), "\"%s\"", clasif);
  else                     strcpy(clasStr, "null");
  if (confFinal >= 0) snprintf(confStr, sizeof(confStr), "%.4f", confFinal);
  else                strcpy(confStr, "null");

  char body[360];
  snprintf(body, sizeof(body),
    "{\"lote_id\":%d,\"clasificacion\":%s,\"confianza\":%s,"
    "\"votos_sana\":%d,\"votos_antracnosis\":%d,\"votos_scab\":%d,"
    "\"r\":%d,\"g\":%d,\"b\":%d,\"lux\":%.1f,"
    "\"temp\":%s,\"humedad\":%s,"
    "\"ir_detectado\":true,\"lecturas\":%d}",
    loteId, clasStr, confStr, vS, vA, vC, r, g, b, lux, tStr, hrStr, VUELTAS_POR_PALTA);

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

// --- PERIFERICOS / HELPERS --------------------------------------------------
void iniciarCamara() {
  bool hayPSRAM = psramFound();
  Serial.printf("[CAM] PSRAM %s\n", hayPSRAM ? "detectada" : "NO encontrada");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_4;  // Cambiado a CHANNEL_4 para evitar conflicto con los canales asignados a los motores
  config.ledc_timer   = LEDC_TIMER_1;    // Cambiado a TIMER_1 para evitar conflictos
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
    config.frame_size  = FRAMESIZE_QVGA;         // 320x240
    config.fb_count    = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  camaraOK = (err == ESP_OK);
  if (camaraOK) {
    Serial.println("[OK] Camara inicializada correctamente en LEDC_CHANNEL_4");
  } else {
    Serial.printf("[!!] ERROR al inicializar la camara. Codigo de error: 0x%x\n", err);
    Serial.println("Revisa si has habilitado PSRAM en el menu de Arduino IDE (Tools -> PSRAM -> OPI PSRAM).");
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
    Serial.print("\n[WiFi] CONECTADO  IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[!!] WiFi NO conectado - revisa SSID/PASSWORD");
  }
}

void esperarBackend() {
  Serial.printf("\n[Backend] esperando conexion con http://%s:%d ...", BACKEND_HOST, BACKEND_PORT);
  for (int intentos = 0; intentos < 30; intentos++) {
    if (pingBackend()) {
      backendConectado = true;
      Serial.println("\n[Backend] CONECTADO.");
      return;
    }
    Serial.print(".");
    delay(1000);
  }
}

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

void parpadeoEspera() {
  if (millis() - ultimoParpadeo > 800) {
    ultimoParpadeo = millis();
    ledAzulEstado = !ledAzulEstado;
    digitalWrite(PIN_LED_AZUL, ledAzulEstado);
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

// Mueve la compuerta del servo de manera suave (para no golpear el chasis)
void moverCompuerta(int nuevaPos) {
  nuevaPos = constrain(nuevaPos, 0, 180);
  Serial.printf("[SERVO] Moviendo compuerta: %d -> %d grados\n", posicionActual, nuevaPos);

  if (nuevaPos > posicionActual) {
    for (int pos = posicionActual; pos <= nuevaPos; pos++) {
      servoCompuerta.write(pos);
      delay(velocidadMovimiento);
    }
  } else {
    for (int pos = posicionActual; pos >= nuevaPos; pos--) {
      servoCompuerta.write(pos);
      delay(velocidadMovimiento);
    }
  }
  posicionActual = nuevaPos;
  Serial.printf("[SERVO] Compuerta posicionada en %d grados\n", posicionActual);
}

