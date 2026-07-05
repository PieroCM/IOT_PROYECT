# 03 · Dashboard del Lote Activo ([LoteActivo.vue](../frontend/src/views/LoteActivo.vue))

Referencia completa de la vista **Lote Activo**: cada card, qué dato muestra, de dónde
proviene, cómo se calcula, y los **modelos de IA** que alimentan la clasificación.

---

## 1. Modelos usados (IA)

El sistema usa **UN solo modelo** (se quitó el de enfermedades). Corre en el **backend**
([backend/inference.py](../backend/inference.py)), no en el ESP32.

| Modelo | Archivo | Arquitectura | Clases | Rol |
|--------|---------|--------------|--------|-----|
| **Binario Palta / No-Palta** | `models/best_model.keras` | MobileNetV2 (transfer learning, `preprocess_input` ADENTRO) | `["no_palta", "palta"]` | Decide si la fruta ES palta o NO |

- **Entrada**: cada foto JPG que sube el ESP32 (redimensionada a 224×224, píxeles crudos 0-255; el modelo normaliza internamente).
- **Umbral**: `UMBRAL_NO_PALTA = 0.60`. Solo marca `no_palta` si su probabilidad ≥ 0.60; si no, por beneficio de la duda la trata como palta.
- **Votación (3 fotos)**: por cada palta el ESP32 toma **3 fotos**; cada una vota y gana la **mayoría** ([backend/main.py](../backend/main.py) en `/api/captura/foto`). La **confianza** = votos_ganadores / total_votos.
- **Entrenamiento**: [scripts/entrenar_binario.py](../scripts/entrenar_binario.py) (split por palta anti-fuga, augmentation, dropout 0.4, EarlyStopping). Última métrica: **91.7% en validación**.

### Mapeo de la clasificación (interno → dashboard)
La columna `palta.clasificacion` reusa valores existentes en la BD:

| Interno (BD) | Significado | En el dashboard | Servo (firmware) |
|--------------|-------------|-----------------|------------------|
| `"sana"` | ES palta (aceptada) | **"Palta"** (verde ✓) | NO se mueve, pasa |
| `"no_es_palta"` | NO es palta (rechazada) | **"No palta"** (rojo ✕) | ABRE compuerta y expulsa |

> En el view, el helper `esNoPalta(c)` = `c === 'no_es_palta'` y `etiqueta(c)` devuelve
> `"No palta"` o `"Palta"`. Cualquier valor distinto de `no_es_palta` se muestra como "Palta".

---

## 2. Flujo de datos (de dónde salen los cards)

La vista usa el store Pinia [frontend/src/stores/palta.js](../frontend/src/stores/palta.js), que
**pollea el backend cada 3 s** mientras hay lote activo:

| Dato del store | Endpoint backend | Contenido |
|----------------|------------------|-----------|
| `store.loteActivo` | `POST /api/lote` / `GET /api/lote/activo` | `{id, codigo, inicio}` |
| `store.kpis` | `GET /api/lote/{id}/kpis` | `{total, sanas, rechazadas, ...}` |
| `store.paltas` | `GET /api/lote/{id}/paltas` | array de paltas c/ `clasificacion, confianza, foto_url, sensor{r,g,b,lux,temp,humedad}, timestamp` |
| `store.ultimaPalta` | (getter) | última palta del array |
| `store.velocidadPpm` | (getter) | paltas / minutos transcurridos |
| `store._speedHistory` | (interno) | historial de ppm para el sparkline |
| `capturas` | `GET /api/palta/{id}/fotos` | las 3 fotos de la palta actual |

---

## 3. Estados de la vista

### 3.1 Sin lote activo
Card central con:
- Ícono + título **"No hay lote activo"**.
- Input de **código** (ej. `LOTE-2024-008`) + botón **"Abrir nuevo lote"** → `store.abrirLote()`.
- Muestra `store.error` si falla.

