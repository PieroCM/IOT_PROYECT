# PaltaCheck — Documentación completa del proyecto

Sistema IoT para clasificar paltas (sanas / con antracnosis) usando un ESP32-S3 con
cámara, sensores, cinta + rodillos, un servo de rechazo, un backend (FastAPI + IA) y
un dashboard (Vue). Este documento resume **todo** lo trabajado: hardware, pines,
alimentación, flujo, integración del modelo, bugs encontrados y sus soluciones.

---

## 1. Hardware

| Elemento | Detalle |
|---|---|
| Placa | **ESP32-S3-CAM** (módulo ESP32-S3-WROOM **N16R8**: 16 MB flash, 8 MB PSRAM) |
| Cámara | **OV3660** integrada (mapa de pines `ESP32S3_EYE`) |
| Sensor color | **TCS34725** (I²C) — R,G,B + lux |
| Sensor humedad/temp | **DHT22** |
| Sensor presencia | **IR FC-51** (LOW = detecta) |
| Motor cinta | 1 motor en **L298N #1** |
| Motores rodillos | 2 motores en **L298N #2** |
| Servo compuerta | **MG996R** (alto torque) |
| Fuentes | **12 V** (motores) + **ESP32 por USB** |

---

## 2. Mapa de pines (GPIO del ESP32-S3)

### Sensores
| Señal | GPIO |
|---|---|
| RGB TCS34725 · SDA (I²C) | **14** |
| RGB TCS34725 · SCL (I²C) | **21** |
| Humedad DHT22 · DATA | **2** |
| IR FC-51 · OUT | **1** |

### Cinta (L298N #1)
| Pin L298N | GPIO |
|---|---|
| ENA (velocidad, PWM) | **46** |
| IN1 (dirección) | **48** |
| IN2 | **a GND** (la cinta solo va adelante) |
| ENB / IN3 / IN4 | **no se usan** |

### Rodillos (L298N #2)
| Pin L298N | GPIO |
|---|---|
| ENA (Rodillo 1) | **42** |
| IN1 (Rodillo 1) | **41** |
| IN2 (Rodillo 1) | **40** |
| ENB (Rodillo 2) | **39** |
| IN3 (Rodillo 2) | **38** |
| IN4 (Rodillo 2) | **47** |

### Servo
| Señal | GPIO |
|---|---|
| Servo (cable amarillo/naranja) | **3** |

### Pines RESERVADOS (no usar)
- **Cámara:** GPIO 4–18 (fijos)
- **PSRAM:** GPIO 33–37
- **USB:** 19, 20 · **UART/Serial:** 43, 44
- **Strapping:** 0, 45, 46 (el **46** es de la cinta: **desconectarlo al subir código**)

---

## 3. Alimentación y GND (en palabras)

**Fuente de 12 V (motores):**
- Rojo (+12V) → al pin **+12V** de los **dos L298N**.
- Negro (−) → **GND común**.

**L298N #1 (cinta) y #2 (rodillos):**
- +12V → rojo de la fuente. GND → GND común.
- En el **L298N #1** deja **puesto el jumper de 5V** → así su pin **+5V** entrega 5 V.

**Servo MG996R:**
- V+ (rojo) → al **pin +5V del L298N #1** (no al 5V de la placa).
- GND (marrón) → GND común. Señal (amarillo) → **GPIO 3**.
- Recomendado: **capacitor 470–1000 µF** entre V+ y GND del servo.

**Sensores:**
- RGB TCS34725: VIN → **3V3** del ESP32 · GND → común.
- DHT22: VCC → **3V3** del ESP32 · GND → común.
- IR FC-51: VCC → **5V** del ESP32 · GND → común.

**ESP32:** se alimenta por **USB**. Nunca conectar 12 V al ESP32.

> **GND COMÚN (lo más importante):** unir en un solo punto el negativo de la
> fuente 12V, el GND del ESP32, el GND de los 2 L298N, el GND del servo y el GND
> de los 3 sensores. Sin GND común, nada funciona bien.

**Salidas a motores:** OUT1/OUT2 de cada L298N → al motor. Para **invertir el giro**
de un motor, intercambiar sus dos cables OUT.

---

## 4. Flujo del sistema

El **lote** es la señal de control: el dashboard lo abre/cierra, el ESP32 lo
consulta. Mientras hay lote activo:

1. **Cinta avanza** hasta que el **IR** detecta la fruta → la cinta **para**.
2. Espera unos segundos (la fruta pasa a los rodillos).
3. **Rodillos ×3 vueltas:** giran lento → paran → **toman foto** + leen sensores.
4. Las **3 fotos** van a **UNA sola palta** (registro único por fruta).
5. El **backend clasifica** cada foto con el modelo y **vota** (mayoría).
6. **Veredicto:**
   - **Antracnosis (mala):** el **servo abre la compuerta 90°**, espera, y los
     rodillos **expulsan**; luego el servo vuelve a 0°.
   - **Healthy (sana):** no toca el servo, la deja pasar.
