# PaltaCheck — Modelos de IA: entrenamiento, métricas y fundamentos

Fecha: 2026-07-07

El sistema usa una **cadena de 2 modelos** que corre en el backend sobre cada foto:

```
foto ──► ETAPA 1: GATE  (¿es palta?)  ──no_palta──► "no_es_palta"  (EXPULSA)
                        │
                        └─ palta ──► ETAPA 2: ENFERMEDAD (3 clases) ──► sana / antracnosis / scab
```

- **Etapa 1 (GATE):** modelo binario **palta / no_palta** — *reentrenado* por el equipo.
- **Etapa 2 (ENFERMEDAD):** modelo de **3 clases** (`sana / antracnosis / scab`) — el que **entrenamos y comparamos** (3 arquitecturas). En producción corre en **ONNX**.

---

## 1. Etapa 1 — GATE palta / no_palta (reentrenado)

| Ítem | Detalle |
|---|---|
| Archivo | `models/best_model.keras` (Keras 3, TensorFlow) |
| Clases | `["no_palta", "palta"]` (`best_model_classes.json`) |
| Preproceso | resize 224×224, **píxeles crudos 0–255** (el modelo normaliza adentro) |
| Umbral | `UMBRAL_NO_PALTA = 0.60` — solo rechaza como `no_palta` si la confianza ≥ 0.60 (le da el beneficio de la duda a paltas dudosas) |
| Rol | **Filtro previo.** Si el frame no es una palta (cinta vacía, rodillos, objeto raro) → `no_es_palta` y **no** se corre el modelo de enfermedad. |

**Por qué es imprescindible:** en las capturas reales del ESP32, **muchos frames son cinta/fondo vacío**. Sin este filtro, esos frames se colarían como "sana" y ensuciarían todo. El gate los expulsa antes de gastar cómputo en la enfermedad. (Nota: sus métricas de entrenamiento las tiene el equipo que lo reentrenó; aquí se documenta su rol e integración.)

---

## 2. Etapa 2 — Enfermedad (3 clases): entrenamiento

### 2.1. Datos y estrategia
- **Dataset de estudio**: ~3.983 imágenes etiquetadas `Healthy / Anthracnose / Scab` (paltas nítidas sobre fondo blanco).
- **Problema de dominio**: la cámara OV2640 del ESP32 ve baja resolución, borroso, desaturado y con el fondo de la cinta — muy distinto al estudio.
- **Solución (domain adaptation por degradación):** el `preprocess.py` **degrada** cada imagen para que se parezca a la cámara real: segmenta la palta, la **compone sobre parches de capturas reales**, baja resolución, desenfoque, desaturación, ruido y **compresión JPEG agresiva**. Se ajustó el blur (`BLUR_KERNELS (3,5)→(11,13)`) para acercar la nitidez a la de las capturas reales (~17 var. Laplaciano).
- **Transfer learning** desde ImageNet, 2 fases (cabeza congelada → fine-tuning), AMP en RTX 4060.
- **Split:** train 3.385 / val 598 (estratificado). Orden de clases (alfabético): **`[antracnosis, sana, scab]`**.

### 2.2. Resultados de validación (598 imágenes)

| Arquitectura | Params | **Macro F1** | Accuracy |
|---|---|---|---|
| **MobileNet-V2** ⭐ | 3.4 M | **0.781** | 0.784 |
| ResNet-50 | 25 M | 0.761 | 0.766 |
| EfficientNet-B0 | 5.3 M | 0.744 | 0.747 |

**F1 por clase:**

| Clase | MobileNet-V2 | ResNet-50 | EfficientNet-B0 |
|---|---|---|---|
| antracnosis | **0.739** | 0.722 | 0.696 |
| sana | **0.793** | 0.777 | 0.754 |
| scab | **0.811** | 0.784 | 0.782 |

![Comparativa de las 3 arquitecturas](../docs_reporte/comparativa_modelos.png)

### 2.3. Matrices de confusión (val, filas = real, orden `[antracnosis, sana, scab]`)

**MobileNet-V2** (macro F1 0.781)
```
                 →antrac  →sana  →scab
real antracnosis   116     32      5
real sana           31    211     23
real scab           14     24    142
```
**ResNet-50** (macro F1 0.761)
```
                 →antrac  →sana  →scab
real antracnosis   105     42      6
real sana           21    213     31
real scab           12     28    140
```
**EfficientNet-B0** (macro F1 0.744)
```
                 →antrac  →sana  →scab
real antracnosis   110     34      9
real sana           38    195     32
real scab           15     23    142
```

