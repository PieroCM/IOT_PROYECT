"""
PaltaCheck · Paso 1 — Preprocesado del dataset
================================================
Transforma el dataset "de estudio" (paltas nítidas sobre fondo blanco) en un
dataset que SE PARECE a lo que ve tu cámara ESP32-CAM (baja resolución, borroso,
desaturado, con el fondo de tu cinta). Así el modelo entrena en el mismo dominio
en el que va a trabajar.

Pipeline por imagen:
    1. Leer + etiqueta desde el CSV (Healthy / Scab / Anthracnose)
    2. Segmentar la palta (quitar el fondo blanco) -> máscara
    3. Reemplazar el fondo según config.BACKGROUND_MODE
    4. Recorte cuadrado centrado en la palta
    5. Degradación tipo cámara OV2640 (resize bajo, blur, desaturación, tinte,
       ruido, compresión JPEG agresiva)
    6. Guardar en data/processed/{train,val}/{sana,antracnosis}/

Uso:
    python preprocess.py
    python preprocess.py --limit 200      # prueba rápida con 200 imágenes
"""
import argparse
import random
import shutil
from pathlib import Path

import cv2
import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from tqdm import tqdm

import config as C


# ── 1. Segmentación: separar la palta del fondo blanco ───────────────────────
def mascara_palta(img):
    """Devuelve una máscara binaria (255 = palta) separando del fondo claro.
    El dataset tiene fondo casi blanco, así que segmentamos por 'no-blanco'
    en HSV (saturación alta o valor bajo) + limpieza morfológica."""
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    s, v = hsv[..., 1], hsv[..., 2]
    # fondo = muy poco saturado Y muy brillante -> lo demás es palta
    fondo = (s < 25) & (v > 200)
    m = np.where(fondo, 0, 255).astype(np.uint8)
    # limpiar ruido y rellenar huecos
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, k)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, k, iterations=3)
    # quedarnos con el componente conexo más grande (la palta)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(m)
    if n > 1:
        mayor = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
        m = np.where(lab == mayor, 255, 0).astype(np.uint8)
    return m


def bbox_de_mascara(m, pad=0.12):
    ys, xs = np.where(m > 0)
    if len(xs) == 0:
        h, w = m.shape
        return 0, 0, w, h
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = x1 - x0, y1 - y0
    px, py = int(w * pad), int(h * pad)
    return max(0, x0 - px), max(0, y0 - py), \
           min(m.shape[1], x1 + px), min(m.shape[0], y1 + py)


# ── 2. Fondos realistas tomados de tus capturas del ESP32 ────────────────────
def cargar_parches_fondo():
    """Extrae parches de las capturas reales para usarlos como fondo."""
    parches = []
    for f in list(C.CAPTURAS_DIR.rglob("*.jpg"))[:400]:
        im = cv2.imread(str(f))
        if im is not None:
            parches.append(im)
    return parches


def poner_fondo(img, m, modo, parches):
    """Reemplaza el fondo blanco por el fondo elegido, usando la máscara."""
    if modo == "keep":
        return img
    h, w = img.shape[:2]
    if modo == "black":
        fondo = np.zeros_like(img)
    elif modo == "random":
        base = np.random.randint(60, 200, (1, 1, 3), np.uint8)
        fondo = np.full_like(img, base)
        ruido = np.random.normal(0, 20, img.shape).astype(np.float32)
        fondo = np.clip(fondo.astype(np.float32) + ruido, 0, 255).astype(np.uint8)
    elif modo == "capturas" and parches:
        p = random.choice(parches)
        fondo = cv2.resize(p, (w, h))
    else:
        fondo = np.zeros_like(img)
    m3 = cv2.cvtColor(m, cv2.COLOR_GRAY2BGR) > 0
    return np.where(m3, img, fondo)


