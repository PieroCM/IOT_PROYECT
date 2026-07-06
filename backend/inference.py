"""
Inferencia PaltaCheck — CADENA de 2 modelos, en el BACKEND.

  1) GATE BINARIO palta / no_palta   (Keras 3, best_model.keras):
        - no_palta -> "no_es_palta"    (se RECHAZA / expulsa)
        - palta    -> pasa al modelo de enfermedad
  2) ENFERMEDAD 3 clases (ONNX MobileNet-V2, modelo_antracnosis.onnx, onnxruntime):
        clases (orden del checkpoint): [antracnosis, sana, scab]
        - sana        -> "sana"         (se ACEPTA / pasa)
        - antracnosis -> "antracnosis"  (se RECHAZA / expulsa)
        - scab        -> "scab"         (se RECHAZA / expulsa)

El GATE (Keras) lleva su normalización ADENTRO: se le pasan píxeles CRUDOS 0-255.
El modelo de ENFERMEDAD (ONNX de PyTorch) NO: requiere resize 224 -> [0,1] ->
normalización ImageNet -> NCHW. Ese preprocesado va aquí en `_preparar_onnx`.
"""
import os
import io
import json
import threading

# ─── Rutas de los modelos ────────────────────────────────────────────────────
BINARY_MODEL_PATH = os.getenv(
    "BINARY_MODEL_PATH",
    "/app/models/best_model.keras",
)
# Etapa 2 ahora es ONNX (antes .keras malogrado).
DISEASE_MODEL_PATH = os.getenv(
    "DISEASE_MODEL_PATH",
    "/app/models/modelo_antracnosis.onnx",
)
DISEASE_CLASSES_PATH = os.getenv(
    "DISEASE_CLASSES_PATH",
    "/app/models/modelo_antracnosis_classes.json",
)

# Orden de clases (índice -> etiqueta)
BINARY_CLASS_NAMES = ["no_palta", "palta"]           # gate

# El ONNX emite logits en el orden en que se entrenó (alfabético). Se lee del
# JSON como fuente de verdad; fallback al orden verificado del checkpoint.
def _cargar_clases_enfermedad():
    try:
        with open(DISEASE_CLASSES_PATH, "r", encoding="utf-8") as f:
            clases = json.load(f)
        if isinstance(clases, list) and clases:
            return [str(c) for c in clases]
    except Exception as e:
        print(f"[ML] No pude leer clases de enfermedad ({DISEASE_CLASSES_PATH}): {e}")
    return ["antracnosis", "sana", "scab"]

DISEASE_CLASS_NAMES = _cargar_clases_enfermedad()

# Normalización ImageNet (igual que el entrenamiento en torchvision).
_MEAN = [0.485, 0.456, 0.406]
_STD  = [0.229, 0.224, 0.225]

# Si la confianza de "no_palta" es menor a esto, le damos el beneficio de la
# duda y la tratamos como palta (evita descartar paltas reales por poco).
UMBRAL_NO_PALTA = float(os.getenv("UMBRAL_NO_PALTA", "0.60"))

# Etapa de enfermedad ON/OFF. Ahora ON por defecto (ya hay modelo ONNX real).
# Si se apaga, toda palta pasa como "sana" (el gate palta/no-palta sigue activo).
DISEASE_ENABLED = os.getenv("DISEASE_ENABLED", "true").lower() == "true"

_binary_model   = None    # gate palta/no_palta (Keras)
_disease_sess    = None    # enfermedad (onnxruntime InferenceSession)
_disease_in_name = None    # nombre del input del grafo ONNX
_err_binary  = None
_err_disease = None
_lock = threading.Lock()


def _cargar_keras(path):
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
                _binary_model = _cargar_keras(BINARY_MODEL_PATH)
                print(f"[ML] Gate binario (Keras) cargado: {BINARY_MODEL_PATH}")
            except Exception as e:
                _err_binary = str(e)
                print(f"[ML] No se pudo cargar el gate ({BINARY_MODEL_PATH}): {e}")
    return _binary_model


def _get_disease_session():
    global _disease_sess, _disease_in_name, _err_disease
    if _disease_sess is not None or _err_disease is not None:
        return _disease_sess
    with _lock:
        if _disease_sess is None and _err_disease is None:
            try:
                import onnxruntime as ort
                _disease_sess = ort.InferenceSession(
                    DISEASE_MODEL_PATH, providers=["CPUExecutionProvider"])
                _disease_in_name = _disease_sess.get_inputs()[0].name
                print(f"[ML] Modelo enfermedad (ONNX) cargado: {DISEASE_MODEL_PATH} "
                      f"| clases={DISEASE_CLASS_NAMES}")
            except Exception as e:
                _err_disease = str(e)
                print(f"[ML] No se pudo cargar enfermedad ONNX ({DISEASE_MODEL_PATH}): {e}")
    return _disease_sess


def _preparar(img_bytes):
    """Preprocesado del GATE Keras: 224x224, píxeles CRUDOS 0-255, NHWC."""
    import numpy as np
    from PIL import Image
    img = Image.open(io.BytesIO(img_bytes)).convert("RGB").resize((224, 224))
    arr = np.asarray(img, dtype="float32")        # 0-255 (el modelo normaliza adentro)
    return np.expand_dims(arr, axis=0)            # (1, 224, 224, 3)