### 3.2 Con lote activo → **Header** (barra superior)
| Elemento | Dato | Fuente |
|----------|------|--------|
| `#código` | Código del lote | `store.loteActivo.codigo` |
| Badge **EN PROCESO** | Indicador fijo (punto pulsante) | — |
| Cronómetro `HH:MM:SS` | Tiempo desde que abrió el lote | `Date.now() − loteActivo.inicio` |
| Botón **Cerrar Lote** | Cierra el lote | `store.cerrarLote()` |

---

## 4. Cards del dashboard

### 🟩 Fila superior — KPIs (2 cards)

#### Card 1 · **Paltas Procesadas**
- **Muestra**: `kpis.total` **/ 50** + barra de progreso.
- **Dato**: `store.kpis.total` (cuenta de TODAS las frutas procesadas del lote, `GET /api/lote/{id}/kpis`).
- **Barra**: `min(total/50 · 100, 100)%`. El `/ 50` es una meta fija de referencia del lote.
- **Sub**: "de este lote".

#### Card 2 · **Velocidad de Línea**
- **Muestra**: `store.velocidadPpm` **ppm** + mini-gráfico (sparkline).
- **Dato**: getter `velocidadPpm = nº paltas / minutos transcurridos` (ritmo de procesamiento).
- **Sparkline**: barras con `store._speedHistory` (últimas ~10 lecturas de ppm); si no hay historial usa un placeholder.

---

### 🟦 Columna izquierda

#### Card 3 · **Captura de cámara**
- **Imagen grande**: la **foto más reciente** de la palta actual (`ultimaCaptura.url`, tomada de `capturas`, la última de las 3 vueltas).
- **Badge** (arriba derecha): **"Palta"** (verde) / **"No palta"** (rojo) según `ultimaCaptura.clasificacion`.
- **Meta**: `Palta #id · hora`.
- **Miniaturas**: las **3 fotos** de la palta (las 3 vueltas de los rodillos) para compararlas — `capturas` desde `GET /api/palta/{id}/fotos`.
- **Estado vacío**: 📷 "Esperando captura de la cámara…".
- **Nota técnica**: usa la URL única de archivo (no `/api/palta/{id}/foto`) para evitar caché del navegador.

#### Card 4 · **Sensores · Palta #id**
Lectura de sensores de la **última palta** (`store.ultimaPalta.sensor`). 5 tiles:

| Tile | Dato | Fuente |
|------|------|--------|
| 🌡️ **Temperatura** | `temp` °C | DHT22 |
| 💧 **Humedad** | `humedad` % | DHT22 |
| 💡 **Lux** | `lux` | TCS34725 |
| 🎨 **RGB** | `r, g, b` + swatch de color real (`rgb(r,g,b)`) | TCS34725 |
| ⚠️ **Riesgo ambiental** | Badge **BAJO / MEDIO / ALTO** | Calculado de temp+humedad |

Regla del **riesgo ambiental** (`riesgoBadge`):
- `HR < 70 % y T < 26 °C` → **BAJO** (verde)
- `HR < 80 % y T < 28 °C` → **MEDIO** (amarillo)
- si no → **ALTO** (rojo)
- sin datos → `—`

---

### 🟨 Columna derecha

#### Card 5 · **Última Palta Clasificada**
- **Resultado grande**: caja verde con ✓ **"PALTA"** o roja con ✕ **"NO PALTA"** (`store.ultimaPalta.clasificacion`).
- **Valores RGB**: `R / G / B / Lux` de esa palta (`store.ultimaPalta.sensor`).
- **Barra de Confianza**: `store.ultimaPalta.confianza · 100%` (confianza del veredicto por votación de las 3 fotos).
- **"hace Xs"**: segundos desde el `timestamp` de la última palta.
- **Estado vacío**: "Esperando primera palta…".

#### Card 6 · **Paltas del lote** (tabla)
Últimas **8** paltas (`ultimasOcho` = `store.paltas` invertido, primeras 8). Columnas:

