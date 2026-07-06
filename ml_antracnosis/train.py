"""
PaltaCheck · Paso 2 — Entrenamiento del clasificador (PyTorch + CUDA)
=====================================================================
Transfer learning sobre ImageNet en dos fases:
    Fase 1 (warm-up): backbone congelado, solo entrena la cabeza nueva.
    Fase 2 (fine-tuning): descongela todo con learning rate bajo.

Optimizado para una RTX 4060 (8 GB). Usa AMP (mixed precision) para ir más
rápido y gastar menos VRAM. Guarda el mejor modelo por F1 de validación, la
curva de entrenamiento y la matriz de confusión.

Uso:
    python train.py
    python train.py --arch resnet50          # probar otra arquitectura
    python train.py --epochs-fine 25
"""
import argparse
import json
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torchvision import datasets, transforms, models
from sklearn.metrics import (classification_report, confusion_matrix,
                             f1_score)
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import config as C


# ── Modelo: elige arquitectura y reemplaza la cabeza por 2 clases ────────────
def construir_modelo(arch, n_clases):
    arch = arch.lower()
    if arch == "mobilenet_v2":
        m = models.mobilenet_v2(weights="IMAGENET1K_V1")
        m.classifier[1] = nn.Linear(m.classifier[1].in_features, n_clases)
        cabeza = m.classifier
    elif arch == "efficientnet_b0":
        m = models.efficientnet_b0(weights="IMAGENET1K_V1")
        m.classifier[1] = nn.Linear(m.classifier[1].in_features, n_clases)
        cabeza = m.classifier
    elif arch == "resnet50":
        m = models.resnet50(weights="IMAGENET1K_V2")
        m.fc = nn.Linear(m.fc.in_features, n_clases)
        cabeza = m.fc
    elif arch == "vgg16":
        m = models.vgg16(weights="IMAGENET1K_V1")
        m.classifier[6] = nn.Linear(m.classifier[6].in_features, n_clases)
        cabeza = m.classifier
    else:
        raise ValueError(f"Arquitectura no soportada: {arch}")
    return m, cabeza


def congelar_backbone(modelo, cabeza, congelar=True):
    for p in modelo.parameters():
        p.requires_grad = not congelar
    for p in cabeza.parameters():
        p.requires_grad = True


# ── Data ──────────────────────────────────────────────────────────────────────
def make_loaders(img_size, batch):
    # Normalización estándar de ImageNet (obligatoria con pesos preentrenados)
    norm = transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    # Aumentaciones extra: el preprocess ya degradó, aquí variamos pose/luz
    tf_train = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.RandomHorizontalFlip(),
        transforms.RandomRotation(20),
        transforms.ColorJitter(0.2, 0.2, 0.2, 0.05),
        transforms.ToTensor(), norm,
    ])
    tf_val = transforms.Compose([
        transforms.Resize((img_size, img_size)),
        transforms.ToTensor(), norm,
    ])
    ds_tr = datasets.ImageFolder(C.PROC_DIR / "train", tf_train)
    ds_va = datasets.ImageFolder(C.PROC_DIR / "val", tf_val)
    # class_to_idx queda por orden alfabético: {antracnosis:0, sana:1}
    ld_tr = DataLoader(ds_tr, batch, shuffle=True, num_workers=C.NUM_WORKERS,
                       pin_memory=True, drop_last=True)
    ld_va = DataLoader(ds_va, batch, shuffle=False, num_workers=C.NUM_WORKERS,
                       pin_memory=True)
    return ds_tr, ds_va, ld_tr, ld_va


def pesos_de_clase(ds):
    """Pesos inversos a la frecuencia para compensar el desbalance."""
    import collections
    cnt = collections.Counter([y for _, y in ds.samples])
    total = sum(cnt.values())
    w = torch.tensor([total / (len(cnt) * cnt[i]) for i in range(len(cnt))],
                     dtype=torch.float32)
    return w


