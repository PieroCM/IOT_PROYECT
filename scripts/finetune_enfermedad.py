"""
============================================================================
 PaltaCheck - scripts/finetune_enfermedad.py
 ----------------------------------------------------------------------------
 FINE-TUNING del modelo de enfermedad para arreglar el DOMAIN SHIFT.
 Toma el backbone del modelo de 3 clases ya entrenado
 (models/modelo_2_mobilenetv2_enfermedades.keras) y le pone una CABEZA NUEVA de
 2 clases (salud / antracnosis), entrenada con TUS fotos del ESP32.

 Por que asi: el modelo de 3 clases aprendio a "ver" antracnosis en OTRO dataset
 y en la camara del ESP32 dice Scab~1.0 siempre (confiadamente equivocado).
 Reusar su backbone + reentrenar la cabeza con fotos reales del ESP32 adapta el
 modelo a TU dominio (luz, fondo, camara).

 Convencion: preprocess_input ADENTRO (el backbone del modelo 3-clases ya lo
 trae) -> el backend le pasa pixeles CRUDOS 0-255. Salida final: ["salud","antracnosis"].
 Anti-fuga: split POR PALTA.

 IMPORTANTE - NECESITAS DATOS: con las 6 antracnosis del lote_23 NO alcanza.
 Pasa un lote nuevo con 8-15 paltas CLARAMENTE enfermas (manchas negras hundidas),
 agrega ese lote en LOTES_EXTRA y reentrena.

 USO (raiz del repo):
     pip install tensorflow pillow
     python scripts/finetune_enfermedad.py
 Salida:
     models/modelo_enfermedad_ft.keras
     models/modelo_enfermedad_ft_classes.json   -> ["salud","antracnosis"]
============================================================================
"""
import re
import json
import random
from pathlib import Path

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── Etiquetas de enfermedad (revisadas por imagen) ──────────────────────────
DISEASE_LOTE = 23
ANTRACNOSIS  = [207, 208, 217, 218, 220, 222]   # paltas con manchas negras notables
EXCLUIR      = [228, -1]                         # sin fruta / basura
# Lotes nuevos con paltas enfermas (RELLENA esto cuando captures mas):
#   LOTES_EXTRA = {26: {"antracnosis": [301, 302, ...], "salud": [310, 311, ...]}}
LOTES_EXTRA = {}

# ─── Rutas ───────────────────────────────────────────────────────────────────
REPO = Path(__file__).resolve().parent
if REPO.name == "scripts":
    REPO = REPO.parent
CAPTURAS_DIR       = REPO / "backend" / "capturas"
BASE_3CLASES_PATH  = REPO / "models" / "modelo_2_mobilenetv2_enfermedades.keras"
OUT_MODEL          = REPO / "models" / "modelo_enfermedad_ft.keras"
OUT_CLASSES        = REPO / "models" / "modelo_enfermedad_ft_classes.json"

# ─── Config ──────────────────────────────────────────────────────────────────
CLASES       = ["salud", "antracnosis"]   # 0 = salud, 1 = antracnosis
IMG          = 224
BATCH        = 16
EPOCHS_HEAD  = 20
EPOCHS_FINE  = 12
VAL_FRACCION = 0.2
SEED         = 123

random.seed(SEED); np.random.seed(SEED); tf.random.set_seed(SEED)
_RE = re.compile(r"palta_(-?\w+?)_\d", re.IGNORECASE)


def recolectar():
    items = []
    carpeta = CAPTURAS_DIR / f"lote_{DISEASE_LOTE}"
    for img in sorted(carpeta.glob("*.jpg")):
        m = _RE.search(img.name); pid = m.group(1) if m else img.stem
        try: pid_i = int(pid)
        except ValueError: pid_i = -999
        if pid_i in EXCLUIR: continue
        lab = 1 if pid_i in ANTRACNOSIS else 0
        items.append((str(img), lab, f"L{DISEASE_LOTE}_p{pid}"))
    for lote, mapa in LOTES_EXTRA.items():
        for clase, ids in mapa.items():
            lab = CLASES.index(clase)
            for pid in ids:
                for img in sorted((CAPTURAS_DIR / f"lote_{lote}").glob(f"palta_{pid}_*.jpg")):
                    items.append((str(img), lab, f"L{lote}_p{pid}"))
    return items


def split_por_palta(items):
    grupo_label = {g: l for _, l, g in items}
    val = set()
    for idx in (0, 1):
        gs = sorted([g for g, l in grupo_label.items() if l == idx])
        random.shuffle(gs)
        n_val = max(1, int(round(len(gs) * VAL_FRACCION))) if gs else 0
        val.update(gs[:n_val])
    return ([it for it in items if it[2] not in val],
            [it for it in items if it[2] in val])


def cargar(ruta, label):
    img = tf.io.read_file(ruta)
    img = tf.image.decode_jpeg(img, channels=3)
    img = tf.image.resize(img, [IMG, IMG])    # crudo 0-255
    return img, tf.cast(label, tf.int32)


