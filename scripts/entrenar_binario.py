"""
PaltaCheck — Entrenamiento del MODELO BINARIO (palta / no_palta)
================================================================
Reentrena el primer filtro de la cadena (`models/best_model.keras`) con TUS
fotos reales del ESP32, para que distinga "es palta" de "no es palta"
(mandarina, limón, mano, cinta vacía, etc.).

DE DÓNDE SALEN LAS FOTOS
------------------------
El backend ya guarda cada foto en:
    backend/capturas/lote_<id>/palta_<paltaid>_<fecha>.jpg
Para etiquetar, abres lotes por clase: unos con PALTAS y otros con NO-paltas.
Aquí indicas, POR NÚMERO DE LOTE, cuáles son de cada clase (mapeo explícito):

    LOTES_PALTA     = [10, 11]     # lotes donde pasaste PALTAS
    LOTES_NO_PALTA  = [13]         # lotes con mandarina/limón/mano/cinta vacía

…o por línea de comandos:
    python scripts/entrenar_binario.py --palta 10,11 --no-palta 13

SPLIT POR PALTA (anti-fuga)
---------------------------
Todas las fotos de una MISMA fruta (mismo `palta_<id>`) van juntas a train o a
val, NUNCA mezcladas. Si no, el modelo "memoriza" la fruta y el accuracy sale
inflado (fuga de datos).

CONVENCIÓN (igual que inference.py / entrenar_modelo.py)
-------------------------------------------------------
El `preprocess_input` de MobileNetV2 va ADENTRO del modelo, así que el backend
le pasa píxeles CRUDOS 0-255 (solo redimensiona a 224). NO normalizar fuera.

Salida:
    models/best_model.keras
    models/best_model_classes.json   ->  ["no_palta", "palta"]

Requisitos:  pip install tensorflow pillow
Ejecutar:    python scripts/entrenar_binario.py
"""
import os
import re
import glob
import json
import random
import argparse
from collections import Counter

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── Configuración (edítala aquí o pásala por CLI) ────────────────────────────
CAPTURAS_DIR   = "backend/capturas"      # carpeta raíz de capturas
LOTES_PALTA    = [10, 11]                 # <-- números de lote con PALTAS
LOTES_NO_PALTA = [13]                     # <-- números de lote con NO-paltas

CLASS_NAMES = ["no_palta", "palta"]      # índice 0 = no_palta, 1 = palta
IMG         = 224
BATCH       = 16
EPOCHS      = 20                          # fase 1: cabeza congelada (más aug -> +epochs)
FINE_EPOCHS = 10                          # fase 2: fine-tuning últimas ~30 capas
VAL_SPLIT   = 0.2
SEED        = 123
SALIDA      = "models/best_model.keras"
SALIDA_JSON = "models/best_model_classes.json"

AUTOTUNE = tf.data.AUTOTUNE


# ─── 1) Recolectar fotos por clase, agrupadas por palta_<id> ──────────────────
def recolectar(lotes, label):
    """Devuelve {grupo: [archivos]} donde grupo = 'clase_paltaid' (unidad de split)."""
    grupos = {}
    for lid in lotes:
        carpeta = os.path.join(CAPTURAS_DIR, f"lote_{lid}")
        encontrados = glob.glob(os.path.join(carpeta, "palta_*.jpg"))
        if not encontrados:
            print(f"  [aviso] sin fotos en {carpeta}")
        for f in encontrados:
            m = re.search(r"palta_(\w+?)_\d", os.path.basename(f))
            pid = m.group(1) if m else os.path.basename(f)
            grupos.setdefault(f"{label}_{pid}", []).append(f)
    return grupos


def split_por_grupo(grupos, val_frac, seed):
    """Reparte GRUPOS (no fotos) a train/val: toda una fruta cae de un solo lado."""
    keys = sorted(grupos.keys())
    random.Random(seed).shuffle(keys)
    n_val = int(len(keys) * val_frac)
    if len(keys) > 1:
        n_val = max(1, n_val)            # al menos 1 grupo a val si hay >1
    val_keys = set(keys[:n_val])
    train, val = [], []
    for k, files in grupos.items():
        (val if k in val_keys else train).extend(files)
    return train, val


