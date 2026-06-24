# PaltaCheck — Resumen del sistema completo

Clasificador automático de paltas (sana vs. enferma) sobre una línea con cinta y
rodillos, controlado por un **ESP32‑S3** y un **dashboard web**. La **IA corre en
el backend**: el ESP32 manda la foto y obedece el veredicto que le devuelve.

Firmware de referencia: [`firmware/paltacheck_flujo_completo/paltacheck_flujo_completo.ino`](../firmware/paltacheck_flujo_completo/paltacheck_flujo_completo.ino)

---

## 1. Arquitectura general

```
   ┌─────────────┐   abre/cierra lote   ┌──────────────┐   GET /api/lote/activo
   │  Frontend   │ ───────────────────► │   Backend    │ ◄──────────────────────┐
   │  (Vue 3)    │ ◄─── KPIs / fotos ── │  (FastAPI)   │                        │
   └─────────────┘   polling cada 3 s   │  + MODELO IA │   POST /api/palta       │
          ▲                             │  (MobileNet) │ ◄── POST /api/captura/foto
          │ muestra foto + veredicto    └──────┬───────┘     (JPEG) → veredicto  │
          │                                    │ guarda                          │
          │                             ┌──────▼───────┐                  ┌──────┴──────┐
          └──────── KPIs / tabla ────── │ TimescaleDB  │                  │   ESP32-S3  │
                                        │ (PostgreSQL) │                  │  cinta/rod. │
                                        └──────────────┘                  │  cámara/IR  │
                                                                          └─────────────┘
```

- **Placa:** ESP32‑S3 N16R8 (16 MB flash, 8 MB PSRAM octal) con cámara **OV3660** por flex y dos USB‑C (USB‑UART + USB‑OTG).
- **Decisión:** el modelo clasifica cada foto; 3 fotos por palta **votan** y gana la mayoría.
- **Persistencia:** la BD guarda lote/palta/sensores y la **ruta** de la foto (el JPG vive en disco).

---

## 2. Mapa de pines (firmware final)

> ⚠️ La cámara ocupa pines FIJOS (van al conector flex, no se pueden mover). Todo
> lo demás se eligió para **no chocar** con la cámara ni entre sí.

### 2.1 Cámara OV3660 — FIJOS (no tocar)
| Señal | GPIO | Señal | GPIO | Señal | GPIO |
|---|---|---|---|---|---|
| XCLK | 15 | D7 | 16 | D2 | 8 |
| SIOD (SDA) | 4 | D6 | 17 | D1 | 9 |
| SIOC (SCL) | 5 | D5 | 18 | D0 | 11 |
| VSYNC | 6 | D4 | 12 | PCLK | 13 |
| HREF | 7 | D3 | 10 | PWDN / RESET | −1 |

### 2.2 Sensores
| Sensor | Señal | GPIO |
|---|---|---|
| **TCS34725** (color/lux) | SDA | **14** |
| | SCL | **21** |
| **DHT22** (temp/humedad) | DATA | **2** |
| **FC‑51 IR** (presencia) | OUT | **1** (LOW = fruta detectada) |

