# PaltaCheck — Explicación del firmware del ESP32

**Archivo:** `firmware/paltacheck_main/paltacheck_main.ino`
**Placa:** ESP32-S3-CAM (WROOM-1 N16R8) con cámara OV2640

Este documento explica, en orden, **qué hace el script**, **qué pin va con cada sensor** y **cómo fluye el programa** (la máquina de estados). Es la primera pieza de la serie; las siguientes (backend, base de datos) van en los otros documentos de `docs/`.

---

## 1. Qué hace este script en una frase

El ESP32 **no decide nada por su cuenta**: pregunta cada 5 segundos al backend si hay un lote abierto. Cuando el frontend abre un lote, el ESP32 arranca, espera que el sensor IR detecte una palta, le da 3 vueltas leyendo los sensores y la cámara, promedia esas 3 lecturas, las manda al backend y vuelve a esperar la siguiente palta. Cuando el frontend cierra el lote, el ESP32 detiene todo.

En esta fase, lo que **se lee de verdad** son los sensores que ya probaste (DHT22, TCS34725, FC-51) y la cámara. Lo que todavía **no está integrado** (LEDs, motores, servo, buzzer, OLED y los modelos TFLite) se **simula imprimiendo la secuencia por el monitor serial con sus delays**, para que veas todo el proceso sin depender de ese hardware.

---

## 2. Mapa de pines por sensor

### 2.1 Sensores y actuadores

| Componente | Señal | GPIO en el código | Notas |
|---|---|---|---|
| **DHT22** (temp/humedad) | DATA | **GPIO 2** | Antes era 16 (lo movimos, ver §2.3). Necesita pull-up de 4.7 kΩ entre DATA y 3V3 |
| **TCS34725** (color RGB) | SDA | **GPIO 14** | Bus I²C, dirección fija 0x29 |
| **TCS34725** (color RGB) | SCL | **GPIO 21** | Antes era 15 (lo movimos, ver §2.3) |
| **FC-51** (infrarrojo) | OUT | **GPIO 1** | Antes era 17 (lo movimos). **Lógica invertida: LOW = palta detectada** |
| LED verde *(simulado)* | — | GPIO 39 | Solo se anuncia por Serial; ajusta el pin cuando lo conectes |
| LED rojo *(simulado)* | — | GPIO 40 | idem |
| LED azul *(simulado)* | — | GPIO 38 | "procesando" / parpadeo en espera |
| Buzzer *(simulado)* | — | GPIO 41 | idem |
| Servo SG90 *(simulado)* | — | GPIO 42 | idem |

> Los pines marcados *(simulado)* son `#define` que puedes cambiar libremente: en esta fase no se controla el hardware, solo se imprime la acción por Serial.

### 2.2 Pines de la cámara (fijos, **no se tocan**)

La cámara del ESP32-S3-CAM viene cableada de fábrica a estos pines. No se pueden mover:

| Señal | GPIO | Señal | GPIO |
|---|---|---|---|
| XCLK | 15 | D3 | 10 |
| SIOD (SDA cámara) | 4 | D2 | 8 |
| SIOC (SCL cámara) | 5 | D1 | 9 |
| D7 | 16 | D0 | 11 |
| D6 | 17 | VSYNC | 6 |
| D5 | 18 | HREF | 7 |
| D4 | 12 | PCLK | 13 |
| PWDN | -1 (no usado) | RESET | -1 (no usado) |

### 2.3 ⚠️ Por qué movimos los pines de los sensores

Tu documentación original ponía **DHT22 en 16, TCS-SCL en 15 e IR en 17**. Pero la cámara **ya usa esos mismos pines** (15 = XCLK, 16 = D7, 17 = D6). Como probaste cámara y sensores en sketches separados, nunca chocaron; pero en el firmware unificado están todos juntos, así que **moví los sensores a pines que la cámara no usa**:

```
DHT22 : 16  →  2
TCS-SCL: 15  →  21
IR    : 17  →  1
(TCS-SDA seguía libre en 14, se quedó en 14)
```

👉 **Acción tuya:** confirma que los GPIO 1, 2 y 21 estén expuestos y libres en tu placa concreta. Si tu placa no los expone, cámbialos en el bloque `PINES SENSORES` del `.ino`.

---

## 3. Librerías necesarias (Arduino IDE)

Instala desde el gestor de librerías:

- **DHT sensor library** (Adafruit) — para el DHT22
- **Adafruit TCS34725** — para el sensor de color
- *(la cámara y WiFi/HTTP ya vienen con el core de ESP32)*

---

