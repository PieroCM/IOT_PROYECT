"""
PaltaCheck - Test de Integracion en Docker
==========================================
Simula las llamadas del ESP32-S3 al backend Docker (localhost:8000)
para validar que las imagenes se reciben y se muestran en el dashboard Vue.

Uso:
    python scripts/test_docker_upload.py
"""

import sys
import httpx
from pathlib import Path

URL_BASE = "http://localhost:8000"

print("\n=== PaltaCheck: Verificación de Docker API ===")

# 1. Obtener Lote Activo
try:
    r_lote = httpx.get(f"{URL_BASE}/api/lote/activo")
except Exception as e:
    print(f"[!] No se pudo conectar al backend en {URL_BASE}. ¿Está levantado Docker?")
    print(f"    Error: {e}")
    sys.exit(1)

lote_data = r_lote.json()
if not lote_data.get("activo"):
    print("[i] No hay lote activo. Creando uno automáticamente para la prueba...")
    r_nuevo = httpx.post(f"{URL_BASE}/api/lote", json={"codigo": "LOTE-DOCKER-AUTO"})
    if r_nuevo.status_code != 200:
        print(f"[!] Error al crear lote automático: {r_nuevo.text}")
        sys.exit(1)
    lote_data = r_nuevo.json()

lote_id = lote_data["id"]
codigo = lote_data["codigo"]
print(f"[OK] Lote activo detectado: #{codigo} (ID: {lote_id})")

# 2. Registrar Palta (Telemetría de sensores simulada)
payload = {
    "lote_id": lote_id,
    "clasificacion": "sana",
    "confianza": 0.94,
    "votos_sana": 2,
    "votos_antracnosis": 1,
    "r": 85,
    "g": 140,
    "b": 90,
    "lux": 420.0,
    "temp": 24.5,
    "humedad": 68.2,
    "ir_detectado": True,
    "lecturas": 3
}

r_palta = httpx.post(f"{URL_BASE}/api/palta", json=payload)
if r_palta.status_code != 200:
    print(f"[!] Error al registrar palta: {r_palta.text}")
    sys.exit(1)

palta_id = r_palta.json()["palta_id"]
print(f"[OK] Palta registrada con ID: {palta_id}")

# 3. Crear Imagen JPEG Simulada y Subirla
# Cabecera estándar de JPEG binario falso
jpeg_bytes = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x01\x00`\x00`\x00\x00" + b"PALTA" * 1000 + b"\xff\xd9"

print("[i] Subiendo imagen de prueba...")
r_foto = httpx.post(
    f"{URL_BASE}/api/captura/foto?palta_id={palta_id}&lote_id={lote_id}",
    content=jpeg_bytes,
    headers={"Content-Type": "image/jpeg"}
)

if r_foto.status_code == 200:
    res = r_foto.json()
    print(f"[OK] Imagen subida exitosamente.")
    print(f"     Guardada en disco: {res['foto_ruta']}")
    print(f"     MIME: image/jpeg ({res['bytes']} bytes)")
    print("\n>>> ¡Verificación completada! Abre http://localhost:5173 y confirma")
    print("    que la palta aparece en el historial con su telemetría e imagen.")
else:
    print(f"[!] Error al subir la imagen: {r_foto.text}")
