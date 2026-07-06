# PaltaCheck · Clasificador de antracnosis (sana vs. enferma)

Reentrena un clasificador **adaptado a tu cámara ESP32-CAM**: toma el dataset de
estudio (paltas nítidas sobre fondo blanco) y lo transforma con OpenCV para que
se parezca a lo que realmente ve tu cámara (baja resolución, borroso,
desaturado, con el fondo de tu cinta). Así el modelo entrena en el mismo dominio
en el que va a trabajar.

## ¿Por qué este enfoque? (tu idea, evaluada)

Tu intuición es **correcta**: entrenar con imágenes de estudio y desplegar sobre
fotos de una OV2640 es un cambio de dominio que degrada mucho la precisión.
Simular la cámara con filtros (*domain adaptation por degradación*) es una
técnica válida y estándar. Dos matices importantes que añadí:

1. **El fondo es el problema más grande, no la calidad.** Tu dataset tiene fondo
   blanco perfecto; tu cámara ve la cinta y los rodillos. Si solo recortas la
   palta y la dejas sobre blanco, el modelo aprende "palta = sobre blanco" y en
   la cinta se confunde. Por eso el preprocesado **compone la palta recortada
   sobre parches de tus propias capturas** (`BACKGROUND_MODE = "capturas"`). Es
   lo que más cierra la brecha.
2. **Scab (sarna) es un punto ciego.** El dataset trae 3 condiciones: Healthy,
   Scab y Anthracnose. Si haces "sana vs antracnosis" y **tiras** Scab, cuando
   llegue una palta con sarna el modelo la puede dar como "sana". Por defecto
   dejé `SCAB_POLICY = "enferma"` (Scab cuenta como enferma/rechazo). Si de
   verdad solo te importa antracnosis, cámbialo a `"drop"` en `config.py`.

## ¿Qué arquitectura de CNN? (fundamentado)

Para ~4.000 imágenes, **transfer learning desde ImageNet** es obligatorio
(entrenar de cero sobreajusta). Comparativa:

| Arquitectura | Params | Por qué |
|---|---|---|
| **EfficientNet-B0** ⭐ | 5.3 M | Mejor relación precisión/costo. Recomendado para inferencia en el **backend**. Entrena rápido en tu 4060. → *default* |
| **MobileNet-V2** | 3.4 M | El más ligero. Elígelo si algún día quieres el modelo **on-device** (TFLite Micro en el ESP32). |
| **ResNet-50** | 25 M | Baseline robusto y clásico. Úsalo para la tabla comparativa del paper ("¿un modelo más grande ayuda?"). |
| VGG-16 ❌ | 138 M | **No recomendado**: sin conexiones residuales, lentísimo, se come la VRAM y sobreajusta con pocos datos. Solo como baseline histórico. |

**Recomendación:** empieza con **EfficientNet-B0 corriendo en el backend** (tu
ESP32 ya sube la foto al backend por `/api/captura/foto`; que el servidor
clasifique es mucho más preciso que meter una CNN en el microcontrolador).
Para el paper, entrena también MobileNet-V2 y ResNet-50 y compara — te da una
tabla de resultados sólida.

> **Sobre "palta o no palta":** de acuerdo, ese modelo puede quedarse como está.
> Este proyecto solo reemplaza el clasificador de 3 clases por el binario
> sana/antracnosis.

---

## Puesta en marcha (VS Code + CUDA en tu RTX 4060)

### 1. Coloca los datos
Dentro de esta carpeta `ml_antracnosis/`, crea `data/` y descomprime ahí:

```
ml_antracnosis/
├── data/
│   ├── archive/      <- contenido de archive.zip (el CSV + "Aguacates sin fondo...")
│   └── capturas/     <- contenido de capturas.zip (las carpetas lote_XX/)
```

### 2. Entorno con CUDA (una sola vez)
Abre la terminal de VS Code en esta carpeta y pega:

```bash
python -m venv .venv
.\.venv\Scripts\activate
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu124
pip install -r requirements.txt
```

Verifica que la GPU está visible:

```bash
python -c "import torch; print(torch.cuda.is_available(), torch.cuda.get_device_name(0))"
# Debe imprimir:  True  NVIDIA GeForce RTX 4060 ...
```

### 3. Preprocesa el dataset (aplica los filtros de cámara)
```bash
python preprocess.py --limit 100   # prueba rápida: revisa data/processed/
python preprocess.py               # dataset completo
```

### 4. Entrena
```bash
python train.py                    # EfficientNet-B0 (default)
python train.py --arch mobilenet_v2
python train.py --arch resnet50
```
Salidas en `outputs/`: `mejor_modelo.pt`, `reporte.txt`, `curvas.png`,
`matriz_confusion.png`.

### 5. Prueba con una captura real de tu ESP32
```bash
python infer.py --img data/capturas/lote_13/palta_50_20260622_185551.jpg
```

### 6. (Opcional) Exporta para el backend
```bash
python export_onnx.py              # genera outputs/modelo_antracnosis.onnx
```

---

## Prompts para VS Code (cópialos al chat de Copilot / Claude en VS Code)

**Conectar el modelo al backend FastAPI:**
> "En `backend/main.py`, en el endpoint `POST /api/palta`, carga el modelo de
> `ml_antracnosis/outputs/mejor_modelo.pt` (o el `.onnx`) una sola vez al
> arrancar, y cuando llegue la foto en `POST /api/captura/foto` corre
> `infer.predecir()` sobre la imagen recibida y guarda `clasificacion` y
> `confianza` reales en la fila `palta`, en vez del placeholder `'sana'/0.5`."

**Afinar el filtro de degradación a tu cámara:**
> "En `ml_antracnosis/preprocess.py`, ajusta `degradar_camara()` para que la
> saturación media y la nitidez (varianza del Laplaciano) del dataset procesado
> coincidan con las de mis capturas reales en `data/capturas/`."

**Añadir métricas al paper:**
> "Lee `ml_antracnosis/outputs/reporte.txt` y genera una tabla comparativa de
> F1, precisión y recall entre MobileNetV2, EfficientNet-B0 y ResNet-50."

---

## Ajustes rápidos (todo en `config.py`)
- `SCAB_POLICY`: `"enferma"` (recom.) o `"drop"`.
- `BACKGROUND_MODE`: `"capturas"` (recom.), `"random"`, `"black"` o `"keep"`.
- `ARCH`, `BATCH_SIZE`, `EPOCHS_FINE`, learning rates.
- Si en Windows el DataLoader da error, pon `NUM_WORKERS = 0`.
