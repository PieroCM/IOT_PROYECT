"""
Inferencia PaltaCheck — CADENA de 2 modelos binarios (Keras 3), en el BACKEND.

  1) BINARIO palta / no_palta:
        - no_palta -> "no_es_palta"    (se RECHAZA / expulsa)
        - palta    -> pasa al modelo de enfermedad
  2) ENFERMEDAD salud / antracnosis (solo si es palta):
        - salud       -> "sana"         (se ACEPTA / pasa)
        - antracnosis -> "antracnosis"  (se RECHAZA / expulsa)

Ambos modelos llevan el preprocesamiento ADENTRO (escala a [-1,1]); aqui solo
redimensionamos a 224x224 y pasamos pixeles CRUDOS 0-255. Se entrenan con
scripts/entrenar_binario.py y scripts/entrenar_enfermedad.py.
"""
import os
import io
import threading

# ─── Rutas de los modelos ────────────────────────────────────────────────────
BINARY_MODEL_PATH = os.getenv(
    "BINARY_MODEL_PATH",
    "/app/models/best_model.keras",
)
DISEASE_MODEL_PATH = os.getenv(
    "DISEASE_MODEL_PATH",
    "/app/models/modelo_enfermedad_ft.keras",
)

# Orden de clases (indice -> etiqueta)
BINARY_CLASS_NAMES  = ["no_palta", "palta"]      # 1er filtro
# Modelo de enfermedad FINE-TUNEADO: ya son 2 clases (0=salud, 1=antracnosis),
# decisión directa (sin colapso 3->2).
DISEASE_CLASS_NAMES = ["salud", "antracnosis"]

# Si la confianza de "no_palta" es menor a esto, le damos el beneficio de la
# duda y la tratamos como palta (evita descartar paltas reales por poco).
UMBRAL_NO_PALTA = float(os.getenv("UMBRAL_NO_PALTA", "0.60"))

# Marca "antracnosis" solo si p(Anthracnose)+p(Scab) >= este umbral (sensibilidad).
UMBRAL_ANTRACNOSIS = float(os.getenv("UMBRAL_ANTRACNOSIS", "0.50"))

# Etapa de enfermedad ON/OFF. OFF (default) mientras no haya un modelo confiable:
# toda palta pasa como "sana" (el filtro palta/no-palta sigue activo). El modelo
# de 3 clases tiene domain shift con la cámara del ESP32 (marca todo antracnosis).
DISEASE_ENABLED = os.getenv("DISEASE_ENABLED", "false").lower() == "true"

_binary_model  = None    # binario palta/no_palta
_disease_model = None    # enfermedad salud/antracnosis
_err_binary  = None
_err_disease = None
_lock = threading.Lock()


def _cargar(path):
    os.environ.setdefault("KERAS_BACKEND", "tensorflow")
    import keras
    return keras.saving.load_model(path, compile=False)


def _get_binary_model():
    global _binary_model, _err_binary
    if _binary_model is not None or _err_binary is not None:
        return _binary_model
    with _lock:
        if _binary_model is None and _err_binary is None:
            try:
                _binary_model = _cargar(BINARY_MODEL_PATH)
                print(f"[ML] Modelo binario cargado: {BINARY_MODEL_PATH}")
            except Exception as e:
                _err_binary = str(e)
                print(f"[ML] No se pudo cargar binario ({BINARY_MODEL_PATH}): {e}")
    return _binary_model


def _get_disease_model():
    global _disease_model, _err_disease
    if _disease_model is not None or _err_disease is not None:
        return _disease_model
    with _lock:
        if _disease_model is None and _err_disease is None:
            try:
                _disease_model = _cargar(DISEASE_MODEL_PATH)
                print(f"[ML] Modelo enfermedad cargado: {DISEASE_MODEL_PATH}")
            except Exception as e:
                _err_disease = str(e)
                print(f"[ML] No se pudo cargar enfermedad ({DISEASE_MODEL_PATH}): {e}")
    return _disease_model


def _preparar(img_bytes):
    import numpy as np
    from PIL import Image
    img = Image.open(io.BytesIO(img_bytes)).convert("RGB").resize((224, 224))
    arr = np.asarray(img, dtype="float32")        # 0-255 crudos (el modelo normaliza adentro)
    return np.expand_dims(arr, axis=0)            # (1, 224, 224, 3)


