"""
============================================================================
 PaltaCheck - scripts/entrenar_enfermedad_final.py
 ----------------------------------------------------------------------------
 Entrena/afina el modelo de ENFERMEDAD con las paltas del ESP32 de TODOS los
 lotes. Reusa el etiquetado palta/no_palta (scripts/etiquetas_palta.json) para
 saber cuales son paltas, y aplica el etiquetado de enfermedad de abajo.

 CLASES (revisadas por imagen):
   - antracnosis: manchas negras/hundidas    -> lote_23: 208, 218, 220
   - scab:        mancha marron/rugosa rojiza -> lote_23: 207, 217, 222
   - salud:       el resto de paltas limpias
 Por defecto scab se COLAPSA en antracnosis (ambas = rechazo) -> 2 clases
 ["salud","antracnosis"]. Pon COLAPSAR_SCAB=False para 3 clases (necesitas mas datos).

 BALANCEO (porque hay MUCHAS sanas y pocas enfermas):
   1) class_weight (inverso a la frecuencia)
   2) OVERSAMPLING de la(s) clase(s) minoritaria(s) en train (repite sus fotos)
   3) AUGMENTATION fuerte (para que las copias no sean identicas)

 Fine-tuning: reusa el backbone del modelo de 3 clases
 (models/modelo_2_mobilenetv2_enfermedades.keras) + cabeza nueva. Si no esta,
 cae a MobileNetV2 imagenet.

 Convencion: pixeles CRUDOS 0-255 (preprocess adentro). Split POR PALTA.

 USO (raiz del repo):
     pip install tensorflow pillow
     python scripts/entrenar_enfermedad_final.py
 Salida:
     models/modelo_enfermedad_ft.keras
     models/modelo_enfermedad_ft_classes.json
============================================================================
"""
import re
import json
import random
from pathlib import Path
from collections import Counter

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── Etiquetado de enfermedad (por lote -> ids de palta) ─────────────────────
ANTRACNOSIS = {23: [208, 218, 220]}       # manchas negras/hundidas
SCAB        = {23: [207, 217, 222]}        # mancha marron/rugosa
# Cuando captures paltas claramente enfermas en lotes nuevos, agregalas aqui.

COLAPSAR_SCAB = True   # True -> scab se cuenta como antracnosis (2 clases)

# ─── Rutas ───────────────────────────────────────────────────────────────────
REPO = Path(__file__).resolve().parent
if REPO.name == "scripts":
    REPO = REPO.parent
CAPTURAS_DIR      = REPO / "backend" / "capturas"
ETIQUETAS_PALTA   = REPO / "scripts" / "etiquetas_palta.json"
BASE_3CLASES_PATH = REPO / "models" / "modelo_2_mobilenetv2_enfermedades.keras"
OUT_MODEL         = REPO / "models" / "modelo_enfermedad_ft.keras"
OUT_CLASSES       = REPO / "models" / "modelo_enfermedad_ft_classes.json"

# ─── Config ──────────────────────────────────────────────────────────────────
IMG          = 224
BATCH        = 16
EPOCHS_HEAD  = 20
EPOCHS_FINE  = 12
VAL_FRACCION = 0.2
SEED         = 123
random.seed(SEED); np.random.seed(SEED); tf.random.set_seed(SEED)
_RE = re.compile(r"palta_(-?\w+?)_\d", re.IGNORECASE)

CLASES = ["salud", "antracnosis"] if COLAPSAR_SCAB else ["salud", "antracnosis", "scab"]


def _clase_enf(lote_int, pid_int):
    if pid_int in ANTRACNOSIS.get(lote_int, []):
        return "antracnosis"
    if pid_int in SCAB.get(lote_int, []):
        return "antracnosis" if COLAPSAR_SCAB else "scab"
    return "salud"


def _es_palta(cfg, pid):
    return cfg.get("excepciones", {}).get(str(pid), cfg.get("default", "excluir")) == "palta"


