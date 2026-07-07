# PaltaCheck — Contexto completo del repositorio

Fecha: 2026-07-07 · Repo: `PieroCM/IOT_PROYECT` · Rama: `feat/esp32-captura-dashboard-lote`

Sistema IoT que clasifica paltas (aguacates) en una faja transportadora: un **ESP32-S3**
mueve la mecánica y saca fotos; un **backend** con **IA** decide si es palta y si está
enferma; un **dashboard** lo muestra al operario; una **base de datos** de series de
tiempo guarda todo.

---

## 1. Arquitectura general

```
┌───────────┐   fotos + sensores    ┌──────────────────────────┐   SQL   ┌───────────────┐
│  ESP32-S3 │ ────────────────────► │  BACKEND  (FastAPI :8000) │ ──────► │ TimescaleDB   │
│ (firmware)│ ◄──────────────────── │  gate Keras + ONNX enferm.│ ◄────── │ (:5433)       │
└───────────┘   veredicto           └──────────────────────────┘         └───────────────┘
      ▲                                        ▲
      │ acciona cinta/rodillos/servo           │ REST (polling 3 s)
                                         ┌──────────────┐
                                         │ FRONTEND Vue │  (:5173)  ← operario
                                         └──────────────┘
```

Todo se levanta con **Docker Compose** (4 servicios: `timescaledb`, `backend`, `frontend`, `pgadmin`).

---

## 2. Backend (`backend/`) — FastAPI + IA

| Archivo | Rol |
|---|---|
| `main.py` | Endpoints REST, orquestación, voto de las fotos, persistencia. |
| `inference.py` | Cadena de modelos: **gate Keras** + **enfermedad ONNX** (onnxruntime). |
| `models.py` | ORM SQLAlchemy: `Lote`, `Palta`, `SensorData`. |
| `database.py` | Conexión + `init_db()` con migraciones idempotentes (`ALTER TABLE … IF NOT EXISTS`). |
| `requirements.txt` | fastapi, uvicorn, sqlalchemy, psycopg2, **tensorflow-cpu + keras** (gate), **onnxruntime** (enfermedad), pillow, numpy. |
| `Dockerfile` | `python:3.12-slim`, uvicorn con `--reload`. |

### Endpoints principales
| Método · Ruta | Para qué |
|---|---|
| `GET /health` | Ping. |
| `GET /api/lote/activo` | El ESP32 pregunta si hay lote abierto (polling 5 s). |
| `POST /api/lote` | Abre lote (cierra remanentes). |
| `POST /api/lote/{id}/cerrar` | Cierra lote (`fin=NOW()`). |
| `GET /api/lote/{id}/kpis` · `/paltas` · `GET /api/lotes` | Datos para el dashboard/historial. |
| `POST /api/palta` | El ESP32 crea la palta (1ª foto) → devuelve `palta_id`. |
| `POST /api/captura/foto?palta_id&lote_id` | Sube el **JPEG**; corre modelos, vota, devuelve `clasificacion` y `clasificacion_agregada`. |
| `POST /api/palta/{id}/sensores` | El ESP32 manda RGB/lux/temp/humedad **solo si es palta**. |
| `GET /api/palta/{id}/fotos` · `GET /api/foto` · `GET /api/palta/{id}/foto` | Sirven las fotos guardadas en disco. |
| `POST /api/modelo/probar` | Probar el modelo con una imagen suelta (sin BD). |

### Cadena de inferencia (`inference.py`)
1. **GATE** `best_model.keras` (palta/no_palta, umbral 0.60). Si `no_palta` → devuelve `no_es_palta`.
2. **ENFERMEDAD** `modelo_antracnosis.onnx` (ONNX, MobileNet-V2, 3 clases). Preproceso ImageNet + softmax → `sana/antracnosis/scab`.
3. Devuelve `es_palta`, `clasificacion`, `confianza`, `confianza_gate`, `probabilidades_enfermedad`.
- **Voto** (en `main.py`): cada una de las 3 fotos vota; `palta.clasificacion` = mayoría (desempate: antracnosis > scab > sana > no_es_palta). `clasificacion_agregada` = ese voto (lo lee el firmware).
- Variables de entorno (docker-compose): `DISEASE_ENABLED=true`, `BINARY_MODEL_PATH`, `DISEASE_MODEL_PATH`, `DISEASE_CLASSES_PATH`.

---

## 3. Frontend (`frontend/`) — Vue 3 + Vite

| Archivo | Rol |
|---|---|
| `src/App.vue` | Layout + **sidebar oculto que se abre al acercar el mouse** (hamburguesa en móvil). |
| `src/views/LoteActivo.vue` | **Dashboard del operario** (vista principal). |
| `src/views/Historial.vue` | Historial de lotes cerrados (datos reales). |
| `src/stores/palta.js` | **Pinia store**: estado del lote, polling cada 3 s (`kpis`+`paltas`), health cada 15 s. |
| `src/router/index.js` | Rutas: `/` (Lote Activo) y `/historial`. |
| `src/style.css` | Tokens de color (verde/rojo/ámbar/gris). |

### Dashboard (`LoteActivo.vue`) muestra por fruta
- **3 tarjetas**: N° Paltas · Sanas · Enfermas.
- **Sensores** de la última fruta: Temperatura · Humedad · RGB.
- **Pie chart "distribución del lote"** (donut): % sana/antracnosis/scab/no-palta con nombre+cantidad en cada porción.
- **Última fruta**: veredicto **PASA / ENFERMA·(Antracnosis|Scab) / EXPULSADA**, foto grande **+ las 3 miniaturas** del ciclo, barra **"Es palta" %**, y **barras de probabilidad por clase** de esa palta.
- **Colores**: verde=sana, rojo=antracnosis, ámbar=scab, gris=no_palta.
- Consume solo datos **reales** del backend (sin mocks). Las fotos se recargan por palta y se **limpian al cambiar de fruta** (no arrastra fotos anteriores).