# ─── 2) Pipeline tf.data desde listas de archivos ─────────────────────────────
def cargar_img(path, label):
    raw = tf.io.read_file(path)
    img = tf.io.decode_jpeg(raw, channels=3)
    img = tf.image.resize(img, [IMG, IMG])
    return tf.cast(img, tf.float32), label   # 0-255 crudo (el modelo normaliza adentro)


def make_ds(pares, training):
    paths  = [p for p, _ in pares]
    labels = [l for _, l in pares]
    ds = tf.data.Dataset.from_tensor_slices((paths, labels))
    if training:
        ds = ds.shuffle(len(paths) or 1, seed=SEED, reshuffle_each_iteration=True)
    ds = ds.map(cargar_img, num_parallel_calls=AUTOTUNE)
    ds = ds.batch(BATCH).prefetch(AUTOTUNE)
    return ds


# ─── 3) Modelo MobileNetV2 (preprocess + augmentation SUAVE adentro) ──────────
def construir_modelo():
    # Augmentation REFORZADA (sin tocar color/tono: el color distingue palta
    # verde de fruta naranja). Solo geometría + luz.
    data_augmentation = keras.Sequential([
        layers.RandomFlip("horizontal"),
        layers.RandomTranslation(0.06, 0.06),   # la fruta no siempre queda centrada
        layers.RandomZoom(0.10),                 # leve acercar / alejar
        layers.RandomRotation(0.05),             # rotación pequeña (cámara fija)
        layers.RandomBrightness(0.20),           # cambios de luz del galpón
        layers.RandomContrast(0.20),
    ], name="data_augmentation")

    base = keras.applications.MobileNetV2(
        input_shape=(IMG, IMG, 3), include_top=False, weights="imagenet")
    base.trainable = False

    inputs = keras.Input(shape=(IMG, IMG, 3))
    x = data_augmentation(inputs)
    x = keras.applications.mobilenet_v2.preprocess_input(x)   # [-1,1] DENTRO del modelo
    x = base(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.4)(x)
    x = layers.Dense(128, activation="relu")(x)
    x = layers.Dropout(0.4)(x)
    outputs = layers.Dense(len(CLASS_NAMES), activation="softmax", name="predictions")(x)
    return keras.Model(inputs, outputs), base