def predecir(img_bytes: bytes):
    """Clasifica una foto JPEG con la cadena de 2 modelos.

    Devuelve un dict (o None si no hay ningun modelo):
    {
      "es_palta":      bool,
      "clasificacion": "no_es_palta" | "sana" | "antracnosis",
      "enfermedad":    "no_palta" | "salud" | "antracnosis",
      "confianza":     0.0-1.0,             # confianza del veredicto de ESTA foto
      "gate":          {"clase": "...", "confianza": x},
      "probabilidades": {...}
    }
    """
    try:
        import numpy as np
        arr = _preparar(img_bytes)

        # ── ÚNICO MODELO: BINARIO palta / no_palta ───────────────────────────
        bin_model = _get_binary_model()
        if bin_model is None:
            return None

        bprobs = bin_model.predict(arr, verbose=0)[0]
        bidx = int(np.argmax(bprobs))
        bclase = BINARY_CLASS_NAMES[bidx] if bidx < len(BINARY_CLASS_NAMES) else str(bidx)
        bconf = float(bprobs[bidx])
        gate = {"clase": bclase, "confianza": round(bconf, 4)}
        probabilidades = {
            BINARY_CLASS_NAMES[i]: round(float(bprobs[i]), 4)
            for i in range(min(len(BINARY_CLASS_NAMES), len(bprobs)))
        }
        prob_no_palta = float(bprobs[BINARY_CLASS_NAMES.index("no_palta")]) \
            if "no_palta" in BINARY_CLASS_NAMES else 0.0
        prob_palta = float(bprobs[BINARY_CLASS_NAMES.index("palta")]) \
            if "palta" in BINARY_CLASS_NAMES else (1.0 - prob_no_palta)

        # NO es palta (con umbral) -> rechazo. Si no, se acepta como palta.
        if bclase == "no_palta" and prob_no_palta >= UMBRAL_NO_PALTA:
            return {
                "es_palta": False,
                "clasificacion": "no_es_palta",
                "enfermedad": "no_palta",
                "confianza": round(prob_no_palta, 4),
                "gate": gate,
                "probabilidades": probabilidades,
            }

        # ── ES PALTA ─────────────────────────────────────────────────────────
        # ARREGLO INTERINO: si la etapa de enfermedad está desactivada, toda
        # palta pasa como "sana" (no corremos el modelo de enfermedad).
        if not DISEASE_ENABLED:
            return {
                "es_palta": True,
                "clasificacion": "sana",
                "enfermedad": "salud",
                "confianza": round(prob_palta, 4),
                "gate": gate,
                "probabilidades": probabilidades,
            }

        # ── 2) MODELO DE ENFERMEDAD (salud / antracnosis) ────────────────────
        dis_model = _get_disease_model()
        if dis_model is None:
            # Sin modelo de enfermedad: NO rompemos el flujo, la damos por sana.
            print("[ML] Modelo de enfermedad no disponible -> se asume 'sana'")
            return {
                "es_palta": True,
                "clasificacion": "sana",
                "enfermedad": "salud",
                "confianza": round(prob_palta, 4),
                "gate": gate,
                "probabilidades": probabilidades,
            }

        # Modelo FT de 2 clases [salud, antracnosis] -> decisión directa.
        dprobs = dis_model.predict(arr, verbose=0)[0]
        p_antracnosis = float(dprobs[1])
        if p_antracnosis >= UMBRAL_ANTRACNOSIS:
            clasificacion, enfermedad, conf = "antracnosis", "antracnosis", p_antracnosis
        else:
            clasificacion, enfermedad, conf = "sana", "salud", float(dprobs[0])

        return {
            "es_palta": True,
            "clasificacion": clasificacion,   # "sana" | "antracnosis"
            "enfermedad": enfermedad,
            "confianza": round(conf, 4),
            "gate": gate,
            "probabilidades": {
                "salud":       round(float(dprobs[0]), 4),
                "antracnosis": round(float(dprobs[1]), 4),
            },
        }
    except Exception as e:
        print(f"[ML] Error en inferencia: {e}")
        return None