| Columna | Dato |
|---------|------|
| **#** | `palta.id` |
| **Hora** | `formatHora(timestamp)` |
| **Resultado** | badge **Palta** (verde) / **No palta** (rojo) |
| **Conf.** | `confianza · 100%` |
| **T°** | `sensor.temp` °C |
| **HR** | `sensor.humedad` % |

La fila se tiñe suave de verde (palta) o rojo (no palta). Vacío → "Sin paltas registradas".

---

### ⬛ Barra inferior (footer)
| Elemento | Dato / Acción |
|----------|---------------|
| Resumen | `"{total} paltas procesadas en {HH:MM:SS}"` |
| **↓ Exportar CSV** | Descarga CSV con `#, Hora, Resultado, Confianza, R, G, B, Lux, Temp, Humedad` de todas las paltas (`exportarCSV()`) |
| **Cerrar Lote** | `store.cerrarLote()` |

---

## 5. Resumen de cards

| # | Card | Ubicación | Dato principal | Modelo/Sensor |
|---|------|-----------|----------------|---------------|
| 1 | Paltas Procesadas | KPI | `kpis.total / 50` + barra | conteo |
| 2 | Velocidad de Línea | KPI | `velocidadPpm` + sparkline | derivado |
| 3 | Captura de cámara | Izq. | foto + badge Palta/No-palta + 3 miniaturas | **modelo binario** |
| 4 | Sensores · Palta | Izq. | Temp, Humedad, Lux, RGB, Riesgo | DHT22 + TCS34725 |
| 5 | Última Palta Clasificada | Der. | Palta/No-palta + RGB + confianza | **modelo binario** |
| 6 | Paltas del lote | Der. | tabla últimas 8 | binario + sensores |

---

## 6. Notas
- Todo se actualiza solo (polling cada 3 s); no hay que refrescar.
- El **modelo de enfermedades ya no se usa**: el veredicto es únicamente **Palta / No-palta**.
- La palta aceptada se guarda internamente como `"sana"` en la BD (reuso para no migrar el
  esquema); el dashboard siempre muestra **"Palta"**. Si se quisiera el valor literal `"palta"`
  en BD/CSV haría falta una mini-migración del `CHECK` + ajustar la votación.
- Contrato de las APIs que alimentan esta vista: ver [docs/02_api_esp32.md](02_api_esp32.md).

---

## 7. UI / Diseño visual

### 7.1 Estructura (layout)
La vista es una columna flex de altura completa: **header** fijo → **body** scrollable →
**footer** fijo. El body tiene una fila de **KPIs (2 columnas)** y debajo un bloque de **dos
columnas** (izquierda `3fr` / derecha `2fr`).

```
┌──────────────────────────────────────────────────────────────────────┐
│ Lote Activo — #LOTE-023     ● EN PROCESO        00:12:34  [Cerrar Lote]│  header (60px)
├──────────────────────────────────────────────────────────────────────┤
│ ┌───────────────────────┐  ┌───────────────────────┐                  │
│ │ Paltas Procesadas     │  │ Velocidad de Línea    │      KPIs (2 col) │
│ │  12 / 50              │  │  8 ppm                │                   │
│ │  ▓▓▓▓▓░░░░░ (barra)   │  │  ▁▂▃▅▂▃▅ (sparkline)  │                   │
│ └───────────────────────┘  └───────────────────────┘                  │
│ ┌─────────────────────────────┐  ┌──────────────────────────┐         │
│ │ Captura de cámara   [Palta] │  │ Última Palta Clasificada │  two-col │
│ │  ┌───────────────────────┐  │  │  ┌────────────────────┐  │ (3fr/2fr)│
│ │  │        [ FOTO ]       │  │  │  │      ✓ PALTA        │  │         │
│ │  └───────────────────────┘  │  │  └────────────────────┘  │         │
│ │  Palta #48 · 14:03:21       │  │  R/G/B · Lux              │         │
│ │  [▪][▪][▪]  (3 miniaturas)  │  │  Confianza ▓▓▓▓▓▓▓░ 90%   │         │
│ ├─────────────────────────────┤  │  hace 5s                 │         │
│ │ Sensores · Palta #48        │  ├──────────────────────────┤         │
│ │ ┌────┐┌────┐┌────┐┌────┐┌──┐│  │ Paltas del lote (tabla)  │         │
│ │ │🌡️22││💧64││💡415││🎨  ││⚠️││  │ # Hora Result Conf T° HR │         │
│ │ └────┘└────┘└────┘└────┘└──┘│  │ 48 14:03 [Palta] 90% ..  │         │
│ └─────────────────────────────┘  └──────────────────────────┘         │
├──────────────────────────────────────────────────────────────────────┤
│ 12 paltas procesadas en 00:12:34        [↓ Exportar CSV] [Cerrar Lote] │  footer (60px)
└──────────────────────────────────────────────────────────────────────┘
```