# ── Bucle de entrenamiento ────────────────────────────────────────────────────
def correr_epoca(modelo, loader, crit, opt, scaler, device, entrenar):
    modelo.train() if entrenar else modelo.eval()
    total, loss_sum = 0, 0.0
    ys, yps = [], []
    with torch.set_grad_enabled(entrenar):
        for x, y in loader:
            x, y = x.to(device, non_blocking=True), y.to(device, non_blocking=True)
            if entrenar:
                opt.zero_grad(set_to_none=True)
            with torch.autocast(device_type=device.type, enabled=(device.type == "cuda")):
                out = modelo(x)
                loss = crit(out, y)
            if entrenar:
                scaler.scale(loss).backward()
                scaler.step(opt)
                scaler.update()
            loss_sum += loss.item() * x.size(0)
            total += x.size(0)
            ys += y.cpu().tolist()
            yps += out.argmax(1).cpu().tolist()
    f1 = f1_score(ys, yps, average="macro")
    return loss_sum / total, f1, ys, yps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", default=C.ARCH)
    ap.add_argument("--epochs-head", type=int, default=C.EPOCHS_HEAD)
    ap.add_argument("--epochs-fine", type=int, default=C.EPOCHS_FINE)
    ap.add_argument("--batch", type=int, default=C.BATCH_SIZE)
    args = ap.parse_args()

    torch.manual_seed(C.SEED)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Dispositivo: {device}  ({torch.cuda.get_device_name(0) if device.type=='cuda' else 'sin GPU'})")
    if device.type != "cuda":
        print("[!] CUDA no disponible: revisa el paso 2 del README (instalar torch con CUDA).")

    C.OUT_DIR.mkdir(parents=True, exist_ok=True)
    ds_tr, ds_va, ld_tr, ld_va = make_loaders(C.IMG_SIZE, args.batch)
    print("Clases (índice):", ds_tr.class_to_idx)
    print(f"Train: {len(ds_tr)}  Val: {len(ds_va)}")

    modelo, cabeza = construir_modelo(args.arch, len(ds_tr.classes))
    modelo.to(device)
    crit = nn.CrossEntropyLoss(weight=pesos_de_clase(ds_tr).to(device))
    scaler = torch.cuda.amp.GradScaler(enabled=(device.type == "cuda"))

    hist = {"train_loss": [], "val_loss": [], "train_f1": [], "val_f1": []}
    mejor_f1, mejor_state = -1, None

    def entrenar_fase(nombre, epocas, lr, congelar):
        nonlocal mejor_f1, mejor_state
        congelar_backbone(modelo, cabeza, congelar)
        params = [p for p in modelo.parameters() if p.requires_grad]
        opt = torch.optim.AdamW(params, lr=lr, weight_decay=1e-4)
        for e in range(1, epocas + 1):
            trl, trf, *_ = correr_epoca(modelo, ld_tr, crit, opt, scaler, device, True)
            val, vaf, ys, yps = correr_epoca(modelo, ld_va, crit, opt, scaler, device, False)
            hist["train_loss"].append(trl); hist["val_loss"].append(val)
            hist["train_f1"].append(trf); hist["val_f1"].append(vaf)
            print(f"[{nombre} {e}/{epocas}] loss {trl:.3f}/{val:.3f}  F1 {trf:.3f}/{vaf:.3f}")
            if vaf > mejor_f1:
                mejor_f1 = vaf
                mejor_state = {k: v.cpu().clone() for k, v in modelo.state_dict().items()}
                torch.save({"state_dict": mejor_state, "arch": args.arch,
                            "classes": ds_tr.classes, "class_to_idx": ds_tr.class_to_idx,
                            "img_size": C.IMG_SIZE},
                           C.OUT_DIR / "mejor_modelo.pt")

    print("\n=== FASE 1: cabeza (backbone congelado) ===")
    entrenar_fase("head", args.epochs_head, C.LR_HEAD, congelar=True)
    print("\n=== FASE 2: fine-tuning completo ===")
    entrenar_fase("fine", args.epochs_fine, C.LR_FINE, congelar=False)

    # ── Reporte final con el mejor modelo ────────────────────────────────────
    modelo.load_state_dict(mejor_state)
    _, _, ys, yps = correr_epoca(modelo, ld_va, crit, None, scaler, device, False)
    rep = classification_report(ys, yps, target_names=ds_tr.classes, digits=3)
    cm = confusion_matrix(ys, yps)
    print(f"\nMejor F1 (macro) val: {mejor_f1:.3f}\n")
    print(rep)

    (C.OUT_DIR / "reporte.txt").write_text(
        f"Arch: {args.arch}\nMejor F1 macro: {mejor_f1:.3f}\n\n{rep}\n\nMatriz de confusión:\n{cm}\n",
        encoding="utf-8")
    json.dump(hist, open(C.OUT_DIR / "historial.json", "w"), indent=2)

    # gráficas
    fig, ax = plt.subplots(1, 2, figsize=(11, 4))
    ax[0].plot(hist["train_loss"], label="train"); ax[0].plot(hist["val_loss"], label="val")
    ax[0].set_title("Loss"); ax[0].legend()
    ax[1].plot(hist["train_f1"], label="train"); ax[1].plot(hist["val_f1"], label="val")
    ax[1].set_title("F1 macro"); ax[1].legend()
    fig.tight_layout(); fig.savefig(C.OUT_DIR / "curvas.png", dpi=120)

    fig2, ax2 = plt.subplots(figsize=(4, 4))
    ax2.imshow(cm, cmap="Greens")
    ax2.set_xticks(range(len(ds_tr.classes))); ax2.set_yticks(range(len(ds_tr.classes)))
    ax2.set_xticklabels(ds_tr.classes); ax2.set_yticklabels(ds_tr.classes)
    ax2.set_xlabel("Predicho"); ax2.set_ylabel("Real")
    for i in range(len(cm)):
        for j in range(len(cm)):
            ax2.text(j, i, cm[i, j], ha="center", va="center")
    fig2.tight_layout(); fig2.savefig(C.OUT_DIR / "matriz_confusion.png", dpi=120)

    print("\nGuardado en", C.OUT_DIR.resolve(),
          "\n  mejor_modelo.pt · reporte.txt · curvas.png · matriz_confusion.png")


if __name__ == "__main__":
    main()
