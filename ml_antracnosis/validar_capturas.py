"""
PaltaCheck · Validar los 3 modelos sobre capturas REALES del ESP32
==================================================================
Corre mobilenet_v2 / resnet50 / efficientnet_b0 sobre las capturas reales de
data/capturas/ (sin degradación: la imagen ya viene de la cámara) y reporta, por
modelo, la distribución de predicciones + confianza media + veredicto de colapso.

OJO: las capturas NO tienen etiqueta -> esto es análisis DISTRIBUCIONAL, no
accuracy. Reutiliza infer.cargar + infer.predecir (mismo preprocesado que el backend).

Uso (desde ml_antracnosis/, con el venv):
    .\.venv\Scripts\python.exe validar_capturas.py
    .\.venv\Scripts\python.exe validar_capturas.py --limit 60   # prueba rápida
"""
import argparse
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import config as C
import infer

ARCHS = ["mobilenet_v2", "resnet50", "efficientnet_b0"]
ABBR = {"mobilenet_v2": "MN", "resnet50": "RN", "efficientnet_b0": "EN"}
COLAPSO_UMBRAL = 0.85
SCRATCH = Path(r"C:\Users\marks\AppData\Local\Temp\claude\c--Users-marks-Documents-Piero-IOT\cc0c81a4-c19d-43dc-9f84-9b9a1816092a\scratchpad")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0, help="usa solo N capturas (prueba)")
    args = ap.parse_args()

    caps = sorted(C.CAPTURAS_DIR.rglob("*.jpg"))
    if args.limit:
        idx = np.linspace(0, len(caps) - 1, min(args.limit, len(caps))).astype(int)
        caps = [caps[i] for i in idx]
    print(f"Capturas reales a evaluar: {len(caps)}  (de {C.CAPTURAS_DIR})\n")

    # cargar los 3 modelos una vez
    modelos = {}
    for arch in ARCHS:
        ck = C.OUT_DIR / arch / "mejor_modelo.pt"
        modelos[arch] = infer.cargar(ck)   # (modelo, classes, img_size)
    classes = modelos[ARCHS[0]][1]         # ['antracnosis','sana','scab']

    # predecir: cada imagen se abre UNA vez y pasa por los 3 modelos
    res = {a: {"preds": [], "confs": [], "probs": []} for a in ARCHS}
    per_img = []
    for k, p in enumerate(caps):
        img = Image.open(p).convert("RGB")
        fila = {"path": p, "pred": {}}
        for arch, (m, cls, isz) in modelos.items():
            clase, conf, allp = infer.predecir(m, cls, isz, img)
            res[arch]["preds"].append(clase)
            res[arch]["confs"].append(conf)
            res[arch]["probs"].append([allp[c] for c in classes])
            fila["pred"][arch] = (clase, conf)
        per_img.append(fila)
        if (k + 1) % 100 == 0:
            print(f"  ...{k+1}/{len(caps)}")

    # ── Reporte por modelo ───────────────────────────────────────────────────
    print("\n" + "=" * 68)
    print("DISTRIBUCIÓN DE PREDICCIONES · CONFIANZA · COLAPSO")
    print("=" * 68)
    n = len(caps)
    for arch in ARCHS:
        preds = res[arch]["preds"]
        confs = np.array(res[arch]["confs"])
        probs = np.array(res[arch]["probs"])
        cnt = Counter(preds)
        top_cls, top_n = cnt.most_common(1)[0]
        colapsa = top_n / n >= COLAPSO_UMBRAL
        print(f"\n[{arch}]  conf media global = {confs.mean():.3f}   "
              f"{'*** COLAPSA ***' if colapsa else 'reparte OK'}")
        for c in classes:
            m = [i for i, pr in enumerate(preds) if pr == c]
            cm = confs[m].mean() if m else 0.0
            pmean = probs[:, classes.index(c)].mean()
            print(f"    {c:12s}: {cnt.get(c,0):4d} ({cnt.get(c,0)/n*100:5.1f}%)   "
                  f"conf_media={cm:.3f}   P_media={pmean:.3f}")

    # ── Acuerdo entre modelos ────────────────────────────────────────────────
    agree3 = sum(1 for f in per_img
                 if len({f["pred"][a][0] for a in ARCHS}) == 1)
    print("\n" + "=" * 68)
    print(f"ACUERDO: los 3 modelos coinciden en {agree3}/{n} ({agree3/n*100:.1f}%)")
    # cruce MobileNet vs EfficientNet
    print("\nCruce MobileNet(fila) vs EfficientNet(col):")
    print("            " + "".join(f"{c[:6]:>9}" for c in classes))
    for cr in classes:
        row = []
        for cc in classes:
            k = sum(1 for f in per_img
                    if f["pred"]["mobilenet_v2"][0] == cr
                    and f["pred"]["efficientnet_b0"][0] == cc)
            row.append(f"{k:>9}")
        print(f"    {cr[:8]:8s}" + "".join(row))

    # ── Montaje visual (15 capturas anotadas) ────────────────────────────────
    sel = np.linspace(0, n - 1, min(15, n)).astype(int)
    fig, axes = plt.subplots(3, 5, figsize=(15, 10))
    fig.suptitle("Capturas reales ESP32 — predicción de cada modelo (MN=MobileNet, RN=ResNet, EN=EfficientNet)",
                 fontsize=12, weight="bold")
    for ax, si in zip(axes.ravel(), sel):
        f = per_img[si]
        ax.imshow(Image.open(f["path"]).convert("RGB"))
        txt = "\n".join(f"{ABBR[a]}: {f['pred'][a][0][:6]} {f['pred'][a][1]:.2f}" for a in ARCHS)
        ax.set_title(f["path"].parent.name, fontsize=7)
        ax.text(0.02, 0.98, txt, transform=ax.transAxes, fontsize=7, va="top",
                bbox=dict(boxstyle="round", fc="white", alpha=0.7))
        ax.axis("off")
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = SCRATCH / "validacion_capturas.png"
    fig.savefig(out, dpi=100)
    print(f"\nMontaje guardado: {out}")


if __name__ == "__main__":
    main()
