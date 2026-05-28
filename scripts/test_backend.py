"""
PaltaCheck — Prueba automática del backend (sin TimescaleDB)
============================================================
Levanta la API en memoria con una BD SQLite temporal y ejercita TODO el flujo
de control + captura, sin necesidad de Docker ni Postgres.

Uso:
    pip install fastapi "uvicorn[standard]" sqlalchemy pydantic python-dotenv httpx
    python scripts/test_backend.py

Qué valida:
    1. POST /api/lote                → abre lote
    2. GET  /api/lote/activo         → el ESP32 ve el lote activo
    3. POST /api/palta               → crea palta + sensor_data, devuelve palta_id
    4. POST /api/captura/foto        → guarda el JPEG en disco y la ruta en BD
    5. GET  /api/lote/{id}/paltas    → la palta aparece con su foto_url
    6. GET  /api/lote/{id}/kpis      → KPIs (fallback en SQLite, ok)
    7. POST /api/lote/{id}/cerrar    → cierra el lote
    8. GET  /api/lote/activo         → ya no hay lote activo
"""
import os
import sys
import tempfile
from pathlib import Path

# --- Configurar entorno ANTES de importar la app ---
tmp = Path(tempfile.mkdtemp(prefix="paltacheck_test_"))
os.environ["DATABASE_URL"] = f"sqlite:///{(tmp / 'test.db').as_posix()}"
os.environ["CAPTURAS_DIR"] = str((tmp / "capturas").as_posix())

# Permitir importar main/database/models desde backend/
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))

from fastapi.testclient import TestClient   # noqa: E402
import main                                 # noqa: E402

client = TestClient(main.app)               # dispara el startup (init_db crea tablas)

PASS, FAIL = 0, 0
def check(nombre, cond):
    global PASS, FAIL
    if cond:
        PASS += 1; print(f"  [OK] {nombre}")
    else:
        FAIL += 1; print(f"  [FALLA] {nombre}")

print("\n=== PaltaCheck — prueba de backend (SQLite) ===\n")

# 1) Abrir lote
r = client.post("/api/lote", json={"codigo": "LOTE-TEST-PRUEBA"})
check("POST /api/lote 200", r.status_code == 200)
lote = r.json(); lote_id = lote["id"]
print(f"     lote_id = {lote_id}")

# 2) Lote activo (señal del ESP32)
r = client.get("/api/lote/activo")
check("GET /api/lote/activo -> activo true", r.json().get("activo") is True)
check("GET /api/lote/activo -> id correcto", r.json().get("id") == lote_id)

# 3) Postear una palta (resultado promediado del ciclo)
payload = {
    "lote_id": lote_id, "clasificacion": None, "confianza": None,
    "votos_sana": 2, "votos_antracnosis": 1,
    "r": 112, "g": 141, "b": 85, "lux": 415.0,
    "temp": 22.4, "humedad": 64.8, "ir_detectado": True, "lecturas": 3,
}
r = client.post("/api/palta", json=payload)
check("POST /api/palta 200", r.status_code == 200)
palta_id = r.json().get("palta_id")
check("POST /api/palta -> palta_id", isinstance(palta_id, int))
print(f"     palta_id = {palta_id}")

# 4) Subir la foto (JPEG simulado)
jpeg_falso = b"\xff\xd8\xff\xe0" + b"PALTACHECK_TEST" * 50 + b"\xff\xd9"
r = client.post(
    f"/api/captura/foto?palta_id={palta_id}&lote_id={lote_id}",
    content=jpeg_falso, headers={"Content-Type": "image/jpeg"},
)
check("POST /api/captura/foto 200", r.status_code == 200)
check("foto guardada en disco", Path(r.json()["foto_ruta"]).exists())

# Rechazo de imagen muy pequeña
r_bad = client.post("/api/captura/foto", content=b"xx", headers={"Content-Type": "image/jpeg"})
check("POST foto vacía -> 400", r_bad.status_code == 400)

# 5) Listar paltas del lote
r = client.get(f"/api/lote/{lote_id}/paltas")
paltas = r.json()
check("GET paltas -> 1 palta", len(paltas) == 1)
check("palta tiene foto_url", paltas[0]["foto_url"] is not None)
check("palta tiene sensores RGB", paltas[0]["sensor"]["r"] == 112)

# 6) KPIs (en SQLite no existe la vista -> fallback)
r = client.get(f"/api/lote/{lote_id}/kpis")
check("GET kpis 200", r.status_code == 200)

# 7) Ver la imagen servida
r = client.get(f"/api/palta/{palta_id}/foto")
check("GET foto servida 200", r.status_code == 200 and r.headers["content-type"] == "image/jpeg")

# 8) Cerrar lote
r = client.post(f"/api/lote/{lote_id}/cerrar")
check("POST cerrar 200", r.status_code == 200)
r = client.get("/api/lote/activo")
check("tras cerrar -> activo false", r.json().get("activo") is False)

print(f"\n=== Resultado: {PASS} OK, {FAIL} fallas ===")
sys.exit(1 if FAIL else 0)