def recolectar():
    mapa = json.loads(ETIQUETAS_PALTA.read_text(encoding="utf-8"))["lotes"]
    items = []
    for lote, cfg in mapa.items():
        carpeta = CAPTURAS_DIR / f"lote_{lote}"
        if not carpeta.exists():
            continue
        for img in sorted(carpeta.glob("*.jpg")):
            m = _RE.search(img.name); pid = m.group(1) if m else img.stem
            if not _es_palta(cfg, pid):
                continue
            try: pid_i = int(pid)
            except ValueError: continue
            clase = _clase_enf(int(lote), pid_i)
            items.append((str(img), CLASES.index(clase), f"L{lote}_p{pid}"))
    return items


def split_por_palta(items):
    grupo_label = {g: l for _, l, g in items}
    val = set()
    for idx in range(len(CLASES)):
        gs = sorted([g for g, l in grupo_label.items() if l == idx])
        random.shuffle(gs)
        n_val = max(1, int(round(len(gs) * VAL_FRACCION))) if gs else 0
        val.update(gs[:n_val])
    return ([it for it in items if it[2] not in val],
            [it for it in items if it[2] in val])


def oversample(train):
    """Repite las fotos de las clases minoritarias hasta ~igualar la mayoritaria."""
    porclase = {}
    for it in train:
        porclase.setdefault(it[1], []).append(it)
    if not porclase:
        return train
    maxn = max(len(v) for v in porclase.values())
    balanceado = []
    for lab, its in porclase.items():
        reps = maxn // len(its)
        resto = maxn - reps * len(its)
        balanceado += its * reps + random.sample(its, resto) if its else []
    random.shuffle(balanceado)
    return balanceado


def cargar(ruta, label):
    img = tf.io.read_file(ruta)
    img = tf.image.decode_jpeg(img, channels=3)
    img = tf.image.resize(img, [IMG, IMG])
    return img, tf.cast(label, tf.int32)


def hacer_ds(items, shuffle):
    if not items: return None
    ds = tf.data.Dataset.from_tensor_slices(([i[0] for i in items], [i[1] for i in items]))
    if shuffle:
        ds = ds.shuffle(len(items), seed=SEED, reshuffle_each_iteration=True)
    return ds.map(cargar, num_parallel_calls=tf.data.AUTOTUNE).batch(BATCH).prefetch(tf.data.AUTOTUNE)


def construir():
    aug = keras.Sequential([
        layers.RandomFlip("horizontal"),
        layers.RandomTranslation(0.08, 0.08),
        layers.RandomZoom(0.15),
        layers.RandomRotation(0.08),
        layers.RandomBrightness(0.22, value_range=(0, 255)),
        layers.RandomContrast(0.22),
    ], name="augment")
    inputs = keras.Input(shape=(IMG, IMG, 3))
    x = aug(inputs)
    try:
        base3 = keras.models.load_model(str(BASE_3CLASES_PATH), compile=False)
        feat = keras.Model(base3.input, base3.layers[-2].output, name="backbone_3cls")
        feat.trainable = False
        x = feat(x, training=False)
        backbone = feat
        print("[FT] Backbone reusado del modelo de 3 clases.")
    except Exception as e:
        print(f"[FT] No pude reusar el 3-clases ({e}); MobileNetV2 imagenet.")
        base = keras.applications.MobileNetV2(input_shape=(IMG, IMG, 3),
                                              include_top=False, weights="imagenet")
        base.trainable = False
        x = keras.applications.mobilenet_v2.preprocess_input(x)
        x = base(x, training=False)
        x = layers.GlobalAveragePooling2D()(x)
        backbone = base
    if len(x.shape) > 2:
        x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.4)(x)
    out = layers.Dense(len(CLASES), activation="softmax", name="cabeza")(x)
    return keras.Model(inputs, out), backbone