**Lectura de las matrices:**
- **antracnosis ↔ scab casi no se confunden** (esquinas: 5–9 y 12–15 casos). Las dos enfermedades quedan bien separadas.
- El error dominante es **enferma → sana** (falso negativo): p.ej. en MobileNet-V2, **32/153 antracnosis (21%) se leen como sana**. Es coherente con el blur fuerte (imita la cámara real): al perder detalle, lesiones leves se difuminan.

---

## 3. Validación sobre capturas REALES del ESP32 (558 imágenes, sin etiqueta)

Análisis **distribucional** (no accuracy, no hay verdad): cómo reparte cada modelo y si **colapsa** a una clase.

| Modelo | sana | scab | antracnosis | Conf. media | Veredicto |
|---|---|---|---|---|---|
| **MobileNet-V2** | 82.4 % | 16.8 % | 0.7 % | **0.78** | reparte OK |
| ResNet-50 | 59.9 % | 39.6 % | 0.5 % | 0.64 | reparte OK |
| EfficientNet-B0 | 86.6 % | 13.3 % | 0.2 % | 0.64 | ⚠ colapsa a sana |

![Validación en capturas reales](../docs_reporte/validacion_capturas.png)

**Hallazgos:**
- El colapso temido ("todo antracnosis") **no ocurrió**. En cambio, antracnosis **casi no dispara** en la cámara real (sub-detección).
- **EfficientNet-B0** es el que más colapsa a `sana` (86.6%) → el más débil.
- Gran parte del "sana" en los 3 viene de **frames de cinta vacía** → refuerza que el **gate** es imprescindible antes de la enfermedad.

---

## 4. Fundamento: ¿por qué MobileNet-V2? (el modelo actual)

El modelo servido en el backend es **MobileNet-V2** (`models/modelo_antracnosis.onnx`, clases `[antracnosis, sana, scab]`). Razones:

1. **Mejor F1 de validación** (0.781 > ResNet 0.761 > EfficientNet 0.744).
2. **Mejor comportamiento en capturas reales**: la **mayor confianza** (0.78) y **no colapsa** (a diferencia de EfficientNet).
3. **"Más grande no es mejor"**: ResNet-50 (25 M) **no gana** y tiene el **peor recall de antracnosis** (0.686). Con dataset chico + degradación fuerte, el modelo **más liviano regulariza mejor**.
4. **Doble victoria de eficiencia**: 3.4 M parámetros, **8.5 MB en ONNX** → ideal para el backend *y* candidato natural para on-device (ruta futura TFLite en el ESP32).
5. **Sin sobreajuste**: las curvas train/val suben juntas (val incluso mejor por la augmentation).

**Preproceso del ONNX (idéntico al entrenamiento):** resize 224 **BILINEAR** → `[0,1]` → normalizar ImageNet `mean=[0.485,0.456,0.406] std=[0.229,0.224,0.225]` → NCHW `(1,3,224,224)` → **softmax** sobre los logits. Se verificó que el backend reproduce PyTorch **10/10** (diff `2.7e-6`).

---

## 5. Limitaciones y trabajo futuro
- **Antracnosis sub-detecta en la cámara real** (se va a sana) — el riesgo principal (dejar pasar fruta enferma). Opciones: **bajar el umbral hacia "enferma"**, recolectar **capturas reales etiquetadas** de antracnosis para medir recall real, o reforzar la degradación/dominio.
- Para el paper: la tabla comparativa (F1/precisión/recall + matrices) de las 3 arquitecturas ya está lista y es un resultado sólido ("más grande no es mejor").

## 6. Archivos de los modelos

| Ruta | Qué es |
|---|---|
| `models/best_model.keras` (+ `_classes.json`) | **GATE** palta/no_palta (en uso) |
| `models/modelo_antracnosis.onnx` (+ `_classes.json`) | **ENFERMEDAD** 3 clases MobileNet-V2 (en uso) |
| `models/modelo_enfermedad*.keras` | Modelos de enfermedad Keras **anteriores** (2 clases salud/antracnosis) — reemplazados por el ONNX |
| `ml_antracnosis/outputs/<arch>/mejor_modelo.pt` | Pesos PyTorch entrenados (las 3 arquitecturas) |
| `ml_antracnosis/outputs/<arch>/reporte.txt`, `matriz_confusion.png`, `curvas.png` | Métricas y gráficas por arquitectura |