def hacer_ds(items, shuffle):
    if not items: return None
    ds = tf.data.Dataset.from_tensor_slices(([i[0] for i in items], [i[1] for i in items]))
    if shuffle:
        ds = ds.shuffle(len(items), seed=SEED, reshuffle_each_iteration=True)
    return ds.map(cargar, num_parallel_calls=tf.data.AUTOTUNE).batch(BATCH).prefetch(tf.data.AUTOTUNE)


def construir_backbone():
    """Reusa el backbone del modelo de 3 clases (quita su cabeza de 3 salidas).
    Si algo falla, cae a un MobileNetV2 imagenet fresco."""
    aug = keras.Sequential([
        layers.RandomFlip("horizontal"),
        layers.RandomTranslation(0.08, 0.08),
        layers.RandomZoom(0.12),
        layers.RandomRotation(0.06),
        layers.RandomBrightness(0.20, value_range=(0, 255)),
        layers.RandomContrast(0.20),
    ], name="augment")
    inputs = keras.Input(shape=(IMG, IMG, 3))   # pixeles 0-255
    x = aug(inputs)
    try:
        base3 = keras.models.load_model(str(BASE_3CLASES_PATH), compile=False)
        # feat = todo el modelo menos la Dense final de 3 clases (incluye su
        # preprocessing interno). Usamos la penultima capa como features.
        feat = keras.Model(base3.input, base3.layers[-2].output, name="backbone_3cls")
        feat.trainable = False
        x = feat(x, training=False)
        backbone = feat
        print("[FT] Backbone reusado del modelo de 3 clases.")
    except Exception as e:
        print(f"[FT] No pude reusar el modelo 3-clases ({e}); uso MobileNetV2 imagenet.")
        base = keras.applications.MobileNetV2(input_shape=(IMG, IMG, 3),
                                              include_top=False, weights="imagenet")
        base.trainable = False
        x = keras.applications.mobilenet_v2.preprocess_input(x)
        x = base(x, training=False)
        x = layers.GlobalAveragePooling2D()(x)
        backbone = base
    x = layers.Dropout(0.4)(x)
    if len(x.shape) > 2:                          # por si el feat no venia aplanado
        x = layers.GlobalAveragePooling2D()(x)
    out = layers.Dense(len(CLASES), activation="softmax", name="cabeza_2cls")(x)
    return keras.Model(inputs, out), backbone


def main():
    if not BASE_3CLASES_PATH.exists():
        print(f"[!] No encuentro {BASE_3CLASES_PATH} (se usara MobileNetV2 fresco).")
    items = recolectar()
    n1 = sum(1 for i in items if i[1] == 1); n0 = sum(1 for i in items if i[1] == 0)
    print(f"Fotos: {len(items)}  (salud={n0}, antracnosis={n1})")
    if n0 == 0 or n1 == 0:
        print("[X] Falta una clase."); return
    if n1 < 9:
        print(f"[!] Solo {n1} antracnosis: el modelo saldra DEBIL. Junta paltas enfermas "
              "claras y agregalas en LOTES_EXTRA antes de confiar en el.")

    train, val = split_por_palta(items)
    print(f"train: {len(train)} ({len({i[2] for i in train})} paltas) | "
          f"val: {len(val)} ({len({i[2] for i in val})} paltas)")
    if not val: print("[X] Sin val."); return
    train_ds, val_ds = hacer_ds(train, True), hacer_ds(val, False)

    t0 = sum(1 for i in train if i[1] == 0); t1 = sum(1 for i in train if i[1] == 1)
    tot = t0 + t1
    pesos = {0: tot/(2*t0), 1: tot/(2*t1)} if t0 and t1 else None
    print("train por clase:", {"salud": t0, "antracnosis": t1}, "pesos:", pesos)

    OUT_MODEL.parent.mkdir(parents=True, exist_ok=True)
    cbs = [keras.callbacks.ModelCheckpoint(str(OUT_MODEL), monitor="val_accuracy",
                                           mode="max", save_best_only=True),
           keras.callbacks.EarlyStopping(monitor="val_accuracy", mode="max",
                                         patience=8, restore_best_weights=True)]
    modelo, backbone = construir_backbone()

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
    cm = np.zeros((2, 2), int)
    for t, p in zip(y_true, y_pred): cm[t][p] += 1
    print("  Matriz (filas=real, cols=pred)  [0=salud,1=antracnosis]")
    print(f"    real salud       -> salud {cm[0,0]}  antrac {cm[0,1]}")
    print(f"    real antracnosis -> salud {cm[1,0]}  antrac {cm[1,1]}   (recall={cm[1,1]}/{cm[1].sum()})")
    print(f"  accuracy(val) = {np.trace(cm)/max(cm.sum(),1)*100:.1f}%")

    OUT_CLASSES.write_text(json.dumps(CLASES, indent=2))
    print(f"\n[OK] Guardado:\n     {OUT_MODEL}\n     {OUT_CLASSES}\n     clases: {CLASES}")
    print("     En inference.py apunta DISEASE_MODEL_PATH a modelo_enfermedad_ft.keras")
    print("     y usa DISEASE_CLASS_NAMES = ['salud','antracnosis'] (SIN colapso 3->2).")


if __name__ == "__main__":
    main()
