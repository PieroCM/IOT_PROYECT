"""
Configuración central de PaltaCheck · clasificador de antracnosis.
Edita AQUÍ y todos los scripts (preprocess / train / export / infer) lo respetan.
"""
from pathlib import Path

# ── Rutas ────────────────────────────────────────────────────────────────────
# Carpeta que descomprimiste de archive.zip (la que tiene el CSV y las imágenes).
RAW_DIR = Path("data/archive")
CSV_NAME = "Avocado labels 1.csv"
IMG_SUBDIR = "Aguacates sin fondo entrenamiento/Aguacates sin fondo entrenamiento"

# Carpeta con las capturas reales del ESP32 (capturas.zip descomprimido).
# Se usan como fondos realistas al compositar (BACKGROUND_MODE = "capturas").
CAPTURAS_DIR = Path("data/capturas")

# Salidas
PROC_DIR = Path("data/processed")      # dataset ya degradado y dividido
OUT_DIR = Path("outputs")              # pesos, métricas, gráficas

# ── Definición del problema ──────────────────────────────────────────────────
# El dataset trae 3 condiciones: Healthy, Scab, Anthracnose.
# CLASS_MODE decide cuántas clases entrena el modelo:
#
#   "3clases"  -> sana / antracnosis / scab.  El operario ve QUÉ enfermedad
#                 tiene la palta. Usa las 3.983 imágenes. RECOMENDADO.
#   "binario"  -> sana / antracnosis. Usa SCAB_POLICY para tratar la sarna:
#                    "drop"    -> ignora Scab (solo antracnosis puro)
#                    "enferma" -> Scab cuenta como "antracnosis" (rechazo)
CLASS_MODE = "3clases"           # "3clases" | "binario"
SCAB_POLICY = "enferma"          # solo aplica si CLASS_MODE="binario"

if CLASS_MODE == "3clases":
    CLASSES = ["sana", "antracnosis", "scab"]
else:
    CLASSES = ["sana", "antracnosis"]

# ── Fondo (el punto clave del dominio) ───────────────────────────────────────
# El dataset viene con fondo BLANCO; tu cámara ve la cinta/rodillos. Opciones:
#   "keep"      -> deja el fondo blanco (NO recomendado: no matchea tu cámara).
#   "black"     -> recorta la palta y la pega sobre negro.
#   "random"    -> recorta y pega sobre color/ruido aleatorio (domain randomization).
#   "capturas"  -> recorta y pega sobre PARCHES de tus fotos reales del ESP32.
#                  Es lo que mejor cierra la brecha de dominio. RECOMENDADO.
BACKGROUND_MODE = "capturas"     # "keep" | "black" | "random" | "capturas"

# ── Parámetros de la degradación (simula la cámara OV2640 del ESP32) ─────────
CAM_SIZE = 240                   # resolución nativa típica de tus capturas
JPEG_QUALITY = (10, 18)          # rango de calidad JPEG (el firmware usa ~12)
SATURATION = (0.45, 0.65)        # tus capturas tienen saturación ~49/255
# Nitidez objetivo ~17 (var. Laplaciano de las capturas reales @240, frame
# completo como lo ve infer.py). Con (3,5) el degradado salía ~54 (demasiado
# nítido); (11,13) lo baja a ~27, cerca del real sin borrar la textura del scab.
BLUR_KERNELS = (11, 13)          # desenfoque (la OV2640 es blanda)

# ── Entrenamiento ────────────────────────────────────────────────────────────
# Arquitectura: "mobilenet_v2" | "efficientnet_b0" | "resnet50" | "vgg16"
ARCH = "efficientnet_b0"
IMG_SIZE = 224                   # entrada de la CNN (ImageNet estándar)
BATCH_SIZE = 32                  # cabe de sobra en los 8 GB de la RTX 4060
EPOCHS_HEAD = 5                  # fase 1: solo la cabeza (backbone congelado)
EPOCHS_FINE = 15                 # fase 2: fine-tuning de todo con LR bajo
LR_HEAD = 1e-3
LR_FINE = 1e-5
VAL_SPLIT = 0.15
SEED = 42
NUM_WORKERS = 0                  # Windows: 4 colgaba el DataLoader (spawn), 0 = fiable
