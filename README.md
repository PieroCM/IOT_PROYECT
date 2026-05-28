# PaltaCheck — Sistema IoT de Clasificación de Paltas

Sistema de clasificación automática de paltas (aguacates) mediante sensores IoT (ESP32-CAM) con detección de antracnosis usando datos de color RGB, luz, temperatura y humedad.

---

## Arquitectura

```
ESP32-CAM / Sensores
        │
        ▼
┌───────────────┐        ┌─────────────────────────┐
│   Backend     │◄──────►│  TimescaleDB (Postgres)  │
│  FastAPI      │        │  + extensión TimescaleDB │
│  Puerto 8000  │        │  Puerto 5433 (host)      │
└───────────────┘        └─────────────────────────┘
        ▲
        │
┌───────────────┐        ┌─────────────────────────┐
│   Frontend    │        │       pgAdmin 4          │
│  Vue 3 + Vite │        │  Admin visual de la BD   │
│  Puerto 5173  │        │  Puerto 5050             │
└───────────────┘        └─────────────────────────┘
```

### Servicios

| Servicio      | Tecnología                     | Descripción                                        |
|---------------|--------------------------------|----------------------------------------------------|
| `backend`     | Python 3.12 · FastAPI · Uvicorn | API REST que recibe datos del ESP32 y sirve el dashboard |
| `frontend`    | Vue 3 · Vite · Chart.js · Pinia | Dashboard web con vistas de lote activo, historial y configuración |
| `timescaledb` | TimescaleDB (PostgreSQL 16)    | Base de datos de series temporales para lecturas del sensor |
| `pgadmin`     | pgAdmin 4                      | Interfaz gráfica para administrar la base de datos |

### Base de datos

- **`lote`** — agrupa un conjunto de paltas procesadas en una sesión
- **`palta`** — cada palta individual con su clasificación (`sana` / `antracnosis` / `no_es_palta`), confianza, votos y `foto_ruta` (referencia a la imagen en disco)
- **`sensor_data`** — lecturas del ESP32 por palta: R, G, B, lux, temperatura, humedad, IR (**hypertable TimescaleDB con compresión columnar** segmentada por `palta_id`)
- **`vista_kpis_lote`** — vista agregada con métricas por lote: totales, tasa de rechazo, promedios

> **Compresión:** `sensor_data` usa compresión nativa de TimescaleDB (hasta ~90-98% de reducción en datos numéricos de series temporales). La política comprime automáticamente los chunks con datos de más de 7 días; para una demo puedes comprimir un chunk a mano:
> ```sql
> SELECT compress_chunk(c) FROM show_chunks('sensor_data') c;
> SELECT * FROM hypertable_compression_stats('sensor_data');  -- ver ahorro
> ```
> Las **fotos JPG NO se guardan en la BD** (el JPEG ya viene comprimido y inflaría la hypertable): van a disco en `backend/capturas/lote_<id>/` y en la BD solo queda la ruta.

---

## Requisitos

