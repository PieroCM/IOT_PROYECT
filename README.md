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
- **`palta`** — cada palta individual con su clasificación (`sana` / `antracnosis`) y confianza del modelo
- **`sensor_data`** — lecturas del ESP32 por palta: R, G, B, lux, temperatura, humedad, IR (hypertable TimescaleDB)
- **`vista_kpis_lote`** — vista agregada con métricas por lote: totales, tasa de rechazo, promedios

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

| Método | Ruta                          | Descripción                              |
|--------|-------------------------------|------------------------------------------|
| GET    | `/health`                     | Estado del servicio                      |
| GET    | `/api/lotes`                  | Lista todos los lotes con KPIs           |
| POST   | `/api/lote`                   | Crea un nuevo lote `{ "codigo": "..." }` |
| POST   | `/api/lote/{id}/cerrar`       | Cierra un lote activo                    |
| GET    | `/api/lote/{id}/kpis`         | KPIs de un lote específico               |
| GET    | `/api/lote/{id}/paltas`       | Lista paltas de un lote con datos sensor |
| POST   | `/api/palta`                  | Recibe lectura del ESP32-CAM             |

---

## Script de prueba (ESP32 simulado)

Para probar la recepción de fotos sin hardware real:

```bash
cd scripts
pip install flask colorama
python test_esp32.py
```

Levanta un servidor Flask en el puerto `8000` que recibe imágenes JPEG crudas via `POST /foto` y las guarda en `fotos_test/`.

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