### 2.3 Actuadores — Motores L298N
| Bloque | Señal | GPIO | Notas |
|---|---|---|---|
| **Cinta** (L298N #1) | ENA | **46** | PWM (velocidad) |
| | IN1 | **48** | IN2 → GND directo (un solo sentido) |
| **Rodillo 1** (L298N #2) | ENA | **42** | PWM |
| | IN1 / IN2 | **41 / 40** | dirección |
| **Rodillo 2** (L298N #2) | ENB | **39** | PWM |
| | IN3 / IN4 | **38 / 47** | dirección |

### 2.4 Servo (compuerta de rechazo)
| Señal | GPIO | Notas |
|---|---|---|
| Servo PWM | **0** | único pin libre restante (BOOT, sirve como salida). 5 V aparte + GND común. |

**PWM:** motores a 1000 Hz / 8 bits (duty 0–255). Servo por LEDC directo a 50 Hz /
16 bits (pulso 500–2400 µs en ventana de 20 ms) — se hizo así a propósito para
**evitar el conflicto de timers de ESP32Servo con la cámara** (hacía temblar el servo).

> 🔌 Imprescindible: **GND común** entre ESP32, ambos L298N, el servo y sus fuentes.
> Los motores y el servo se alimentan de **fuente externa**, nunca desde el ESP32.

---

## 3. Firmware — flujo de control

Archivo: `paltacheck_flujo_completo.ino`. Máquina de estados que corre **mientras
haya lote activo** (lo consulta cada 5 s al backend).

```
SIN LOTE ──(GET /api/lote/activo = true)──► PROCESANDO LOTE
   │                                              │
   └── detenerTodo(), espera                      ▼
                                    1) Cinta avanza hasta que IR detecta fruta
                                       → para → 3 s para pasar a rodillos
                                              │
                                              ▼
                                    2) Por cada vuelta (×3):
                                       - rodillos giran ~3 s, paran
                                       - espera 5 s
                                       - lee TCS (RGB/lux) + DHT (T/HR)
                                       - captura foto
                                       - POST /api/palta  → palta_id
                                       - POST /api/captura/foto → VEREDICTO del modelo
                                       - espera 5 s entre vueltas
                                              │
                                              ▼
                                    3) Decisión (veredicto de la última foto):
                                       • antracnosis (enferma):
                                            servo ABRE 90° (10 s) → expulsa 5 s
                                            a máxima potencia → servo CIERRA
                                       • sana (healthy):
                                            expulsa 5 s sin servo (la deja pasar)
                                              │
                                              ▼
                                    4) Espera a que el IR se libere → siguiente fruta
```

**Parámetros clave (constantes del .ino):**

| Constante | Valor | Significado |
|---|---|---|
| `cantidadVueltas` | 3 | fotos/votos por palta |
| `tiempoGiroRodillos` | 3000 ms | giro de rodillos por vuelta |
| `tiempoEsperaFoto` | 5000 ms | estabilización antes de la foto |
| `tiempoEntreVueltas` | 5000 ms | pausa entre vueltas |
| `tiempoServoArriba` | 10000 ms | compuerta levantada en rechazo |
| `tiempoExpulsion` | 5000 ms | expulsión a máxima potencia |
| `velocidadCinta` | 100 | duty PWM de la cinta |
| `potenciaBotar` | 255 | duty PWM al expulsar |
| `POLL_LOTE_MS` | 5000 ms | frecuencia de consulta del lote |
| `IR_DEBOUNCE_MS` | 60 ms | antirrebote del IR |

- **`dormirVigilando()`** reemplaza a los `delay` largos: mientras "duerme", sigue
  consultando el lote; si lo cierras desde el dashboard, **aborta y frena todo**.
- **WiFi:** se reconecta solo. Config actual en el sketch → SSID `Piero`,
  backend `10.116.108.254:8000` (ajustar a tu red/IP de la laptop).

---

## 4. Backend (FastAPI) — el "programa"

Carpeta `backend/`. API REST que habla con el ESP32 y con el frontend, persiste en
TimescaleDB y ejecuta la inferencia del modelo.

### 4.1 Endpoints
| Método · Ruta | Para qué |
|---|---|
| `GET /health` | ping de salud |
| `GET /api/lote/activo` | señal de control que el ESP32 pollea (lote abierto = `fin IS NULL`) |
| `POST /api/lote` | abrir lote (desde el frontend) |
| `POST /api/lote/{id}/cerrar` | cerrar lote |
| `GET /api/lotes` | historial de lotes con agregados |
| `GET /api/lote/{id}/kpis` | KPIs del lote (vista `vista_kpis_lote`) |
| `GET /api/lote/{id}/paltas` | paltas del lote + sensores + `foto_url` |
| `POST /api/palta` | crea palta + `sensor_data` (clasificación la pone el modelo) → `palta_id` |
| `POST /api/captura/foto?palta_id&lote_id` | recibe el JPEG, **clasifica con el modelo**, vota y devuelve `prediccion` |
| `GET /api/palta/{id}/foto` | sirve la foto de la palta (la que ve el dashboard) |
| `GET /api/palta/{id}/fotos` | lista las 3 fotos de la palta para comparar |

### 4.2 Base de datos (SQLAlchemy + TimescaleDB)
- **`lote`** — id, codigo (único), inicio, fin, total_paltas, observacion.
- **`palta`** — id, lote_id, clasificacion (`sana`|`antracnosis`), confianza, votos_sana, votos_antracnosis, foto_ruta, timestamp.
- **`sensor_data`** — id, palta_id, r/g/b, lux, temp, humedad, ir_detectado, timestamp.
- Esquema inicial e índices/vista en [`db/init.sql`](../db/init.sql); KPIs vía `vista_kpis_lote`.
- Las **fotos JPG se guardan en disco** (`CAPTURAS_DIR=/app/capturas`), en la BD solo va la ruta.

---

## 5. La IA (modelo de enfermedades)

### 5.1 Inferencia — [`backend/inference.py`](../backend/inference.py)
- **Modelo:** MobileNetV2 (Keras 3), archivo `models/modelo_2_mobilenetv2_enfermedades.keras`.
- **Clases del modelo:** `["scab", "healthy", "anthracnose"]`.
- **Mapeo a la BD:** `healthy → sana`; `scab` y `anthracnose → antracnosis` (rechazo).
- **Preprocesamiento dentro del modelo** (`preprocess_input` escala a [−1, 1]): la
  inferencia solo redimensiona a **224×224** y pasa los píxeles **crudos 0–255**.
- Se carga **una sola vez** (perezoso, con lock); si falla, lo recuerda y no reintenta.
- Devuelve `{clasificacion, enfermedad, confianza, probabilidades}`.

### 5.2 Votación 3‑de‑3 (en `POST /api/captura/foto`)
Cada una de las 3 fotos de la palta **vota**. Se acumulan `votos_sana` /
`votos_antracnosis`, gana la mayoría y la **confianza = votos_ganadores / total**.
El ESP32 lee `clasificacion` de la respuesta y con eso decide el servo.

### 5.3 Entrenamiento — [`scripts/entrenar_modelo.py`](../scripts/entrenar_modelo.py)
Reentrena/afina con **tus** fotos del ESP32 (corrige el *domain shift* de cámara/luz/fondo).
- **Estructura:** `dataset/{healthy,anthracnose,scab}/` (+ `dataset_test/` opcional para medir sin fuga de datos).
- **Transfer learning** MobileNetV2: Fase 1 entrena la cabeza (base congelada),
  Fase 2 fine‑tuning de las últimas ~30 capas con LR muy bajo (1e‑5).
- **Aumento de datos** (flip, rotación, zoom, brillo, contraste) + **pesos por clase** por desbalance.
- Salida: `models/modelo_paltas.keras` e imprime el **orden de clases** → hay que copiarlo a `CLASS_NAMES` en `inference.py`.
- Guía paso a paso en [`docs/03_entrenar_modelo.md`](03_entrenar_modelo.md).

> Modelos presentes en `models/`: `modelo_2_mobilenetv2_enfermedades.keras` (el que usa el backend) y `best_model.keras`.

---

## 6. Frontend (Vue 3 + Pinia)

- **`LoteActivo.vue`** — dashboard del lote: abrir/cerrar lote, KPIs (procesadas,
  tasa de rechazo, velocidad ppm, confianza), **cuadrito "Captura de cámara"**,
  panel de sensores de la última palta y tabla de paltas.
- **Store `palta.js`** — pollea cada **3 s** (`fetchKpis` + `fetchPaltas`) cuando hay
  lote activo; la foto aparece sola al detectar `foto_url` en la última palta.
- API base configurable por `VITE_API_URL`.

---

## 7. Despliegue (Docker Compose)

`docker compose up -d --build` levanta 4 servicios:

| Servicio | Imagen / build | Puerto | Notas |
|---|---|---|---|
| **timescaledb** | timescale/timescaledb pg16 | 5433→5432 | volumen persistente + `db/init.sql` |
| **backend** | `./backend` | 8000 | monta `./models` (solo lectura) y `./backend/capturas` |
| **frontend** | `./frontend` | 5173 | `VITE_API_URL` |
| **pgadmin** | dpage/pgadmin4 | 5050 | admin de la BD |

Variables clave del backend: `DATABASE_URL`, `CAPTURAS_DIR=/app/capturas`,
`MODEL_PATH=/app/models/modelo_2_mobilenetv2_enfermedades.keras`.
Dependencias Python en [`backend/requirements.txt`](../backend/requirements.txt)
(FastAPI, SQLAlchemy, **tensorflow‑cpu**, keras ≥ 3.14, pillow, numpy).

> Tras reentrenar o cambiar el modelo: `docker compose up -d --build backend`.

---

## 8. Contrato ESP32 ↔ Backend (resumen rápido)

```
1) GET  /api/lote/activo
        → {"activo":true,"id":N,"codigo":"...","inicio":"..."}

2) POST /api/palta            (JSON, clasificacion=null → la decide el modelo)
        {"lote_id":N,"clasificacion":null,"confianza":null,
         "r":..,"g":..,"b":..,"lux":..,"temp":..,"humedad":..,
         "ir_detectado":true,"lecturas":3}
        → {"ok":true,"palta_id":M}

3) POST /api/captura/foto?palta_id=M&lote_id=N    (Content-Type: image/jpeg, JPEG crudo)
        → {"ok":true,"prediccion":{"clasificacion":"sana|antracnosis",
            "enfermedad":"healthy|scab|anthracnose","confianza":0.xx, ...}}
        El ESP32 lee "clasificacion" y decide el servo.
```

---

## 9. Estructura del repositorio (lo relevante)

```
backend/      main.py · inference.py · models.py · database.py · requirements.txt · Dockerfile
db/           init.sql               (esquema + vista_kpis_lote)
models/       *.keras                (modelos de IA, montados solo-lectura)
scripts/      entrenar_modelo.py     (reentrenamiento MobileNetV2)
frontend/     src/views/LoteActivo.vue · src/stores/palta.js
firmware/     paltacheck_flujo_completo/  ← FIRMWARE FINAL (IA + servo)
              paltacheck_pintest/         ← sketch de prueba (motores + 1 foto)
              + varios sketches de prueba por subsistema
docs/         01_firmware_esp32.md · 02_api_esp32.md · 03_entrenar_modelo.md
              mapa_pines_esp32s3.svg · conexiones_esp32s3.svg
```

---

### Notas / pendientes
- Hay **varios sketches de prueba** en `firmware/` (cinta, rodillos, IR, servo, full
  sensores, pintest…) usados para validar cada subsistema por separado.
- El **servo en GPIO 0** funciona, pero es pin de BOOT: evita que esté forzado a LOW
  durante el arranque/flasheo.
- Ajusta SSID/clave/IP del backend en el `.ino` según tu red antes de flashear.