### 7.2 Paleta de colores (CSS variables globales)
| Variable | Uso |
|----------|-----|
| `--verde-acento` | Palta/aceptado, barras, badge EN PROCESO, botón primario |
| `--rojo-rechazo` | No-palta/rechazo, botón peligro |
| `#c87f0a` (amarillo) | Riesgo MEDIO |
| `--fondo-cards` / `--fondo-pagina` | Fondo de cards / fondo general |
| `--borde-cards` | Bordes y separadores |
| `--texto-primary` / `--texto-secondary` | Texto principal / secundario |

### 7.3 Estilo de los componentes
- **Card base** (`.card`): fondo blanco, borde `1px`, `border-radius: 12px`, sombra sutil; título `.card-title` en negrita 0.95rem.
- **KPI** (`.kpi-card`): número grande (2rem). Barra de progreso verde (`.progress-bar/.progress-fill`, alto 6px, transición). Sparkline: barras verdes translúcidas de 32px de alto.
- **Badges pill** (`.badge-pill`): pastilla redondeada; variantes `--verde` (Palta), `--rojo` (No palta), `--amarillo` (riesgo medio), `--sm` (pequeña, en tabla/cámara).
- **Cámara** (`.camara-*`): imagen `260px` con `object-fit: contain` (foto completa, sin recortar) sobre fondo negro; meta en monoespaciado; 3 miniaturas de 64px.
- **Sensores** (`.sensores-grid`): grid `auto-fit minmax(120px,1fr)`; cada tile centrado con ícono + label + valor; swatch RGB = cuadro con el color real `rgb(r,g,b)`.
- **Resultado** (`.result-display`): caja grande verde (`--sana`) o roja (`--antracnosis`) con ícono ✓/✕ y texto **PALTA / NO PALTA**.
- **Tabla** (`.paltas-table`): header sticky, filas tenues verde (`.row--sana`) / rojo (`.row--antracnosis`), scroll vertical (máx 280px), monoespaciado en celdas.
- **Botones** (`.btn`): primario verde, peligro rojo, outline verde (Exportar) / outline rojo (Cerrar).
- **Header**: badge EN PROCESO con punto pulsante (`.dot--pulse`, animación) + cronómetro monoespaciado.

### 7.4 Responsive
| Ancho | Comportamiento |
|-------|----------------|
| `> 1100px` | KPIs 2 columnas · two-col `3fr / 2fr` |
| `≤ 1100px` | two-col se apila (1 columna); KPIs siguen en 2 |
| `≤ 640px` | KPIs 1 columna · sensores 2 columnas · header con wrap; paddings reducidos |

### 7.5 Microinteracciones
- Punto **EN PROCESO** pulsa (animación `pulse-dot`).
- Barras de progreso/confianza con transición `width 0.4s`.
- Todo se **refresca solo cada 3 s** (polling); la foto grande siempre muestra la más reciente.

