"""
Inferencia de enfermedades de palta — modelo MobileNetV2 (Keras 3).
Corre en el BACKEND. Recibe el JPEG que mandó el ESP32 y devuelve la clase.

El modelo trae el preprocesamiento ADENTRO (capas TrueDivide + Subtract =
escala los píxeles a [-1, 1] solo), así que aquí solo redimensionamos a
224x224 y pasamos los píxeles CRUDOS (0-255). NO hay que normalizar.
"""
import os
import io
import threading

MODEL_PATH = os.getenv(
    "MODEL_PATH",
    "/app/models/modelo_2_mobilenetv2_enfermedades.keras",
)

# Orden de clases del modelo (índice -> etiqueta), confirmado por el usuario.
CLASS_NAMES = ["scab", "healthy", "anthracnose"]

# Mapeo a la clasificación que entiende la BD/dashboard ('sana' | 'antracnosis').
# healthy = sana; scab y anthracnose = enfermedad => 'antracnosis' (rechazo).
A_DB = {"healthy": "sana", "anthracnose": "antracnosis", "scab": "antracnosis"}

_model = None
_load_error = None
_lock = threading.Lock()


def _get_model():
    """Carga el modelo una sola vez (perezoso). Si falla, lo recuerda y no reintenta."""
    global _model, _load_error
    if _model is not None or _load_error is not None:
        return _model
    with _lock:
        if _model is None and _load_error is None:
            try:
                os.environ.setdefault("KERAS_BACKEND", "tensorflow")
                import keras
                _model = keras.saving.load_model(MODEL_PATH, compile=False)
                print(f"[ML] Modelo cargado OK: {MODEL_PATH}")
            except Exception as e:
                _load_error = str(e)
                print(f"[ML] No se pudo cargar el modelo ({MODEL_PATH}): {e}")
    return _model


def predecir(img_bytes: bytes):
    """Clasifica una imagen JPEG. Devuelve un dict o None si no hay modelo.

    {
      "clasificacion": "sana" | "antracnosis",   # para la BD/dashboard
      "enfermedad":    "healthy" | "scab" | "anthracnose",
      "confianza":     0.0-1.0,
      "probabilidades": {clase: prob, ...}
    }
    """
    model = _get_model()
    if model is None:
        return None
    try:
        import numpy as np
        from PIL import Image

        img = Image.open(io.BytesIO(img_bytes)).convert("RGB").resize((224, 224))
        arr = np.asarray(img, dtype="float32")        # 0-255 crudos (el modelo normaliza adentro)
        arr = np.expand_dims(arr, axis=0)             # (1, 224, 224, 3)

        probs = model.predict(arr, verbose=0)[0]
        idx = int(np.argmax(probs))
        enfermedad = CLASS_NAMES[idx] if idx < len(CLASS_NAMES) else str(idx)

        return {
            "clasificacion": A_DB.get(enfermedad, "antracnosis"),
            "enfermedad": enfermedad,
            "confianza": round(float(probs[idx]), 4),
            "probabilidades": {
                CLASS_NAMES[i]: round(float(probs[i]), 4)
                for i in range(min(len(CLASS_NAMES), len(probs)))
            },
        }
    except Exception as e:
        print(f"[ML] Error en inferencia: {e}")
        return None