- [Docker Desktop](https://www.docker.com/products/docker-desktop/)
- Puerto `5433`, `8000`, `5173` y `5050` disponibles en el host

---

## Levantar el stack

```bash
# Primera vez o tras cambios en el código
docker compose up --build -d

# Siguientes veces (sin cambios)
docker compose up -d
```

> **Nota:** Se llama *stack* (no cluster) porque corre en una sola máquina. Un cluster implicaría múltiples nodos (eso es Kubernetes).

### Verificar que todo está corriendo

```bash
docker ps
```

Deberías ver 4 contenedores con estado `Up` / `healthy`.

### Detener el stack

```bash
docker compose down
```

### Detener y eliminar los datos de la base de datos

```bash
docker compose down -v
```

---

## URLs de acceso

| Servicio       | URL                                          | Credenciales                          |
|----------------|----------------------------------------------|---------------------------------------|
| **Frontend**   | http://localhost:5173                        | —                                     |
| **API Backend**| http://localhost:8000                        | —                                     |
| **API Docs**   | http://localhost:8000/docs                   | Swagger UI generado automáticamente   |
| **pgAdmin**    | http://localhost:5050                        | `admin@paltacheck.com` / `admin123`   |

### Conectar pgAdmin a la base de datos

Al entrar a pgAdmin por primera vez, agregar un servidor con:

| Campo    | Valor         |
|----------|---------------|
| Host     | `timescaledb` |
| Port     | `5432`        |
| Database | `paltacheck_db` |
| Username | `paltacheck`  |
| Password | `paltacheck123` |

> Usar `timescaledb` como host (nombre del servicio en la red Docker interna), no `localhost`.

---

## Endpoints de la API

| Método | Ruta                          | Quién la usa | Descripción                              |
|--------|-------------------------------|--------------|------------------------------------------|
| GET    | `/health`                     | —            | Estado del servicio                      |
| GET    | `/api/lotes`                  | Frontend     | Lista todos los lotes con KPIs           |
| POST   | `/api/lote`                   | Frontend     | Abre un nuevo lote `{ "codigo": "..." }` |
| **GET**| **`/api/lote/activo`**        | **ESP32 (5s)** | **Señal de control: devuelve el lote abierto o `{activo:false}`. El ESP32 arranca con lote activo y para cuando se cierra** |
| POST   | `/api/lote/{id}/cerrar`       | Frontend     | Cierra un lote activo                    |
| GET    | `/api/lote/{id}/kpis`         | Frontend (3s)| KPIs de un lote específico               |
| GET    | `/api/lote/{id}/paltas`       | Frontend     | Lista paltas de un lote (incluye `foto_url`) |
| POST   | `/api/palta`                  | ESP32        | Recibe el resultado promediado de un ciclo: crea `palta` + `sensor_data` y devuelve `palta_id` |
| **POST**| **`/api/captura/foto`**      | **ESP32**    | **Recibe el JPEG crudo (body) + `?palta_id=&lote_id=`. Guarda la foto en disco y la ruta en BD** |
| **GET**| **`/api/palta/{id}/foto`**    | **Frontend** | **Sirve la imagen JPG de una palta**     |

---

## Flujo de control (frontend → backend → ESP32)

El lote se abre y se cierra **desde el dashboard**; el ESP32 obedece esa señal:

```
Frontend "Abrir Lote"  → POST /api/lote            → backend marca el lote activo
ESP32 (poll cada 5s)   → GET  /api/lote/activo      → arranca cinta/servos
                       → IR (FC-51) detecta palta   → ciclo de 3 vueltas:
                            motor 120° · cámara · TCS34725 · DHT22  (×3)
                       → promedia las 3 lecturas
                       → POST /api/palta             → recibe palta_id
                       → POST /api/captura/foto      → sube el JPEG con ese palta_id
Frontend "Cerrar Lote" → POST /api/lote/{id}/cerrar  → ESP32 lo detecta y DETIENE todo
```

> **Fase actual:** los sensores (DHT22, TCS34725, FC-51) y la cámara se leen de verdad; LEDs, motores, servo, buzzer, OLED y los modelos TFLite se **simulan por `Serial.println` con sus delays**, para ver toda la secuencia sin depender de hardware aún no integrado.

## Firmware ESP32

`firmware/paltacheck_main/paltacheck_main.ino` — sketch unificado (máquina de estados).

Antes de compilar, ajustar arriba del archivo: `SSID`, `PASSWORD` y `BACKEND_HOST` (la IP de tu laptop). Librerías necesarias en el Arduino IDE: **DHT sensor library** (Adafruit) y **Adafruit TCS34725**.

> ⚠️ **Conflicto de pines:** la cámara del ESP32-S3-CAM ocupa de forma fija los GPIO 15/16/17, que tu doc de sensores usaba para TCS-SCL / DHT22 / IR. En el firmware unificado los sensores se reasignaron a pines libres (DHT22→GPIO2, TCS-SCL→GPIO21, IR→GPIO1). **Verifica que esos GPIO estén libres en tu placa** y ajústalos si hace falta.

## Scripts de prueba

**Probar el backend completo (sin Docker ni Postgres):**

```bash
pip install fastapi "uvicorn[standard]" sqlalchemy pydantic python-dotenv httpx
python scripts/test_backend.py
```

Levanta la API en memoria con SQLite y ejercita todo el flujo (abrir lote → activo → palta → foto → kpis → cerrar). Debe imprimir `Resultado: N OK, 0 fallas`.

**Probar solo la recepción de fotos (servidor Flask suelto):**

```bash
cd scripts
pip install flask colorama
python test_esp32.py
```

Recibe imágenes JPEG crudas vía `POST /foto` y las guarda en `fotos_test/`.

---

## Variables de entorno

Copiar `.env.example` a `.env` y ajustar si es necesario:

```bash
cp .env.example .env
```

| Variable          | Default                                                         |
|-------------------|-----------------------------------------------------------------|
| `DB_USER`         | `paltacheck`                                                    |
| `DB_PASSWORD`     | `paltacheck123`                                                 |
| `DB_NAME`         | `paltacheck_db`                                                 |
| `DATABASE_URL`    | `postgresql://paltacheck:paltacheck123@timescaledb:5432/paltacheck_db` |
| `VITE_API_URL`    | `http://localhost:8000`                                         |
| `PGADMIN_EMAIL`   | `admin@paltacheck.com`                                          |
| `PGADMIN_PASSWORD`| `admin123`                                                      |
