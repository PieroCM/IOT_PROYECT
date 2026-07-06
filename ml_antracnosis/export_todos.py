"""
PaltaCheck · Exportar los 3 modelos entrenados a ONNX
=====================================================
Recorre outputs/<arch>/mejor_modelo.pt (mobilenet_v2, resnet50, efficientnet_b0),
exporta cada uno a su propio <arch>/modelo_antracnosis.onnx y lo verifica con
onnxruntime (corre un tensor 1x3x224x224 y confirma que salen 3 logits).

No cambia la arquitectura ni el preprocesado: reutiliza construir_modelo() de
infer.py, la misma construccion que usa la inferencia.

Uso (desde ml_antracnosis/, con el venv):
    .\.venv\Scripts\python.exe export_todos.py
"""
import numpy as np
import onnxruntime as ort
import torch

import config as C
from infer import construir_modelo   # misma construccion que la inferencia

ARCHS = ["mobilenet_v2", "resnet50", "efficientnet_b0"]


def exportar_uno(arch):
    ckpt_path = C.OUT_DIR / arch / "mejor_modelo.pt"
    if not ckpt_path.exists():
        print(f"[SKIP] {arch}: no existe {ckpt_path}")
        return False

    # torch 2.6: weights_only=False porque el checkpoint guarda arch/classes/etc.
    ck = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    classes = ck["classes"]
    img = ck["img_size"]

    modelo = construir_modelo(ck["arch"], len(classes))
    modelo.load_state_dict(ck["state_dict"])
    modelo.eval()

    onnx_path = C.OUT_DIR / arch / "modelo_antracnosis.onnx"
    dummy = torch.randn(1, 3, img, img)
    with torch.no_grad():
        logits_torch = modelo(dummy).numpy()
    torch.onnx.export(
        modelo, dummy, str(onnx_path),
        input_names=["input"], output_names=["logits"],
        dynamic_axes={"input": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=17,
    )

    # ── Verificacion con onnxruntime ─────────────────────────────────────────
    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    in_name = sess.get_inputs()[0].name
    x = dummy.numpy().astype(np.float32)
    logits_onnx = sess.run(None, {in_name: x})[0]

    n = len(classes)
    salida_ok = tuple(logits_onnx.shape) == (1, n)
    max_diff = float(np.abs(logits_onnx - logits_torch).max())  # export fiel?
    fiel = max_diff < 1e-3
    size_mb = onnx_path.stat().st_size / 1e6

    estado = "OK" if (salida_ok and fiel) else "MAL"
    print(f"[{arch:16s}] clases={classes}  salida={logits_onnx.shape} "
          f"(3 logits={'si' if salida_ok else 'NO'})  "
          f"diff_vs_torch={max_diff:.2e}  {size_mb:6.1f} MB  -> {estado}")
    return salida_ok and fiel


def main():
    print(f"Exportando a ONNX desde: {C.OUT_DIR.resolve()}\n")
    ok = 0
    for arch in ARCHS:
        if exportar_uno(arch):
            ok += 1
    print(f"\nResumen: {ok}/{len(ARCHS)} modelos exportados y verificados OK.")


if __name__ == "__main__":
    main()
