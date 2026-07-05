"""
============================================================================
 PaltaCheck - scripts/entrenar_enfermedad.py   (SALUD vs ANTRACNOSIS)
 ----------------------------------------------------------------------------
 Entrena un MobileNetV2 BINARIO de enfermedad usando las fotos reales del
 ESP32 del lote de PALTAS (backend/capturas/lote_<N>/palta_<id>_*.jpg).

 Las etiquetas por palta ya vienen abajo (revisadas foto por foto). Las paltas
 con manchas negras notables = ANTRACNOSIS; el resto = SALUD.

 Convencion (igual que inference.py / entrenar_binario.py):
   - preprocess_input va ADENTRO del modelo -> el backend le pasa pixeles
     CRUDOS 0-255. Se enchufa sin tocar el preprocesamiento.
 Anti-fuga: split train/val POR PALTA (todas las fotos de una fruta a un lado).

 IMPORTANTE (leelo): hoy hay MUY pocas antracnosis y de baja confianza. Para
 un modelo util, pasa 8-15 paltas CLARAMENTE enfermas (manchas negras hundidas)
 en un lote nuevo, agrega ese numero de lote en LOTES_EXTRA y sus ids en
 ANTRACNOSIS, y reentrena.

 USO (desde la raiz del repo):
     pip install tensorflow pillow
     python scripts/entrenar_enfermedad.py
 Salida:
     models/modelo_enfermedad.keras
     models/modelo_enfermedad_classes.json   -> ["salud","antracnosis"]
============================================================================
"""
import re
import json
import random
from pathlib import Path
from collections import defaultdict

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── ETIQUETAS (revisadas por imagen en el lote_23) ──────────────────────────
LOTE_PALTAS = 23                 # lote cuyas fotos son PALTAS
# Paltas con manchas negras notables -> ANTRACNOSIS
ANTRACNOSIS = [207, 208, 217, 218, 220, 222]
# Paltas a excluir (sin fruta en cuadro / basura)
EXCLUIR     = [228, -1]
# El resto de paltas del lote se toma como SALUD automaticamente.

# Si consigues mas paltas enfermas en otros lotes, agrega aqui:
#   LOTES_EXTRA = {24: {"antracnosis": [..], "salud": [..]}}
LOTES_EXTRA = {}

# ─── Rutas robustas ──────────────────────────────────────────────────────────
REPO = Path(__file__).resolve().parent
if REPO.name == "scripts":
    REPO = REPO.parent
CAPTURAS_DIR = REPO / "backend" / "capturas"
OUT_MODEL    = REPO / "models" / "modelo_enfermedad.keras"
OUT_CLASSES  = REPO / "models" / "modelo_enfermedad_classes.json"

# ─── Config ──────────────────────────────────────────────────────────────────
CLASES       = ["salud", "antracnosis"]   # 0 = salud, 1 = antracnosis
IMG          = 224
BATCH        = 16
EPOCHS_HEAD  = 15
EPOCHS_FINE  = 10
VAL_FRACCION = 0.2
SEED         = 123

random.seed(SEED); np.random.seed(SEED); tf.random.set_seed(SEED)
_RE = re.compile(r"palta_(-?\w+?)_\d", re.IGNORECASE)


def _label_de(pid_int):
    return 1 if pid_int in ANTRACNOSIS else 0


def recolectar():
    """(ruta, label, grupo) desde capturas/lote_<N>, agrupando por palta."""
    items = []
    # lote principal (etiquetas por defecto: antracnosis lista, resto salud)
    carpeta = CAPTURAS_DIR / f"lote_{LOTE_PALTAS}"
    for img in sorted(carpeta.glob("*.jpg")):
        m = _RE.search(img.name)
        pid = m.group(1) if m else img.stem
        try: pid_int = int(pid)
        except ValueError: pid_int = -999
        if pid_int in EXCLUIR:
            continue
        items.append((str(img), _label_de(pid_int), f"L{LOTE_PALTAS}_p{pid}"))
    # lotes extra con etiquetas explicitas
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


