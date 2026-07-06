"""
PaltaCheck · Paso 3 — Inferencia sobre una imagen (para el backend)
====================================================================
Aplica la MISMA degradación que en el entrenamiento NO: aquí la imagen ya viene
de la cámara real, así que solo se redimensiona y normaliza. Devuelve la clase
y la confianza, listo para conectarlo al endpoint /api/palta del backend.

Uso:
    python infer.py --img ruta/a/una_captura.jpg
"""
import argparse
import torch
import torch.nn.functional as F
from torchvision import transforms, models
from PIL import Image
import torch.nn as nn

import config as C


def construir_modelo(arch, n):
    arch = arch.lower()
    if arch == "mobilenet_v2":
        m = models.mobilenet_v2(); m.classifier[1] = nn.Linear(m.classifier[1].in_features, n)
    elif arch == "efficientnet_b0":
        m = models.efficientnet_b0(); m.classifier[1] = nn.Linear(m.classifier[1].in_features, n)
    elif arch == "resnet50":
        m = models.resnet50(); m.fc = nn.Linear(m.fc.in_features, n)
    elif arch == "vgg16":
        m = models.vgg16(); m.classifier[6] = nn.Linear(m.classifier[6].in_features, n)
    else:
        raise ValueError(arch)
    return m


def cargar(ckpt_path=None):
    # weights_only=False: el checkpoint guarda arch/classes/etc., no solo tensores
    # (torch 2.6 cambió el default a True y fallaría al cargarlo).
    ck = torch.load(ckpt_path or (C.OUT_DIR / "mejor_modelo.pt"),
                    map_location="cpu", weights_only=False)
    m = construir_modelo(ck["arch"], len(ck["classes"]))
    m.load_state_dict(ck["state_dict"])
    m.eval()
    return m, ck["classes"], ck["img_size"]


def predecir(modelo, classes, img_size, pil_img):
    norm = transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    tf = transforms.Compose([transforms.Resize((img_size, img_size)),
                             transforms.ToTensor(), norm])
    x = tf(pil_img.convert("RGB")).unsqueeze(0)
    with torch.no_grad():
        p = F.softmax(modelo(x), 1)[0]
    idx = int(p.argmax())
    return classes[idx], float(p[idx]), {c: float(p[i]) for i, c in enumerate(classes)}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", required=True)
    ap.add_argument("--ckpt", default=None)
    args = ap.parse_args()
    modelo, classes, img_size = cargar(args.ckpt)
    clase, conf, todo = predecir(modelo, classes, img_size, Image.open(args.img))
    print(f"Clasificación: {clase}   confianza: {conf:.3f}")
    print("Probabilidades:", {k: round(v, 3) for k, v in todo.items()})