# ─── 4) Main ──────────────────────────────────────────────────────────────────
def main():
    global CAPTURAS_DIR, LOTES_PALTA, LOTES_NO_PALTA

    ap = argparse.ArgumentParser(description="Entrena el modelo binario palta/no_palta")
    ap.add_argument("--capturas", default=CAPTURAS_DIR, help="carpeta raíz de capturas")
    ap.add_argument("--palta", help="lotes PALTA separados por coma, ej: 10,11")
    ap.add_argument("--no-palta", dest="no_palta", help="lotes NO_PALTA, ej: 13,14")
    args = ap.parse_args()

    CAPTURAS_DIR = args.capturas
    if args.palta:
        LOTES_PALTA = [int(x) for x in args.palta.split(",") if x.strip()]
    if args.no_palta:
        LOTES_NO_PALTA = [int(x) for x in args.no_palta.split(",") if x.strip()]

    print(f"Capturas : {CAPTURAS_DIR}")
    print(f"PALTA    : lotes {LOTES_PALTA}")
    print(f"NO_PALTA : lotes {LOTES_NO_PALTA}")

    # Recolectar + split POR GRUPO (palta) en cada clase
    g_palta   = recolectar(LOTES_PALTA, 1)
    g_nopalta = recolectar(LOTES_NO_PALTA, 0)
    if not g_palta or not g_nopalta:
        raise SystemExit("[ERROR] Falta(n) fotos: revisa los números de lote y la ruta de capturas.")

    p_tr, p_va = split_por_grupo(g_palta,   VAL_SPLIT, SEED)
    n_tr, n_va = split_por_grupo(g_nopalta, VAL_SPLIT, SEED)

    train = [(f, 1) for f in p_tr] + [(f, 0) for f in n_tr]
    val   = [(f, 1) for f in p_va] + [(f, 0) for f in n_va]
    random.Random(SEED).shuffle(train)

    print(f"\nGrupos (frutas): palta={len(g_palta)}  no_palta={len(g_nopalta)}")
    print(f"Fotos train={len(train)}  val={len(val)}")
    print(f"  train por clase: {Counter(l for _, l in train)}")
    print(f"  val   por clase: {Counter(l for _, l in val)}")
    if not val:
        print("[aviso] val vacío (pocas frutas): no habrá matriz de confusión fiable.")

    train_ds = make_ds(train, training=True)
    val_ds   = make_ds(val,   training=False) if val else None

    # class_weight para balancear (suele haber más de una clase que de otra)
    cuenta = Counter(l for _, l in train)
    total  = sum(cuenta.values())
    class_weight = {i: total / (len(CLASS_NAMES) * cuenta.get(i, 1)) for i in range(len(CLASS_NAMES))}
    print(f"class_weight: {class_weight}")

    model, base = construir_modelo()

    # EarlyStopping: corta si val_accuracy deja de mejorar y restaura el mejor peso.
    # Solo con set de validación (si no hay val, entrena sin callback).
    cbs = []
    if val_ds is not None:
        cbs.append(keras.callbacks.EarlyStopping(
            monitor="val_accuracy", mode="max", patience=6, restore_best_weights=True))

    # Fase 1: cabeza
    model.compile(optimizer=keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    print("\n=== Fase 1: entrenando la cabeza (base congelada) ===")
    model.fit(train_ds, validation_data=val_ds, epochs=EPOCHS,
              class_weight=class_weight, callbacks=cbs)

    # Fase 2: fine-tuning de las últimas ~30 capas
    base.trainable = True
    for capa in base.layers[:-30]:
        capa.trainable = False
    model.compile(optimizer=keras.optimizers.Adam(1e-5),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    print("\n=== Fase 2: fine-tuning (últimas ~30 capas) ===")
    model.fit(train_ds, validation_data=val_ds, epochs=FINE_EPOCHS,
              class_weight=class_weight, callbacks=cbs)

    # ── Matriz de confusión en val (sin sklearn) ──
    if val_ds is not None:
        y_true = np.array([l for _, l in val])
        probs  = model.predict(val_ds, verbose=0)
        y_pred = np.argmax(probs, axis=1)
        cm = np.zeros((2, 2), dtype=int)
        for t, p in zip(y_true, y_pred):
            cm[t][p] += 1
        acc = float(np.trace(cm)) / max(int(cm.sum()), 1)
        print("\nMatriz de confusión (val)   filas=real, cols=predicho")
        print(f"             pred:no_palta  pred:palta")
        print(f"real:no_palta     {cm[0,0]:>6}      {cm[0,1]:>6}")
        print(f"real:palta        {cm[1,0]:>6}      {cm[1,1]:>6}")
        print(f"accuracy (val) = {acc*100:.1f}%")
        if acc >= 0.999:
            print("\n[AVISO] accuracy = 100% -> sospecha de dataset DEMASIADO fácil o fuga de")
            print("        datos (poca variedad de frutas). No es señal de modelo robusto:")
            print("        junta más NO-paltas variadas y fotos en distintos ángulos/luz.")

    # ── Guardar modelo + orden de clases ──
    os.makedirs(os.path.dirname(SALIDA), exist_ok=True)
    model.save(SALIDA)
    with open(SALIDA_JSON, "w", encoding="utf-8") as f:
        json.dump(CLASS_NAMES, f, ensure_ascii=False, indent=2)
    print(f"\n[OK] Modelo guardado en {SALIDA}")
    print(f"[OK] Clases guardadas en {SALIDA_JSON}: {CLASS_NAMES}")
    print("     El backend (inference.py) ya usa BINARY_CLASS_NAMES = ['no_palta','palta'].")


if __name__ == "__main__":
    main()