def construir():
    # Augmentation MAS fuerte (dataset chico) pero SIN romper el color, porque la
    # mancha oscura de la antracnosis es una senal cromatica clave.
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
    inputs = keras.Input(shape=(IMG, IMG, 3))          # pixeles 0-255
    x = aug(inputs)
    x = keras.applications.mobilenet_v2.preprocess_input(x)   # [-1,1] adentro
    x = base(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.4)(x)
    out = layers.Dense(len(CLASES), activation="softmax", name="predictions")(x)
    return keras.Model(inputs, out), base


def main():
    items = recolectar()
    n_ant = sum(1 for i in items if i[1] == 1)
    n_sal = sum(1 for i in items if i[1] == 0)
    print(f"Fotos: {len(items)}  (salud={n_sal}, antracnosis={n_ant})")
    if n_ant == 0 or n_sal == 0:
        print("[X] Falta una de las dos clases. Revisa ANTRACNOSIS/EXCLUIR."); return
    if n_ant < 9:
        print(f"[!] Solo {n_ant} fotos de antracnosis: el modelo saldra DEBIL. "
              "Junta paltas claramente enfermas y agregalas antes de confiar en el.")

    train, val = split_por_palta(items)
    print(f"train: {len(train)} ({len({i[2] for i in train})} paltas) | "
          f"val: {len(val)} ({len({i[2] for i in val})} paltas)")
    if not val: print("[X] Sin val (necesitas >=2 paltas por clase)."); return

    train_ds, val_ds = hacer_ds(train, True), hacer_ds(val, False)
    t0 = sum(1 for i in train if i[1] == 0); t1 = sum(1 for i in train if i[1] == 1)
    tot = t0 + t1
    pesos = {0: tot/(2*t0), 1: tot/(2*t1)} if t0 and t1 else None
    print("Fotos train por clase:", {"salud": t0, "antracnosis": t1}, "pesos:", pesos)

    OUT_MODEL.parent.mkdir(parents=True, exist_ok=True)
    cbs = [keras.callbacks.ModelCheckpoint(str(OUT_MODEL), monitor="val_accuracy",
                                           mode="max", save_best_only=True),
           keras.callbacks.EarlyStopping(monitor="val_accuracy", mode="max",
                                         patience=6, restore_best_weights=True)]
    modelo, base = construir()

    print("\n=== FASE 1: cabeza (base congelada) ===")
    modelo.compile(optimizer=keras.optimizers.Adam(1e-3),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_HEAD,
               class_weight=pesos, callbacks=cbs)

    print("\n=== FASE 2: fine-tuning (ultimas 30 capas) ===")
    base.trainable = True
    for capa in base.layers[:-30]:
        capa.trainable = False
    modelo.compile(optimizer=keras.optimizers.Adam(1e-5),
                   loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    modelo.fit(train_ds, validation_data=val_ds, epochs=EPOCHS_FINE,
               class_weight=pesos, callbacks=cbs)

    print("\n=== Evaluacion (val con paltas NO vistas) ===")
    modelo = keras.models.load_model(str(OUT_MODEL))
    y_true, y_pred = [], []
    for xb, yb in val_ds:
        p = modelo.predict(xb, verbose=0)
        y_pred.extend(np.argmax(p, axis=1).tolist()); y_true.extend(yb.numpy().tolist())
    y_true, y_pred = np.array(y_true), np.array(y_pred)
    cm = np.zeros((2, 2), int)
    for t, p in zip(y_true, y_pred): cm[t][p] += 1
    print("  Matriz (filas=real, cols=pred)  [0=salud,1=antracnosis]")
    print(f"    real salud       -> pred: salud {cm[0,0]}  antrac {cm[0,1]}")
    print(f"    real antracnosis -> pred: salud {cm[1,0]}  antrac {cm[1,1]}")
    acc = np.trace(cm) / max(cm.sum(), 1)
    print(f"  accuracy(val) = {acc*100:.1f}%")

    OUT_CLASSES.write_text(json.dumps(CLASES, indent=2))
    print(f"\n[OK] Guardado:\n     {OUT_MODEL}\n     {OUT_CLASSES}\n     clases: {CLASES}")
    print("     Mapeo a la BD: salud -> 'sana' ; antracnosis -> 'antracnosis'.")


if __name__ == "__main__":
    main()