---

## 4. Base de datos (`db/init.sql`) — TimescaleDB / PostgreSQL

| Tabla | Columnas clave |
|---|---|
| **lote** | `id`, `codigo` (único), `inicio`, `fin` (NULL = activo), `total_paltas`, `observacion`. |
| **palta** | `id`, `lote_id`, `clasificacion` *(CHECK: sana / antracnosis / scab / no_es_palta)*, `confianza`, **`confianza_gate`**, **`probabilidades` (JSONB)**, `votos_sana/antracnosis/scab/no_palta`, `foto_ruta`, `timestamp`. |
| **sensor_data** *(hypertable)* | `id`, `palta_id`, `timestamp`, `r`, `g`, `b`, `lux`, `temp`, `humedad`, `ir_detectado`. Comprimida por columnas (segment by `palta_id`). |

- **Fotos**: NO se guardan en la BD (solo la **ruta**); el JPEG va a disco `backend/capturas/lote_{id}/palta_{id}_*.jpg`.
- **Vista** `vista_kpis_lote` y migraciones idempotentes en `database.py` (añaden `votos_scab`, `confianza_gate`, `probabilidades` y el CHECK con `scab` sin perder datos).

---

## 5. Modelos (`models/`)

| Archivo | Uso |
|---|---|
| `best_model.keras` (+ `_classes.json` `["no_palta","palta"]`) | **GATE** en uso. |
| `modelo_antracnosis.onnx` (+ `_classes.json` `["antracnosis","sana","scab"]`) | **ENFERMEDAD** en uso (MobileNet-V2, 8.5 MB). |
| `modelo_enfermedad*.keras`, `modelo_2_mobilenetv2_enfermedades.keras` | Modelos de enfermedad **anteriores** (2 clases) — reemplazados. |

> Detalle de métricas/fundamentos: ver **`02_modelos.md`**.

---

## 6. Firmware (`firmware/`) — ESP32-S3 N16R8

- **En uso: `paltacheck_flujo_completo/paltacheck_flujo_completo.ino`** (idéntico en lógica a `paltacheck_flujo_binario`). El resto son **pruebas** (`*_test`, `pintest`, `rodillos_test`, `servo_test`…).
- Sensores: **TCS34725** (RGB/lux, I2C), **DHT22** (temp/humedad), **IR FC-51**. Actuadores: **cinta** y **2 rodillos** (L298N) + **servo** compuerta (GPIO 3).
- Config editable arriba del sketch: `SSID`, `PASSWORD`, `BACKEND_HOST` (IPv4 de la laptop).
- Flujo: cinta→IR→3 vueltas (gira/espera/foto/espera)→veredicto agregado→servo. Detalle: **`01_flujo_completo.md`**.

---

## 7. Pipeline de entrenamiento (`ml_antracnosis/`)

| Archivo | Qué hace |
|---|---|
| `config.py` | Configuración central (3 clases, degradación, arquitectura). |
| `preprocess.py` | Degrada el dataset de estudio para parecerse a la cámara real. |
| `train.py` | Transfer learning (mobilenet_v2 / resnet50 / efficientnet_b0). |
| `export_todos.py` | Exporta los `.pt` → `.onnx` y verifica con onnxruntime. |
| `validar_capturas.py` | Corre los modelos sobre las capturas reales (análisis distribucional). |
| `infer.py` | Inferencia sobre una imagen (reutilizada por el export/validación). |
| `Data/` (ignorado en git) | Dataset de estudio (`archive/`) + capturas reales (`capturas/`). |

---

## 8. Infraestructura (`docker-compose.yml`)

| Servicio | Puerto | Qué es |
|---|---|---|
| `timescaledb` | 5433 | BD (PostgreSQL + TimescaleDB). |
| `backend` | 8000 | FastAPI + modelos (monta `./models` solo-lectura y `./backend/capturas`). |
| `frontend` | 5173 | Vite dev server. |
| `pgadmin` | 5050 | Administración de la BD (opcional). |

**Levantar todo:** `docker compose up -d --build` → dashboard en **http://localhost:5173**.

---

## 9. Estado y control de versiones
- `.gitignore` ignora: `data/`, `*.zip`, `.venv/`, `ml_antracnosis/outputs/`, `backend/capturas/`, `*.jpg`, `node_modules/`.
- **Sí** se versionan los modelos servidos (`models/*.keras`, `models/*.onnx`, `*_classes.json`).
- Documentación: `REPORTE_PALTACHECK.md` (raíz), `docs/` (equipo), `docs_reporte/` (figuras), y **`docs_informe/`** (estos 3 informes).

---

## 10. Mapa de carpetas (resumen)
```
IOT_PROYECT/
├── backend/            FastAPI + IA (main, inference, models, database)
├── frontend/           Vue 3 + Vite (App, views, store)
├── db/init.sql         Esquema TimescaleDB
├── models/             Gate Keras + Enfermedad ONNX (+ legacy)
├── firmware/           Sketches ESP32 (en uso: paltacheck_flujo_completo)
├── ml_antracnosis/     Pipeline de entrenamiento (train/preprocess/export/validar)
├── docs/  docs_reporte/  docs_informe/   Documentación y figuras
├── docker-compose.yml  4 servicios
└── REPORTE_PALTACHECK.md
```
