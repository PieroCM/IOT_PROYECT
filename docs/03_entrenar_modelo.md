# 03 · Cómo hacer que el modelo reconozca bien tus paltas

El modelo viejo marca todo como antracnosis por **"domain shift"**: fue entrenado
con imágenes distintas a las de tu cámara (otra luz, fondo, escala). La solución
no es tocar el escalado, es **reentrenar (fine-tuning) con fotos de TU ESP32**.

---

## 1. La idea (técnicas que aplicamos)

- **Transfer learning + fine-tuning:** partimos de MobileNetV2 pre-entrenado y le
  cambiamos/reentrenamos la "cabeza" con tus fotos. No se entrena desde cero.
- **Aumento de datos** (flip, rotación, brillo, zoom, contraste): multiplica tus
  pocas fotos y evita el sobreajuste. Es lo que más ayuda con datasets chicos.
- **Preprocesamiento/escalado:** va **dentro del modelo** (`preprocess_input` de
  MobileNetV2, que lleva los píxeles a [-1, 1]). Así, en el backend solo mandas la
  foto cruda 0–255 y el modelo la escala solo — `inference.py` no cambia.
- **Balance de clases** (`class_weight`): si tienes más fotos de una clase que de
  otra, el entrenamiento las compensa.

---

## 2. Cuántas fotos necesitas (realista)

- Lo ideal: **~100 fotos por clase**. Mínimo decente para una demo: **30–50 por clase**.
- **Necesitas las dos clases sí o sí:** con solo 3 paltas con antracnosis NO se puede
  entrenar — el modelo nunca vería una palta **sana**. Consigue también **paltas sanas**
  (y de scab si quieres esa clase).
- Mejor **varias paltas distintas por clase** (no una sola fotografiada 100 veces):
  el modelo aprende la enfermedad, no esa palta puntual. Apunta a **5–10 paltas por clase**.

> Tu flujo ya ayuda: por cada palta giras 3 veces y se guardan fotos en
> `backend/capturas/lote_X/`. Pasa varias paltas y tendrás muchas fotos rápido.

---

## 3. Recolectar el dataset (con tu propio ESP32)

1. Pon buena **luz pareja** y un **fondo neutro** (la cámara fija a ~10–15 cm, enfocada).
2. Corre el flujo y pasa cada palta (gira y toma fotos). Se guardan en `backend/capturas/`.
3. Organiza las fotos en carpetas por clase:

```
dataset/
    healthy/        <- fotos de paltas sanas
    anthracnose/    <- fotos con antracnosis
    scab/           <- fotos con scab (opcional)
```

4. (Recomendado) Aparta **paltas enteras** para test honesto:

```
dataset_test/
    healthy/
    anthracnose/
    scab/
```

> ⚠️ **Evita la fuga de datos:** TODAS las fotos de una misma palta van a un solo
> lado (train **o** test), nunca repartidas. Si no, la precisión sale inflada y falsa.

---

## 4. Entrenar

En la carpeta del proyecto (con las carpetas `dataset/` listas):

```bash
pip install tensorflow pillow
python scripts/entrenar_modelo.py
```

El script:
1. Carga las fotos y hace split 80/20.
2. Aumenta datos + entrena la cabeza (fase 1).
3. Hace fine-tuning de la parte alta de MobileNetV2 (fase 2).
4. Evalúa con `dataset_test/` si existe.
5. Guarda **`models/modelo_paltas.keras`** e imprime el **orden de clases**.

---

## 5. Conectar el modelo nuevo al backend

1. Copia el orden de clases que imprimió el script (ej. `['anthracnose','healthy','scab']`)
   a `backend/inference.py` -> `CLASS_NAMES = [...]` (¡en ESE orden exacto!).
2. Apunta el backend al modelo nuevo: en `docker-compose.yml`,
   `MODEL_PATH: /app/models/modelo_paltas.keras`.
3. Reconstruye: `docker compose up -d --build backend`.
4. Prueba con `curl` una foto conocida y mira las `probabilidades` en la respuesta.

---

## 6. Si aún falla

- Revisa que **healthy** salga con prob alta en una palta claramente sana.
- Más fotos y más variedad de paltas = mejor. Sube `EPOCHS`/`FINE_EPOCHS` con cuidado.
- Mantén las fotos de entrenamiento **parecidas a las de producción** (misma luz,
  distancia y fondo que usará la máquina).