7. Si se **cierra el lote**, frena todo y vuelve a esperar.

---

## 5. Backend + Modelo de IA

- Backend **FastAPI** (Docker) + **TimescaleDB** + dashboard **Vue**.
- Endpoints clave: `GET /api/lote/activo`, `POST /api/palta`, `POST /api/captura/foto`.
- El **modelo corre en el backend** (no en el ESP32): el ESP32 manda la foto y el
  backend devuelve el veredicto. Modelo: **MobileNetV2** (`modelo_2_mobilenetv2_enfermedades.keras`),
  3 clases (**scab, healthy, anthracnose**), entrada 224×224 (el preprocesamiento va
  dentro del modelo → se le pasan los píxeles crudos 0–255).
- Mapeo a la BD: `healthy → sana`; `scab` y `anthracnose → antracnosis`.
- Para activar: `docker compose up -d --build backend`. Modelos montados en
  `/app/models` vía `docker-compose.yml`.

### Reentrenar el modelo (recomendado)
El modelo marca casi todo como antracnosis por **domain shift** (fue entrenado con
imágenes distintas a las de tu cámara). Solución: **reentrenar con fotos de tu ESP32**.
- Necesitas **ambas clases** (sanas y enfermas), idealmente **5–10 paltas por clase**,
  ~30–100 fotos por clase.
- Separar por **palta entera** entre train y test (evitar fuga de datos).
- Script: `scripts/entrenar_modelo.py` (transfer learning + aumento de datos).
- Guía: `docs/03_entrenar_modelo.md`.

---

## 6. Bugs encontrados y soluciones

| Problema | Causa | Solución |
|---|---|---|
| `HTTP -1` al backend | IP de la laptop equivocada (cambia con la red) | Actualizar `BACKEND_HOST` con `ipconfig`; ideal usar hotspot de la laptop (IP fija) |
| Cámara muy "cerca"/borrosa | Lente fijo OV3660, distancia muy corta | Alejar la cámara ~10–15 cm + ajustar el lente M12 + más luz |
| Foto repetida (vieja) | Frame viejo en el buffer | `capturarFresca()`: descarta 2 frames y captura el actual |
| Dashboard: solo la 1ª foto grande | Caché del navegador (misma URL) | Imagen grande usa la URL única del archivo más reciente |
| Servo "loco" / oscila | **MG996R en el 5V de la placa** (tira hasta 2.5 A → brownout) | Servo a fuente aparte (o **+5V del L298N**) + GND común + cap |
| Cinta no para tras un ciclo | **Choque de canal LEDC**: los motores tomaban el canal 0 de la cámara | **Inicializar la cámara PRIMERO** en `setup()` |
| No entra modo descarga al subir | **GPIO 46 es strapping** (cinta) | **Desconectar GPIO 46** al subir + BOOT+RESET |
| `pin_'d3` error de compilación | Apóstrofo de más por tipeo | Borrar el `'` |
| Todo sale "antracnosis" | Domain shift del modelo + fotos oscuras / palta fuera de cuadro | Reentrenar con fotos propias + mejor luz/encuadre |

---

## 7. Buenas prácticas / pendientes

- **Servo MG996R:** nunca del 5V/3V3 del ESP32 (resetea la placa). Fuente aparte o
  +5V del L298N, **GND común**, capacitor de bulk.
- **Capacitores:** 1000 µF en la entrada 12V de cada L298N; 470–1000 µF en el servo;
  100 nF en bornes de cada motor.
- **GPIO 46:** desconectar al subir el firmware.
- **PSRAM = OPI** y **Flash 16MB** en Herramientas (Arduino) para mejor foto.
- **Autonomía con power bank:** muchas se apagan con bajo consumo (<50–100 mA);
  usar una con modo "always on" o un keep-alive.
- **IP fija:** usar el hotspot de la laptop (IP siempre igual) o mDNS para no
  reconfigurar `BACKEND_HOST` cada vez.
- **Worker de lotes:** auto-cerrar lotes abiertos tras 2 h de inactividad (pendiente).

---

## 8. Archivos del proyecto

- Firmware final: `firmware/paltacheck_ml_servo/paltacheck_ml_servo.ino`
- Pruebas: `firmware/paltacheck_ir_flujo/`, `paltacheck_servo_test_0y3/`, etc.
- Backend: `backend/main.py`, `backend/inference.py`, `backend/models.py`
- Frontend: `frontend/src/views/LoteActivo.vue`
- Modelos: `models/modelo_2_mobilenetv2_enfermedades.keras`
- Entrenamiento: `scripts/entrenar_modelo.py` + `docs/03_entrenar_modelo.md`
- Diagramas: `docs/pinout_paltacheck.svg`, `docs/conexion_total_paltacheck.svg`