## 4. Configuración antes de flashear

Arriba del archivo, edita estas 4 líneas:

```cpp
const char* SSID         = "TU_RED_WIFI";       // nombre de tu WiFi
const char* PASSWORD     = "TU_CONTRASENA";     // clave de tu WiFi
const char* BACKEND_HOST = "192.168.1.100";     // IP de TU LAPTOP en la red
const int   BACKEND_PORT = 8000;                // puerto del backend
```

> Para saber la IP de tu laptop: en Windows abre `cmd` y escribe `ipconfig` (busca "Dirección IPv4"). El ESP32 y la laptop deben estar en la **misma red WiFi**.

---

## 5. El flujo: máquina de estados

El programa tiene 3 estados. La transición entre ellos la dispara, sobre todo, la respuesta del backend a `GET /api/lote/activo`.

```
                 ┌─────────────────────────────────────────────┐
                 │                                             │
                 ▼                                             │
        ┌──────────────────┐                                  │
        │    SIN_LOTE       │   LED azul parpadea lento        │
        │  (sistema en      │   Pollea /api/lote/activo cada 5s │
        │   espera)         │                                  │
        └────────┬─────────┘                                  │
                 │  frontend abre lote → backend responde       │
                 │  {"activo": true}                            │
                 ▼                                              │
        ┌──────────────────┐                                  │
        │ ESPERANDO_PALTA   │   Cinta corriendo (simulada)      │
        │                   │   Lee el sensor IR continuamente   │
        └────────┬─────────┘                                  │
                 │  IR == LOW (palta en la ranura)              │
                 ▼                                              │
        ┌──────────────────┐                                  │
        │   PROCESANDO      │   Ciclo de 3 vueltas (ver §6)      │
        │                   │   → POST palta + foto al backend   │
        └────────┬─────────┘                                  │
                 │  ciclo terminado                             │
                 └──────────────► vuelve a ESPERANDO_PALTA      │
                                                                │
   En cualquier momento, si el backend responde {"activo": false}
   (frontend cerró el lote) → detenerTodo() → vuelve a SIN_LOTE ─┘
```

### Qué hace cada estado

- **SIN_LOTE** — el sistema está apagado/en espera. El LED azul parpadea lento. Cada 5 s pregunta al backend si hay lote. Cuando aparece uno, arranca la cinta y pasa a `ESPERANDO_PALTA`.
- **ESPERANDO_PALTA** — la cinta corre y el ESP32 vigila el sensor IR. Apenas el IR detecta una palta (su salida pasa a `LOW`), entra al ciclo de procesamiento. Si mientras tanto el lote se cierra, detiene todo y vuelve a `SIN_LOTE`.
- **PROCESANDO** — ejecuta el ciclo de 3 vueltas de una palta (detallado abajo) y, al terminar, regresa a `ESPERANDO_PALTA` para la siguiente.

### El "latido" cada 5 segundos

En cada vuelta del `loop()`, si pasaron más de 5 s (`POLL_LOTE_MS`), llama a `consultarLoteActivo()`. Esa función hace el `GET /api/lote/activo` y actualiza dos variables: `loteActivo` (true/false) y `loteId`. Comparando con el estado anterior, detecta si el lote **acaba de abrirse** (arranca) o **acaba de cerrarse** (para). Así el frontend tiene el control total del arranque y la parada.

---

## 6. El ciclo por palta (función `procesarPalta()`)

Cuando el IR detecta una palta, se ejecuta esto:

```
1. Detener la cinta · LED azul ON ("procesando")

2. REPETIR 3 veces (las 3 vueltas):
   ├─ Motor rota la palta ~120°        (simulado: Serial + delay 1200 ms)
   ├─ Motor se detiene · palta quieta  (delay 400 ms para estabilizar)
   ├─ 📷 Cámara captura una foto        (REAL → esp_camera_fb_get)
   ├─ 🧠 Modelo TFLite clasifica        (pendiente: solo Serial por ahora)
   ├─ 🎨 TCS34725 lee R, G, B, Lux      (REAL)
   └─ 🌡️ DHT22 lee temperatura y HR     (REAL)
      → se acumulan las lecturas

3. Promediar las 3 lecturas (RGB, Lux, Temp, HR)

4. Veredicto                            (pendiente: lo decidirán los modelos)

5. Acción física: servo + LED verde/rojo (simulado por Serial)

6. Enviar al backend:
   ├─ POST /api/palta  → recibe el palta_id
   └─ POST /api/captura/foto?palta_id=...&lote_id=...  (sube el JPEG)

7. OLED actualiza contador (simulado) · LED apaga · cinta arranca de nuevo
```

