from dotenv import load_dotenv
load_dotenv()

from fastapi import FastAPI, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional, Any
from datetime import datetime, timezone
from database import get_db, init_db

app = FastAPI(title="PaltaCheck API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def startup():
    init_db()


# ─── Schemas ────────────────────────────────────────────────────────────────

class LoteCreate(BaseModel):
    codigo: str


class ESP32Payload(BaseModel):
    palta_id: Optional[int] = None
    lote_id:  Optional[int] = None
    r:         Optional[int]   = None
    g:         Optional[int]   = None
    b:         Optional[int]   = None
    lux:       Optional[float] = None
    temp:      Optional[float] = None
    humedad:   Optional[float] = None
    ir_detectado: Optional[bool] = True


# ─── Health ──────────────────────────────────────────────────────────────────

@app.get("/health")
def health():
    return {"status": "ok", "service": "PaltaCheck API"}


# ─── Lotes ───────────────────────────────────────────────────────────────────

@app.get("/api/lotes")
def listar_lotes(db: Any = Depends(get_db)):
    if db is None:
        return []
    from models import Lote, Palta, SensorData
    from sqlalchemy import func as sqlfunc
    lotes = db.query(Lote).order_by(Lote.inicio.desc()).all()
    result = []
    for l in lotes:
        total = db.query(sqlfunc.count(Palta.id)).filter(Palta.lote_id == l.id).scalar() or 0
        sanas = db.query(sqlfunc.count(Palta.id)).filter(
            Palta.lote_id == l.id, Palta.clasificacion == 'sana'
        ).scalar() or 0
        rechazo = db.query(sqlfunc.count(Palta.id)).filter(
            Palta.lote_id == l.id, Palta.clasificacion == 'antracnosis'
        ).scalar() or 0
        temp_prom = db.query(sqlfunc.avg(SensorData.temp)).join(
            Palta, SensorData.palta_id == Palta.id
        ).filter(Palta.lote_id == l.id).scalar()
        hum_prom = db.query(sqlfunc.avg(SensorData.humedad)).join(
            Palta, SensorData.palta_id == Palta.id
        ).filter(Palta.lote_id == l.id).scalar()
        conf_prom = db.query(sqlfunc.avg(Palta.confianza)).filter(
            Palta.lote_id == l.id
        ).scalar()
        result.append({
            "id":                 l.id,
            "codigo":             l.codigo,
            "inicio":             l.inicio.isoformat() if l.inicio else None,
            "fin":                l.fin.isoformat()    if l.fin    else None,
            "total":              total,
            "sanas":              sanas,
            "rechazadas":         rechazo,
            "temp_promedio":      round(float(temp_prom), 2) if temp_prom else None,
            "humedad_promedio":   round(float(hum_prom), 2)  if hum_prom  else None,
            "confianza_promedio": round(float(conf_prom) * 100, 2) if conf_prom else None,
        })
    return result


@app.post("/api/lote")
def crear_lote(body: LoteCreate, db: Any = Depends(get_db)):
    if db is not None:
        from models import Lote
        lote = Lote(codigo=body.codigo)
        db.add(lote)
        db.commit()
        db.refresh(lote)
        return {
            "id":     lote.id,
            "codigo": lote.codigo,
            "inicio": lote.inicio.isoformat() if lote.inicio else None,
        }
    return {"id": 1, "codigo": body.codigo, "inicio": datetime.now(timezone.utc).isoformat()}


@app.post("/api/lote/{lote_id}/cerrar")
def cerrar_lote(lote_id: int, db: Any = Depends(get_db)):
    if db is None:
        return {"ok": True, "id": lote_id}
    from models import Lote
    lote = db.query(Lote).filter(Lote.id == lote_id).first()
    if not lote:
        raise HTTPException(status_code=404, detail="Lote no encontrado")
    lote.fin = datetime.now(timezone.utc)
    db.commit()
    return {"ok": True, "id": lote.id, "codigo": lote.codigo}


@app.get("/api/lote/{lote_id}/kpis")
def get_kpis(lote_id: int, db: Any = Depends(get_db)):
    if db is not None:
        try:
            from sqlalchemy import text
            row = db.execute(
                text("SELECT * FROM vista_kpis_lote WHERE lote_id = :id"),
                {"id": lote_id}
            ).mappings().first()
            if row:
                return dict(row)
        except Exception:
            pass
    return {
        "lote_id":            lote_id,
        "total":              0,
        "sanas":              0,
        "rechazadas":         0,
        "tasa_rechazo":       0.0,
        "confianza_promedio": None,
        "temp_promedio":      None,
        "humedad_promedio":   None,
    }


@app.get("/api/lote/{lote_id}/paltas")
def get_paltas(lote_id: int, db: Any = Depends(get_db)):
    if db is None:
        return []
    from models import Palta, SensorData
    paltas = db.query(Palta).filter(Palta.lote_id == lote_id).order_by(Palta.timestamp).all()
    result = []
    for p in paltas:
        sd = db.query(SensorData).filter(
            SensorData.palta_id == p.id
        ).order_by(SensorData.timestamp.desc()).first()
        result.append({
            "id":            p.id,
            "lote_id":       p.lote_id,
            "clasificacion": p.clasificacion,
            "confianza":     p.confianza,
            "timestamp":     p.timestamp.isoformat() if p.timestamp else None,
            "sensor": {
                "r":       sd.r,
                "g":       sd.g,
                "b":       sd.b,
                "lux":     sd.lux,
                "temp":    sd.temp,
                "humedad": sd.humedad,
            } if sd else None,
        })
    return result


# ─── Palta / Sensor data ─────────────────────────────────────────────────────

@app.post("/api/palta")
def recibir_palta(payload: ESP32Payload, db: Any = Depends(get_db)):
    if db is not None:
        from models import SensorData
        entry = SensorData(
            palta_id=payload.palta_id or 1,
            r=payload.r,
            g=payload.g,
            b=payload.b,
            lux=payload.lux,
            temp=payload.temp,
            humedad=payload.humedad,
            ir_detectado=payload.ir_detectado,
        )
        db.add(entry)
        db.commit()
    return {"ok": True}