# ── 3. Degradación tipo cámara OV2640 del ESP32 ──────────────────────────────
def degradar_camara(img):
    size = C.CAM_SIZE
    h, w = img.shape[:2]
    mm = min(h, w)
    img = img[(h - mm) // 2:(h - mm) // 2 + mm, (w - mm) // 2:(w - mm) // 2 + mm]
    # baja resolución real: encoge y vuelve a ampliar (pierde detalle)
    small = cv2.resize(img, (size // 2, size // 2), interpolation=cv2.INTER_AREA)
    img = cv2.resize(small, (size, size), interpolation=cv2.INTER_LINEAR)
    # desenfoque (lente blanda)
    k = random.choice(C.BLUR_KERNELS)
    img = cv2.GaussianBlur(img, (k, k), 0)
    # baja saturación + tinte (mal balance de blancos)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[..., 1] *= random.uniform(*C.SATURATION)
    hsv[..., 2] *= random.uniform(0.85, 1.05)
    img = cv2.cvtColor(np.clip(hsv, 0, 255).astype(np.uint8), cv2.COLOR_HSV2BGR)
    b, g, r = cv2.split(img.astype(np.float32))
    g *= random.uniform(1.0, 1.12)
    b *= random.uniform(0.9, 1.05)
    img = cv2.merge([np.clip(b, 0, 255), np.clip(g, 0, 255),
                     np.clip(r, 0, 255)]).astype(np.uint8)
    # ruido de sensor
    n = np.random.normal(0, random.uniform(4, 9), img.shape).astype(np.float32)
    img = np.clip(img.astype(np.float32) + n, 0, 255).astype(np.uint8)
    # compresión JPEG agresiva (como jpeg_quality=12 del firmware)
    q = random.randint(*C.JPEG_QUALITY)
    ok, enc = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, q])
    return cv2.imdecode(enc, cv2.IMREAD_COLOR)


# ── 4. Etiquetas: mapear las 3 condiciones a las 2 clases finales ────────────
def etiqueta_final(condition):
    if condition == "Healthy":
        return "sana"
    if condition == "Anthracnose":
        return "antracnosis"
    if condition == "Scab":
        if C.CLASS_MODE == "3clases":
            return "scab"
        return None if C.SCAB_POLICY == "drop" else "antracnosis"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0, help="procesa solo N imágenes (prueba)")
    args = ap.parse_args()
    random.seed(C.SEED)
    np.random.seed(C.SEED)

    csv_path = C.RAW_DIR / C.CSV_NAME
    img_dir = C.RAW_DIR / C.IMG_SUBDIR
    assert csv_path.exists(), f"No encuentro el CSV en {csv_path}"
    assert img_dir.exists(), f"No encuentro las imágenes en {img_dir}"

    df = pd.read_csv(csv_path)
    df["clase"] = df["Condition"].map(etiqueta_final)
    df = df.dropna(subset=["clase"]).reset_index(drop=True)
    if args.limit:
        df = df.sample(min(args.limit, len(df)), random_state=C.SEED).reset_index(drop=True)

    print(f"Imágenes a procesar: {len(df)}")
    print(df["clase"].value_counts().to_string())
    print(f"Fondo: {C.BACKGROUND_MODE} | Scab: {C.SCAB_POLICY}")

    parches = cargar_parches_fondo() if C.BACKGROUND_MODE == "capturas" else []
    if C.BACKGROUND_MODE == "capturas" and not parches:
        print("[!] No hay capturas en", C.CAPTURAS_DIR, "-> uso fondo 'random'")

    # split estratificado train/val
    tr, va = train_test_split(df, test_size=C.VAL_SPLIT, stratify=df["clase"],
                              random_state=C.SEED)
    splits = {"train": tr, "val": va}

    if C.PROC_DIR.exists():
        shutil.rmtree(C.PROC_DIR)
    for sp in splits:
        for cl in C.CLASSES:
            (C.PROC_DIR / sp / cl).mkdir(parents=True, exist_ok=True)

    for sp, sub in splits.items():
        for _, row in tqdm(sub.iterrows(), total=len(sub), desc=sp):
            src = img_dir / f"{row['Identification']}.jpg"
            img = cv2.imread(str(src))
            if img is None:
                continue
            m = mascara_palta(img)
            img = poner_fondo(img, m, C.BACKGROUND_MODE, parches)
            x0, y0, x1, y1 = bbox_de_mascara(m)
            img = img[y0:y1, x0:x1]
            if img.size == 0:
                continue
            img = degradar_camara(img)
            dst = C.PROC_DIR / sp / row["clase"] / f"{row['Identification']}.jpg"
            cv2.imwrite(str(dst), img)

    print("\nListo. Dataset procesado en:", C.PROC_DIR.resolve())
    for sp in splits:
        for cl in C.CLASSES:
            n = len(list((C.PROC_DIR / sp / cl).glob("*.jpg")))
            print(f"  {sp}/{cl}: {n}")


if __name__ == "__main__":
    main()