**Detalle del color (función `map8`):** el TCS34725 entrega cada canal en crudo de 0 a 65535. Como la base de datos guarda R/G/B en escala 0–255, el código normaliza cada canal dividiéndolo por el canal "Clear" (luz total) y lo escala a 0–255. Así el color queda comparable independientemente de cuánta luz haya.

---

## 7. Qué es real y qué está simulado (resumen)

| Elemento | Estado | Cómo se comporta hoy |
|---|---|---|
| DHT22 (temp/HR) | ✅ Real | Lectura real con `dht.readTemperature()` / `readHumidity()` |
| TCS34725 (color) | ✅ Real | Lectura real con `tcs.getRawData()` + `calculateLux()` |
| FC-51 (IR) | ✅ Real | `digitalRead()` real; dispara el ciclo |
| Cámara OV2640 | ✅ Real | Captura JPEG real y lo sube al backend |
| WiFi + HTTP | ✅ Real | GET lote activo, POST palta, POST foto |
| Motores / cinta | 🟡 Simulado | `Serial.println` + delay (no mueve hardware) |
| Servo | 🟡 Simulado | `Serial.println` |
| LEDs / Buzzer / OLED | 🟡 Simulado | `Serial.println` (los LEDs sí hacen `digitalWrite`, inofensivo) |
| Modelos TFLite | 🔜 Pendiente | Placeholder por Serial; se integran en la siguiente fase |

---

## 8. Comunicación con el backend (3 llamadas HTTP)

| Función en el `.ino` | Llamada | Para qué |
|---|---|---|
| `consultarLoteActivo()` | `GET /api/lote/activo` | Saber si arrancar o parar (cada 5 s) |
| `enviarPalta(...)` | `POST /api/palta` | Mandar el resultado promediado; devuelve `palta_id` |
| `enviarFoto(...)` | `POST /api/captura/foto?palta_id=&lote_id=` | Subir el JPEG asociado a esa palta |

**Sin ArduinoJson:** el JSON del `POST /api/palta` se arma a mano con `snprintf` (son solo números), tal como venías evitando la dependencia de ArduinoJson. La foto se manda como **bytes crudos** en el cuerpo con `Content-Type: image/jpeg`, y el `palta_id`/`lote_id` viajan como parámetros en la URL (fáciles de concatenar en Arduino).

---

## 9. Cómo probarlo y qué verás en el Monitor Serial

1. Levanta el backend en tu laptop (`docker compose up` o el de pruebas).
2. Configura WiFi + IP de la laptop en el `.ino` y flashea la placa.
3. Abre el **Monitor Serial a 115200 baudios**.
4. Desde el dashboard, haz clic en **Abrir Lote**.

Verás algo así:

```
================ PaltaCheck — firmware unificado ================
[OK] Camara inicializada
[OK] TCS34725 detectado
[WiFi] OK  IP: 192.168.1.42
[WiFi] backend -> http://192.168.1.100:8000
[i] Sistema en espera de lote (abre uno desde el dashboard)

>>> LOTE ACTIVO id=8  →  arrancando cinta
[CINTA] (simulado) ARRANCA

[IR] Palta detectada en la ranura
[CINTA] (simulado) DETENIDA
[LED AZUL] ON — procesando palta

--- Vuelta 1/3 ---
[MOTOR] rotando palta ~120°  (delay 1200 ms)
[MOTOR] detenido — palta quieta
[CAM] foto_1 capturada: 18432 bytes
[ML] clasificacion pendiente (modelos TFLite aun no integrados)
[TCS] R=112 G=141 B=85  Lux=415.0
[DHT] Temp=22.4°C  HR=64.8%
... (vueltas 2 y 3) ...

[PROMEDIOS]
  RGB=(110,139,84)  Lux=412.0  Temp=22.4  HR=64.8
[NET] POST /api/palta OK  palta_id=47
[NET] POST foto OK (18432 bytes)
[i] Esperando siguiente palta...
```

Al hacer clic en **Cerrar Lote** verás:

```
<<< LOTE CERRADO  →  deteniendo todo el sistema
[CINTA] (simulado) DETENIDA
[SISTEMA] todo detenido — esperando nuevo lote
```

---

## Siguiente fase

Cuando integres los **modelos TFLite**, las líneas `[ML] clasificacion pendiente...` se reemplazan por la inferencia real (palta sí/no + sana/antracnosis), se llenan `votos_sana`/`votos_antracnosis` y el veredicto enciende el LED y mueve el servo de verdad.
