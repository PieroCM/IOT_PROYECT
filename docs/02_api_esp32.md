# 02 · API del ESP32 ↔ Backend (PaltaCheck)

Contrato de las APIs que usa el **ESP32** para mandar la data al backend y que el
**dashboard** usa para leerla. El backend es FastAPI (`backend/main.py`) y **ya
está implementado** — este documento describe el contrato vigente, no propone
cambios.

> El ESP32 **no necesita que nadie lo arranque manualmente**: pollea el backend
> y, en cuanto detecta un lote activo, empieza a enviar datos automáticamente.
> Ver [Flujo end-to-end](#flujo-end-to-end) y [Auto-detección de lote](#auto-detección-de-lote-activo).

---

## Configuración

| Parámetro | Valor por defecto | Dónde se define |
|-----------|-------------------|-----------------|
| Base URL | `http://<BACKEND_HOST>:8000` | — |
| `BACKEND_HOST` | `10.150.244.60` (IP de la laptop) | `firmware/paltacheck_main/paltacheck_main.ino` |
| `BACKEND_PORT` | `8000` | firmware + `docker-compose.yml` |
| Intervalo de polling | `5000 ms` (`POLL_LOTE_MS`) | firmware |
| Carpeta de fotos | `CAPTURAS_DIR` (def. `capturas/`, en Docker `/app/capturas`) | `backend/main.py` |

Todos los `POST` con cuerpo JSON usan `Content-Type: application/json`, **salvo**
`POST /api/captura/foto`, que envía el **JPEG crudo** con `Content-Type: image/jpeg`.

---

## Flujo end-to-end

```
ESP32                                   Backend                         Dashboard
  │                                        │                                │
  │ 1. GET /api/lote/activo  (cada 5 s) ──▶│                                │
  │◀── {activo:true, id, codigo, inicio}   │                                │
  │                                        │                                │
  │  ── lote activo: arranca cinta ──      │                                │
  │                                        │                                │
  │  (por cada palta, ciclo de 3 vueltas)  │                                │
  │ 2. POST /api/palta  (promedio RGB/   ─▶│  crea palta + sensor_data      │
  │     lux/temp/humedad)                  │                                │
  │◀── {ok:true, palta_id}                 │                                │
  │                                        │                                │
  │ 3. POST /api/captura/foto?palta_id   ─▶│  guarda JPEG en disco,         │
  │     &lote_id   (bytes JPEG)            │  actualiza palta.foto_ruta     │
  │◀── {ok:true, foto_ruta, bytes}         │                                │
  │                                        │                                │
  │                                        │◀── 4. GET /api/lote/{id}/paltas (cada 3 s)
  │                                        │     GET /api/lote/{id}/kpis
  │                                        │     GET /api/palta/{id}/foto ──▶│ muestra
  │                                        │                                │ sensores + foto
```

1. El ESP32 **pollea** `GET /api/lote/activo`.
2. Si hay lote activo, por **cada palta** envía el promedio de sensores con `POST /api/palta` y recibe el `palta_id`.
3. Sube la foto de esa palta con `POST /api/captura/foto` usando ese `palta_id`.
4. El dashboard lee periódicamente las paltas, los KPIs y las fotos.

---

## Endpoints que **envía** el ESP32

### 1) `GET /api/lote/activo` — detectar lote activo

Señal de control. El ESP32 la consulta cada 5 s para saber si debe trabajar.

**Request:** sin parámetros ni cuerpo.

**Response 200 — hay lote abierto** (`lote.fin IS NULL`):
```json
{ "activo": true, "id": 12, "codigo": "LOTE-2024-008", "inicio": "2026-06-19T10:30:00+00:00" }
```

**Response 200 — no hay lote:**
```json
{ "activo": false }
```

**Comportamiento del ESP32:** ver [Auto-detección de lote](#auto-detección-de-lote-activo).

---

### 2) `POST /api/palta` — enviar el ciclo de sensores (promediado)

El ESP32 acumula las 3 vueltas, promedia y envía **un** payload por palta.

**Request** (`Content-Type: application/json`):
```json
{
  "lote_id": 12,
  "clasificacion": null,
  "confianza": null,
  "votos_sana": 0,
  "votos_antracnosis": 0,
  "r": 112,
  "g": 141,
  "b": 85,
  "lux": 415.0,
  "temp": 22.4,
  "humedad": 64.8,
  "ir_detectado": true,
  "lecturas": 3
}
```

**Response 200:**
```json
{ "ok": true, "palta_id": 456 }
```
Sin base de datos (modo prueba): `{ "ok": true, "palta_id": null, "sin_bd": true }`.

**Campos del cuerpo:**

| Campo | Tipo | Req. | Default | Notas |
|-------|------|------|---------|-------|
| `lote_id` | int | **Sí** | — | El `id` recibido en `/api/lote/activo`. |
| `clasificacion` | string \| null | No | `null` | `'sana'` \| `'antracnosis'` \| `'no_es_palta'`. Hoy llega `null`. |
| `confianza` | float \| null | No | `null` | 0–1. Hoy llega `null`. |
| `votos_sana` | int | No | `0` | Votos del modelo (TFLite, pendiente). |
| `votos_antracnosis` | int | No | `0` | Votos del modelo (TFLite, pendiente). |
| `r`, `g`, `b` | int \| null | No | `null` | TCS34725 normalizado a 0–255. |
| `lux` | float \| null | No | `null` | Luminosidad calculada. |
| `temp` | float \| null | No | `null` | DHT22 (°C). `null` si la lectura fue inválida. |
| `humedad` | float \| null | No | `null` | DHT22 (%). `null` si la lectura fue inválida. |
| `ir_detectado` | bool | No | `true` | Sensor FC-51. |
| `lecturas` | int | No | `3` | Vueltas promediadas. |

> **Nota (fase actual, sin TFLite):** si `clasificacion` llega `null`, el backend
> asume `"sana"`; si `confianza` llega `null`, asume `0.5`. Así el dashboard
> tiene datos consistentes hasta integrar los modelos.

---

### 3) `POST /api/captura/foto` — subir la foto JPEG

El ESP32 envía los **bytes del JPEG** en el cuerpo; los IDs van como query params
(fácil de armar en Arduino, sin JSON).

**Request:**
- URL: `POST /api/captura/foto?palta_id=456&lote_id=12`
- Header: `Content-Type: image/jpeg`
- Body: bytes crudos del JPEG (framebuffer de la cámara).

| Query param | Tipo | Notas |
|-------------|------|-------|
| `palta_id` | int (opcional) | El `palta_id` devuelto por `POST /api/palta`. |
| `lote_id` | int (opcional) | Organiza la foto por carpeta de lote. |

**Response 200:**
```json
{ "ok": true, "palta_id": 456, "foto_ruta": "capturas/lote_12/palta_456_20260619_103045.jpg", "bytes": 45821 }
```

**Errores:**
- `400` — imagen vacía o `< 100 bytes` (`{"detail": "Imagen vacía o demasiado pequeña"}`).

**Almacenamiento:** la imagen se guarda en disco en
`CAPTURAS_DIR/lote_{lote_id}/palta_{palta_id}_{YYYYMMDD_HHMMSS}.jpg` y solo la
**ruta** se persiste en `palta.foto_ruta` (no el blob en la BD).

---

## Endpoints que **lee** el dashboard

### 4) `GET /api/lote/{lote_id}/paltas`

Lista las paltas del lote con su última lectura de sensores y la URL de la foto.

**Response 200:**
```json
[
  {
    "id": 456,
    "lote_id": 12,
    "clasificacion": "sana",
    "confianza": 0.5,
    "votos_sana": 0,
    "votos_antracnosis": 0,
    "foto_ruta": "capturas/lote_12/palta_456_20260619_103045.jpg",
    "foto_url": "/api/palta/456/foto",
    "timestamp": "2026-06-19T10:30:45+00:00",
    "sensor": { "r": 112, "g": 141, "b": 85, "lux": 415.0, "temp": 22.4, "humedad": 64.8 }
  }
]
```
`foto_url` es `null` mientras la palta no tenga foto. El dashboard arma la URL
absoluta como `VITE_API_URL + foto_url`.

### 5) `GET /api/palta/{palta_id}/foto`

Devuelve la imagen JPG (`FileResponse`, `image/jpeg`). `404` si no existe la
ruta o el archivo. Es la fuente del card **"Captura de cámara"**.

### 6) `GET /api/lote/{lote_id}/kpis`

KPIs agregados desde la vista `vista_kpis_lote`.

**Response 200:**
```json
{
  "lote_id": 12, "codigo": "LOTE-2024-008",
  "inicio": "2026-06-19T10:30:00+00:00", "fin": null,
  "total": 23, "sanas": 20, "rechazadas": 3,
  "tasa_rechazo": 13.04, "confianza_promedio": 0.5,
  "temp_promedio": 22.4, "humedad_promedio": 64.8
}
```
> El dashboard actual usa de aquí principalmente `total` (card "Paltas
> Procesadas"); el resto queda disponible para futuras vistas/Historial.

---

## Endpoints de gestión de lote (los usa el dashboard, no el ESP32)

| Método | Ruta | Cuerpo / params | Respuesta |
|--------|------|-----------------|-----------|
| `POST` | `/api/lote` | `{"codigo": "LOTE-..."}` | `{id, codigo, inicio}` — abre lote (lo vuelve activo). |
| `POST` | `/api/lote/{id}/cerrar` | — | `{ok, id, codigo}` — cierra el lote (`fin=NOW()`). |
| `GET`  | `/api/lotes` | — | lista de lotes con KPIs agregados (Historial). |
| `GET`  | `/health` | — | `{status:"ok"}`. |

---

## Auto-detección de lote activo

El ESP32 (`consultarLoteActivo()` en el firmware) consulta `GET /api/lote/activo`
cada 5 s y reacciona ante los cambios de estado:

- **`activo:false → true`** (se abrió un lote desde el dashboard): arranca la
  cinta y pasa a `ESPERANDO_PALTA`. A partir de ahí, por cada palta envía
  `POST /api/palta` + `POST /api/captura/foto` **sin intervención manual**.
- **`activo:true → false`** (se cerró el lote): detiene cinta/actuadores y vuelve
  a `SIN_LOTE`.
- **Sin respuesta del backend** (caída de red): **mantiene el último estado** y
  no cierra el lote; reanuda al recuperar la conexión.

En consecuencia, el operador solo **abre/cierra el lote en el dashboard** y el
ESP32 se sincroniza solo.

---

## Pruebas

- `scripts/test_backend.py` — recorre el flujo completo (abrir lote → `/api/palta`
  → `/api/captura/foto` → leer paltas/KPIs/foto → cerrar) contra SQLite, sin Docker.
- `scripts/test_esp32.py` — simula al ESP32 enviando paltas y fotos al backend real.

### Ejemplos `curl`

```bash
# 1) ¿Hay lote activo?
curl http://localhost:8000/api/lote/activo

# 2) Enviar una palta (recibe palta_id)
curl -X POST http://localhost:8000/api/palta \
  -H "Content-Type: application/json" \
  -d '{"lote_id":12,"r":112,"g":141,"b":85,"lux":415.0,"temp":22.4,"humedad":64.8,"lecturas":3}'

# 3) Subir la foto de esa palta (JPEG crudo)
curl -X POST "http://localhost:8000/api/captura/foto?palta_id=456&lote_id=12" \
  -H "Content-Type: image/jpeg" \
  --data-binary @foto.jpg

# 4) Ver la foto guardada
curl http://localhost:8000/api/palta/456/foto --output palta_456.jpg
```
