"""
PaltaCheck · Paso 4 (opcional) — Exportar a ONNX
=================================================
ONNX corre en el backend con onnxruntime (sin depender de PyTorch) y es más
rápido en CPU. Si más adelante quieres el modelo ON-DEVICE (TFLite Micro en el
ESP32), el camino es: entrenar MobileNetV2 -> ONNX -> onnx2tf -> TFLite int8.

Uso:
    python export_onnx.py
"""
import torch
import config as C
from infer import cargar

modelo, classes, img_size = cargar()
dummy = torch.randn(1, 3, img_size, img_size)
out = C.OUT_DIR / "modelo_antracnosis.onnx"
torch.onnx.export(modelo, dummy, out,
                  input_names=["input"], output_names=["logits"],
                  dynamic_axes={"input": {0: "batch"}, "logits": {0: "batch"}},
                  opset_version=17)
print("Exportado:", out.resolve())
print("Clases:", classes, "| entrada:", (1, 3, img_size, img_size))
