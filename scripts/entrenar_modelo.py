"""
PaltaCheck — Entrenamiento / fine-tuning del modelo de enfermedades
==================================================================
Reentrena un MobileNetV2 con TUS fotos del ESP32 (resuelve el "domain shift":
el modelo viejo no había visto fotos de tu cámara/luz/fondo).

ESTRUCTURA DE CARPETAS esperada (cada clase = una carpeta):

    dataset/
        healthy/      <- fotos de paltas sanas
        anthracnose/  <- fotos con antracnosis
        scab/         <- fotos con scab (roña)   [opcional]

    dataset_test/     <- (opcional pero RECOMENDADO) paltas ENTERAS que NO están
        healthy/         en dataset/. Sirve para medir de verdad (sin fuga).
        anthracnose/
        scab/

IMPORTANTE (fuga de datos): NO mezcles fotos de la MISMA palta en train y test.
Mete todas las fotos de una palta en una sola carpeta/lado.

El modelo lleva el preprocesamiento ADENTRO (preprocess_input de MobileNetV2),
igual que tu modelo actual -> el backend (inference.py) sigue funcionando sin
cambios, solo pásale las fotos crudas 0-255.

Requisitos:  pip install tensorflow pillow
Ejecutar:    python scripts/entrenar_modelo.py
Salida:      models/modelo_paltas.keras  (+ imprime el ORDEN de clases)
"""
import os
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# ─── Parámetros ───────────────────────────────────────────────────────────────
IMG          = 224          # MobileNetV2 usa 224x224
BATCH        = 16
EPOCHS       = 15           # 1ra fase: solo la "cabeza"
FINE_EPOCHS  = 10           # 2da fase: fine-tuning de la base
DATA_DIR     = "dataset"
TEST_DIR     = "dataset_test"   # si no existe, usa un split de validación normal
SALIDA       = "models/modelo_paltas.keras"
SEED         = 123

# ─── 1) Cargar datos ──────────────────────────────────────────────────────────
train_ds = keras.utils.image_dataset_from_directory(
    DATA_DIR, validation_split=0.2, subset="training",
    seed=SEED, image_size=(IMG, IMG), batch_size=BATCH, label_mode="int")
val_ds = keras.utils.image_dataset_from_directory(
    DATA_DIR, validation_split=0.2, subset="validation",
    seed=SEED, image_size=(IMG, IMG), batch_size=BATCH, label_mode="int")

class_names = train_ds.class_names
print("\n=========================================")
print(" ORDEN DE CLASES (cópialo a inference.py):")
print("  CLASS_NAMES =", class_names)
print("=========================================\n")

AUTOTUNE = tf.data.AUTOTUNE
train_ds = train_ds.cache().shuffle(1000).prefetch(AUTOTUNE)
val_ds   = val_ds.cache().prefetch(AUTOTUNE)

# ─── 2) Aumento de datos (clave con pocas fotos) ──────────────────────────────
data_augmentation = keras.Sequential([
    layers.RandomFlip("horizontal"),
    layers.RandomRotation(0.15),
    layers.RandomZoom(0.15),
    layers.RandomBrightness(0.20),
    layers.RandomContrast(0.20),
], name="data_augmentation")

# ─── 3) Modelo (MobileNetV2 con preprocesamiento ADENTRO) ─────────────────────
base = keras.applications.MobileNetV2(
    input_shape=(IMG, IMG, 3), include_top=False, weights="imagenet")
base.trainable = False   # 1ra fase: base congelada

inputs = keras.Input(shape=(IMG, IMG, 3))
x = data_augmentation(inputs)
x = keras.applications.mobilenet_v2.preprocess_input(x)   # escala a [-1,1] DENTRO del modelo
x = base(x, training=False)
x = layers.GlobalAveragePooling2D()(x)
x = layers.Dropout(0.3)(x)
x = layers.Dense(128, activation="relu")(x)
x = layers.Dropout(0.3)(x)
outputs = layers.Dense(len(class_names), activation="softmax", name="predictions")(x)
model = keras.Model(inputs, outputs)

# Pesos por clase (si hay desbalance, p.ej. más antracnosis que sanas)
import numpy as np
from collections import Counter
labels = []
for _, y in keras.utils.image_dataset_from_directory(
        DATA_DIR, image_size=(IMG, IMG), batch_size=BATCH, label_mode="int", shuffle=False):
    labels.extend(y.numpy().tolist())
cuenta = Counter(labels)
total = sum(cuenta.values())
class_weight = {i: total / (len(class_names) * cuenta.get(i, 1)) for i in range(len(class_names))}
print("Fotos por clase:", {class_names[i]: cuenta.get(i, 0) for i in range(len(class_names))})
print("class_weight:", class_weight)

# ─── 4) Fase 1: entrenar la cabeza ────────────────────────────────────────────
model.compile(optimizer=keras.optimizers.Adam(1e-3),
              loss="sparse_categorical_crossentropy", metrics=["accuracy"])
print("\n=== Fase 1: entrenando la cabeza ===")
model.fit(train_ds, validation_data=val_ds, epochs=EPOCHS, class_weight=class_weight)

# ─── 5) Fase 2: fine-tuning de la parte alta de la base ───────────────────────
base.trainable = True
for capa in base.layers[:-30]:      # descongela solo las últimas ~30 capas
    capa.trainable = False
model.compile(optimizer=keras.optimizers.Adam(1e-5),   # LR MUY bajo para no romper lo aprendido
              loss="sparse_categorical_crossentropy", metrics=["accuracy"])
print("\n=== Fase 2: fine-tuning ===")
model.fit(train_ds, validation_data=val_ds, epochs=FINE_EPOCHS, class_weight=class_weight)

# ─── 6) Evaluación honesta con paltas NO vistas (si existe dataset_test/) ──────
if os.path.isdir(TEST_DIR):
    test_ds = keras.utils.image_dataset_from_directory(
        TEST_DIR, image_size=(IMG, IMG), batch_size=BATCH, label_mode="int", shuffle=False)
    loss, acc = model.evaluate(test_ds)
    print(f"\n[TEST con paltas nuevas] accuracy = {acc*100:.1f}%")
else:
    print("\n(Sin dataset_test/: la precisión real puede ser optimista por fuga de datos.)")

# ─── 7) Guardar ───────────────────────────────────────────────────────────────
os.makedirs("models", exist_ok=True)
model.save(SALIDA)
print(f"\n[OK] Modelo guardado en {SALIDA}")
print(f"     Orden de clases: {class_names}")
print("     -> Actualiza CLASS_NAMES en backend/inference.py con ESE orden.")
