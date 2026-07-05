"""
============================================================================
 PaltaCheck - scripts/entrenar_binario_full.py
 ----------------------------------------------------------------------------
 Entrena el binario PALTA / NO_PALTA usando TODOS los lotes de
 backend/capturas, con el etiquetado revisado a mano en scripts/etiquetas_palta.json
 (por palta: default del lote + excepciones; 'excluir' no entra al dataset).

 Convencion (igual que inference.py):
   - preprocess_input ADENTRO del modelo -> el backend pasa pixeles CRUDOS 0-255.
 Anti-fuga: split train/val POR PALTA (todas las fotos de una fruta a un lado).

 USO (desde la raiz del repo):
     pip install tensorflow pillow
     python scripts/entrenar_binario_full.py
 Salida:
     models/best_model.keras
     models/best_model_classes.json   -> ["no_palta","palta"]

 Opcion: pon INCLUIR_VACIO_COMO_NOPALTA = True para meter las tomas de cinta
 vacia como no_palta (le ensena "sin fruta = no palta"). Por defecto se excluyen.
============================================================================
"""
import re
import json
import glob
import random
from pathlib import Path
from collections import defaultdict

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── Rutas ───────────────────────────────────────────────────────────────────
REPO = Path(__file__).resolve().parent
if REPO.name == "scripts":
    REPO = REPO.parent
CAPTURAS_DIR = REPO / "backend" / "capturas"
ETIQUETAS    = REPO / "scripts" / "etiquetas_palta.json"
OUT_MODEL    = REPO / "models" / "best_model.keras"
OUT_CLASSES  = REPO / "models" / "best_model_classes.json"

# ─── Config ──────────────────────────────────────────────────────────────────
CLASES       = ["no_palta", "palta"]      # 0 = no_palta, 1 = palta
IMG          = 224
BATCH        = 16
EPOCHS_HEAD  = 15
EPOCHS_FINE  = 10
VAL_FRACCION = 0.2
SEED         = 123
INCLUIR_VACIO_COMO_NOPALTA = False        # las tomas de cinta vacia -> no_palta

random.seed(SEED); np.random.seed(SEED); tf.random.set_seed(SEED)
_RE = re.compile(r"palta_(-?\w+?)_\d", re.IGNORECASE)


def label_de(lote_cfg, pid):
    """Devuelve 'palta' | 'no_palta' | 'excluir' para una palta segun el JSON."""
    exc = lote_cfg.get("excepciones", {})
    if str(pid) in exc:
        return exc[str(pid)]
    return lote_cfg.get("default", "excluir")


def recolectar():
    mapa = json.loads(ETIQUETAS.read_text(encoding="utf-8"))["lotes"]
    items = []
    for lote, cfg in mapa.items():
        carpeta = CAPTURAS_DIR / f"lote_{lote}"
        if not carpeta.exists():
            continue
        for img in sorted(carpeta.glob("*.jpg")):
            m = _RE.search(img.name)
            pid = m.group(1) if m else img.stem
            clase = label_de(cfg, pid)
            if clase == "excluir":
                if INCLUIR_VACIO_COMO_NOPALTA and cfg.get("default") == "palta":
                    clase = "no_palta"   # tratar las excepciones 'vacio' como no_palta
                else:
                    continue
            if clase not in CLASES:
                continue
            items.append((str(img), CLASES.index(clase), f"L{lote}_p{pid}"))
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
    img = tf.image.resize(img, [IMG, IMG])   # crudo 0-255
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
        layers.RandomZoom(0.12),
        layers.RandomRotation(0.06),
        layers.RandomBrightness(0.20),
        layers.RandomContrast(0.20),
    ], name="augment")
    base = keras.applications.MobileNetV2(input_shape=(IMG, IMG, 3),
                                          include_top=False, weights="imagenet")
    base.trainable = False
    inputs = keras.Input(shape=(IMG, IMG, 3))
    x = aug(inputs)
    x = keras.applications.mobilenet_v2.preprocess_input(x)
    x = base(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.4)(x)
    out = layers.Dense(len(CLASES), activation="softmax", name="predictions")(x)
    return keras.Model(inputs, out), base


def main():
    items = recolectar()
    n1 = sum(1 for i in items if i[1] == 1); n0 = sum(1 for i in items if i[1] == 0)
    paltas_p = len({i[2] for i in items if i[1] == 1})
    paltas_n = len({i[2] for i in items if i[1] == 0})
    print(f"Fotos: {len(items)}  (palta={n1} de {paltas_p} frutas, no_palta={n0} de {paltas_n} frutas)")
    if n0 == 0 or n1 == 0:
        print("[X] Falta una clase. Revisa etiquetas_palta.json."); return

    train, val = split_por_palta(items)
    print(f"train: {len(train)} ({len({i[2] for i in train})} frutas) | "
          f"val: {len(val)} ({len({i[2] for i in val})} frutas)")
    if not val: print("[X] Sin val."); return

    train_ds, val_ds = hacer_ds(train, True), hacer_ds(val, False)
    t0 = sum(1 for i in train if i[1] == 0); t1 = sum(1 for i in train if i[1] == 1)
    tot = t0 + t1
    pesos = {0: tot/(2*t0), 1: tot/(2*t1)} if t0 and t1 else None
    print("Fotos train por clase:", {"no_palta": t0, "palta": t1}, "pesos:", pesos)

    OUT_MODEL.parent.mkdir(parents=True, exist_ok=True)
    cbs = [keras.callbacks.ModelCheckpoint(str(OUT_MODEL), monitor="val_accuracy",
                                           mode="max", save_best_only=True),
           keras.callbacks.EarlyStopping(monitor="val_accuracy", mode="max",
                                         patience=6, restore_best_weights=True)]
    modelo, base = construir()

    print("\n=== FASE 1: cabeza ===")
    modelo.compile(optimizer=keras.optimizers.Adam(1e-3),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_HEAD,
               class_weight=pesos, callbacks=cbs)

    print("\n=== FASE 2: fine-tuning ===")
    base.trainable = True
    for capa in base.layers[:-30]:
        capa.trainable = False
    modelo.compile(optimizer=keras.optimizers.Adam(1e-5),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_FINE,
               class_weight=pesos, callbacks=cbs)

    print("\n=== Evaluacion (val, frutas NO vistas) ===")
    modelo = keras.models.load_model(str(OUT_MODEL))
    y_true, y_pred = [], []
    for xb, yb in val_ds:
        p = modelo.predict(xb, verbose=0)
        y_pred.extend(np.argmax(p, axis=1).tolist()); y_true.extend(yb.numpy().tolist())
    y_true, y_pred = np.array(y_true), np.array(y_pred)
    cm = np.zeros((2, 2), int)
    for t, p in zip(y_true, y_pred): cm[t][p] += 1
    print("  Matriz (filas=real, cols=pred)  [0=no_palta,1=palta]")
    print(f"    real no_palta -> pred: no_palta {cm[0,0]}  palta {cm[0,1]}")
    print(f"    real palta    -> pred: no_palta {cm[1,0]}  palta {cm[1,1]}")
    print(f"  accuracy(val) = {np.trace(cm)/max(cm.sum(),1)*100:.1f}%")

    OUT_CLASSES.write_text(json.dumps(CLASES, indent=2))
    print(f"\n[OK] Guardado:\n     {OUT_MODEL}\n     {OUT_CLASSES}\n     clases: {CLASES}")


if __name__ == "__main__":
    main()