def main():
    items = recolectar()
    cuenta = Counter(i[1] for i in items)
    print("Clases:", CLASES)
    print("Fotos por clase:", {CLASES[k]: v for k, v in sorted(cuenta.items())})
    if len(cuenta) < 2:
        print("[X] Falta al menos una clase enferma."); return
    n_enf = sum(v for k, v in cuenta.items() if k != CLASES.index("salud"))
    if n_enf < 9:
        print(f"[!] Solo {n_enf} fotos enfermas: aun con balanceo el modelo saldra DEBIL. "
              "Captura paltas claramente enfermas y agregalas en ANTRACNOSIS/SCAB.")

    train, val = split_por_palta(items)
    print(f"train: {len(train)} | val: {len(val)}  (split por palta, frutas no vistas)")
    if not val:
        print("[X] Sin val (necesitas >=2 paltas por clase)."); return

    # BALANCEO 2: oversampling de minoritarias en TRAIN
    train_bal = oversample(train)
    print("train tras oversampling:", {CLASES[k]: v for k, v in
                                        sorted(Counter(i[1] for i in train_bal).items())})
    train_ds, val_ds = hacer_ds(train_bal, True), hacer_ds(val, False)

    # BALANCEO 1: class_weight (sobre el train ORIGINAL, no el oversampleado)
    ct = Counter(i[1] for i in train); tot = sum(ct.values())
    pesos = {k: tot / (len(CLASES) * ct.get(k, 1)) for k in range(len(CLASES))}
    print("class_weight:", pesos)

    OUT_MODEL.parent.mkdir(parents=True, exist_ok=True)
    cbs = [keras.callbacks.ModelCheckpoint(str(OUT_MODEL), monitor="val_accuracy",
                                           mode="max", save_best_only=True),
           keras.callbacks.EarlyStopping(monitor="val_accuracy", mode="max",
                                         patience=8, restore_best_weights=True)]
    modelo, backbone = construir()

    print("\n=== FASE 1: cabeza nueva (backbone congelado) ===")
    modelo.compile(optimizer=keras.optimizers.Adam(1e-3),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_HEAD,
               class_weight=pesos, callbacks=cbs)

    print("\n=== FASE 2: fine-tuning (ultimas capas del backbone) ===")
    backbone.trainable = True
    for capa in backbone.layers[:-30]:
        capa.trainable = False
    modelo.compile(optimizer=keras.optimizers.Adam(1e-5),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_FINE,
               class_weight=pesos, callbacks=cbs)

    print("\n=== Evaluacion (val, paltas NO vistas) ===")
    modelo = keras.models.load_model(str(OUT_MODEL))
    y_true, y_pred = [], []
    for xb, yb in val_ds:
        p = modelo.predict(xb, verbose=0)
        y_pred.extend(np.argmax(p, axis=1).tolist()); y_true.extend(yb.numpy().tolist())
    y_true, y_pred = np.array(y_true), np.array(y_pred)
    n = len(CLASES); cm = np.zeros((n, n), int)
    for t, p in zip(y_true, y_pred): cm[t][p] += 1
    print("  Matriz (filas=real, cols=pred):", CLASES)
    for i in range(n):
        print(f"    real {CLASES[i]:<12} ->", {CLASES[j]: int(cm[i][j]) for j in range(n)})
    ia = CLASES.index("antracnosis")
    rec = cm[ia, ia] / max(cm[ia].sum(), 1)
    print(f"  recall antracnosis = {cm[ia,ia]}/{cm[ia].sum()} = {rec*100:.0f}%")
    print(f"  accuracy(val) = {np.trace(cm)/max(cm.sum(),1)*100:.1f}%")

    OUT_CLASSES.write_text(json.dumps(CLASES, indent=2))
    print(f"\n[OK] {OUT_MODEL}\n     clases: {CLASES}")
    print("     En inference.py: DISEASE_MODEL_PATH -> modelo_enfermedad_ft.keras,")
    print(f"     DISEASE_CLASS_NAMES = {CLASES}  (2 clases, SIN colapso 3->2), DISEASE_ENABLED=true.")


if __name__ == "__main__":
    main()