def _preparar_onnx(img_bytes):
    """Preprocesado del modelo de ENFERMEDAD ONNX (idéntico a ml_antracnosis/infer):
    resize 224 BILINEAR -> [0,1] -> normalizar ImageNet -> NCHW (1,3,224,224)."""
    import numpy as np
    from PIL import Image
    img = Image.open(io.BytesIO(img_bytes)).convert("RGB").resize((224, 224), Image.BILINEAR)
    arr = np.asarray(img, dtype="float32") / 255.0                 # HWC en [0,1]
    arr = (arr - np.array(_MEAN, dtype="float32")) / np.array(_STD, dtype="float32")
    arr = np.transpose(arr, (2, 0, 1))                            # CHW
    return np.expand_dims(arr, axis=0).astype("float32")          # (1, 3, 224, 224)


def _softmax(x):
    import numpy as np
    x = np.asarray(x, dtype="float64")
    e = np.exp(x - np.max(x))
    return e / e.sum()


def predecir(img_bytes: bytes):
    """Clasifica una foto JPEG: gate palta/no_palta (Keras) -> enfermedad (ONNX).

    Devuelve un dict (o None si el gate no está disponible):
    {
      "es_palta":       bool,
      "clasificacion":  "no_es_palta" | "sana" | "antracnosis" | "scab",
      "enfermedad":     igual que clasificacion (compat/logging),
      "confianza":      0.0-1.0,   # confianza de la clase ganadora de ESTA foto
      "gate":           {"clase": "palta"|"no_palta", "confianza": x},
      "confianza_gate": 0.0-1.0,   # prob de que SÍ es palta (gate)
      "probabilidades_gate":       {"no_palta": x, "palta": y},
      "probabilidades_enfermedad": {"sana": x, "antracnosis": y, "scab": z} | None,
      "probabilidades":            (enfermedad si es palta, gate si no)  # compat
    }
    """
    try:
        import numpy as np

        # ── ETAPA 1: GATE palta / no_palta (Keras) ───────────────────────────
        bin_model = _get_binary_model()
        if bin_model is None:
            return None

        bprobs = bin_model.predict(_preparar(img_bytes), verbose=0)[0]
        bidx = int(np.argmax(bprobs))
        bclase = BINARY_CLASS_NAMES[bidx] if bidx < len(BINARY_CLASS_NAMES) else str(bidx)
        bconf = float(bprobs[bidx])
        gate = {"clase": bclase, "confianza": round(bconf, 4)}
        prob_no_palta = float(bprobs[BINARY_CLASS_NAMES.index("no_palta")]) \
            if "no_palta" in BINARY_CLASS_NAMES else 0.0
        prob_palta = float(bprobs[BINARY_CLASS_NAMES.index("palta")]) \
            if "palta" in BINARY_CLASS_NAMES else (1.0 - prob_no_palta)
        probs_gate = {
            BINARY_CLASS_NAMES[i]: round(float(bprobs[i]), 4)
            for i in range(min(len(BINARY_CLASS_NAMES), len(bprobs)))
        }

        # NO es palta (con umbral) -> rechazo. No corre el modelo de enfermedad.
        if bclase == "no_palta" and prob_no_palta >= UMBRAL_NO_PALTA:
            return {
                "es_palta": False,
                "clasificacion": "no_es_palta",
                "enfermedad": "no_palta",
                "confianza": round(prob_no_palta, 4),
                "gate": gate,
                "confianza_gate": round(prob_palta, 4),
                "probabilidades_gate": probs_gate,
                "probabilidades_enfermedad": None,
                "probabilidades": probs_gate,
            }

        # ── ES PALTA ─────────────────────────────────────────────────────────
        # Enfermedad desactivada: toda palta pasa como "sana" (gate sigue activo).
        if not DISEASE_ENABLED:
            return _resultado_sana(gate, prob_palta)

        # ── ETAPA 2: ENFERMEDAD (ONNX 3 clases) ──────────────────────────────
        sess = _get_disease_session()
        if sess is None:
            print("[ML] Modelo de enfermedad no disponible -> se asume 'sana'")
            return _resultado_sana(gate, prob_palta)

        logits = sess.run(None, {_disease_in_name: _preparar_onnx(img_bytes)})[0][0]
        probs = _softmax(logits)                       # orden = DISEASE_CLASS_NAMES
        didx = int(np.argmax(probs))
        clasificacion = DISEASE_CLASS_NAMES[didx]      # sana | antracnosis | scab
        conf = float(probs[didx])
        probs_enf = {DISEASE_CLASS_NAMES[i]: round(float(probs[i]), 4)
                     for i in range(len(DISEASE_CLASS_NAMES))}

        return {
            "es_palta": True,
            "clasificacion": clasificacion,
            "enfermedad": clasificacion,
            "confianza": round(conf, 4),
            "gate": gate,
            "confianza_gate": round(prob_palta, 4),
            "probabilidades_gate": probs_gate,
            "probabilidades_enfermedad": probs_enf,
            "probabilidades": probs_enf,
        }
    except Exception as e:
        print(f"[ML] Error en inferencia: {e}")
        return None


def _resultado_sana(gate, prob_palta):
    """Palta aceptada como sana (enfermedad OFF o no disponible)."""
    return {
        "es_palta": True,
        "clasificacion": "sana",
        "enfermedad": "sana",
        "confianza": round(prob_palta, 4),
        "gate": gate,
        "confianza_gate": round(prob_palta, 4),
        "probabilidades_gate": {"no_palta": round(1 - prob_palta, 4),
                                "palta": round(prob_palta, 4)},
        "probabilidades_enfermedad": None,
        "probabilidades": {"sana": round(prob_palta, 4)},
    }
