from dotenv import load_dotenv
load_dotenv()

import os
from pathlib import Path

from fastapi import FastAPI, Depends, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from pydantic import BaseModel
from sqlalchemy.exc import IntegrityError
from typing import Optional, Any
from datetime import datetime, timezone, timedelta
from database import get_db, init_db

# Carpeta donde se guardan las fotos JPG que envía el ESP32 (en disco, NO en la BD).
CAPTURAS_DIR = Path(os.getenv("CAPTURAS_DIR", "capturas"))
CAPTURAS_DIR.mkdir(parents=True, exist_ok=True)

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


class PaltaPayload(BaseModel):
    """Resultado promediado de un ciclo completo de palta (3 vueltas).

    El ESP32 acumula las 3 lecturas internamente, promedia y envía UN payload
    al cerrar el ciclo. clasificacion/confianza/votos llegan vacíos mientras no
    se integren los modelos TFLite (fase actual: solo captura)."""
    lote_id:           int
    clasificacion:     Optional[str]   = None   # 'sana'|'antracnosis'|'no_es_palta'
    confianza:         Optional[float] = None
    votos_sana:        Optional[int]   = 0
    votos_antracnosis: Optional[int]   = 0
    r:                 Optional[int]   = None
    g:                 Optional[int]   = None
    b:                 Optional[int]   = None
    lux:               Optional[float] = None
    temp:              Optional[float] = None
    humedad:           Optional[float] = None
    ir_detectado:      Optional[bool]  = True
    lecturas:          Optional[int]   = 3


class SensoresPayload(BaseModel):
    """Lecturas de sensores de UNA ronda. El ESP32 las envía SOLO cuando el gate
    confirmó que es palta (los frames 'no_es_palta' no mandan sensores)."""
    r:       Optional[int]   = None
    g:       Optional[int]   = None
    b:       Optional[int]   = None
    lux:     Optional[float] = None
    temp:    Optional[float] = None
    humedad: Optional[float] = None


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
            Palta.lote_id == l.id, Palta.clasificacion.in_(['antracnosis', 'scab'])
        ).scalar() or 0
        no_palta = db.query(sqlfunc.count(Palta.id)).filter(
            Palta.lote_id == l.id, Palta.clasificacion == 'no_es_palta'
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
            "no_palta":           no_palta,
            "temp_promedio":      round(float(temp_prom), 2) if temp_prom else None,
            "humedad_promedio":   round(float(hum_prom), 2)  if hum_prom  else None,
            "confianza_promedio": round(float(conf_prom) * 100, 2) if conf_prom else None,
        })
    return result


@app.get("/api/lote/activo")
def lote_activo(db: Any = Depends(get_db)):
    """Señal de control que el ESP32 pollea cada 5s.

    Devuelve el lote abierto (fin IS NULL) o {"activo": false}. El ESP32
    arranca la cinta/servos cuando hay lote activo y se detiene cuando deja
    de estarlo (es decir, cuando el frontend cierra el lote)."""
    if db is None:
        return {"activo": False}
    try:
        from models import Lote
        lote = db.query(Lote).filter(Lote.fin.is_(None)).order_by(Lote.inicio.desc()).first()
        if lote is None:
            return {"activo": False}
        return {
            "activo": True,
            "id":     lote.id,
            "codigo": lote.codigo,
            "inicio": lote.inicio.isoformat() if lote.inicio else None,
        }
    except Exception:
        return {"activo": False}


@app.post("/api/lote")
def crear_lote(body: LoteCreate, db: Any = Depends(get_db)):
    if db is not None:
        from models import Lote
        codigo = body.codigo.strip()
        if not codigo:
            raise HTTPException(status_code=400, detail="Ingresa un codigo de lote.")
        existente = db.query(Lote).filter(Lote.codigo == codigo).first()
        if existente is not None:
            raise HTTPException(
                status_code=409,
                detail=f"Ya existe un lote con el codigo '{codigo}'. Usa un codigo nuevo.",
            )
        # Cierra cualquier lote que haya quedado ABIERTO (remanente) antes de
        # crear el nuevo: así nunca hay dos lotes activos a la vez y el ESP32
        # (que pollea /api/lote/activo) siempre apunta a uno solo.
        abiertos = db.query(Lote).filter(Lote.fin.is_(None)).all()
        for prev in abiertos:
            prev.fin = datetime.now(timezone.utc)
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
    """KPIs del lote. Se calculan DIRECTO con consultas (no dependen de la vista
    vista_kpis_lote, que puede no existir si el volumen de la BD es viejo)."""
    base = {
        "lote_id": lote_id, "codigo": None, "inicio": None, "fin": None,
        "total": 0, "sanas": 0, "rechazadas": 0, "no_palta": 0, "tasa_rechazo": 0.0,
        "confianza_promedio": None, "temp_promedio": None, "humedad_promedio": None,
    }
    if db is None:
        return base

    from models import Lote, Palta, SensorData
    from sqlalchemy import func as sqlfunc

    lote = db.query(Lote).filter(Lote.id == lote_id).first()
    total = db.query(sqlfunc.count(Palta.id)).filter(Palta.lote_id == lote_id).scalar() or 0
    sanas = db.query(sqlfunc.count(Palta.id)).filter(
        Palta.lote_id == lote_id, Palta.clasificacion == 'sana'
    ).scalar() or 0
    rechazadas = db.query(sqlfunc.count(Palta.id)).filter(
        Palta.lote_id == lote_id, Palta.clasificacion.in_(['antracnosis', 'scab'])
    ).scalar() or 0
    no_palta = db.query(sqlfunc.count(Palta.id)).filter(
        Palta.lote_id == lote_id, Palta.clasificacion == 'no_es_palta'
    ).scalar() or 0
    conf = db.query(sqlfunc.avg(Palta.confianza)).filter(Palta.lote_id == lote_id).scalar()
    temp = db.query(sqlfunc.avg(SensorData.temp)).join(
        Palta, SensorData.palta_id == Palta.id
    ).filter(Palta.lote_id == lote_id).scalar()
    hum = db.query(sqlfunc.avg(SensorData.humedad)).join(
        Palta, SensorData.palta_id == Palta.id
    ).filter(Palta.lote_id == lote_id).scalar()

    return {
        "lote_id":            lote_id,
        "codigo":             lote.codigo if lote else None,
        "inicio":             lote.inicio.isoformat() if lote and lote.inicio else None,
        "fin":                lote.fin.isoformat() if lote and lote.fin else None,
        "total":              total,
        "sanas":              sanas,
        "rechazadas":         rechazadas,
        "no_palta":           no_palta,
        "tasa_rechazo":       round(rechazadas / total * 100, 2) if total else 0.0,
        "confianza_promedio": round(float(conf), 4) if conf is not None else None,
        "temp_promedio":      round(float(temp), 2) if temp is not None else None,
        "humedad_promedio":   round(float(hum), 2) if hum is not None else None,
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
            "id":            p.id,   # uso interno (key + foto_url); el UI NO lo muestra
            "clasificacion": p.clasificacion,     # sana | antracnosis | scab | no_es_palta
            "confianza":     p.confianza,
            "confianza_gate": p.confianza_gate,   # prob del gate de que ES palta
            "probabilidades": p.probabilidades,   # {sana,antracnosis,scab} o None
            "foto_url":      f"/api/palta/{p.id}/foto" if p.foto_ruta else None,
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
def recibir_palta(payload: PaltaPayload, db: Any = Depends(get_db)):
    """Recibe el resultado promediado de un ciclo de palta del ESP32.

    Crea la fila `palta` (cabecera del veredicto) + la fila `sensor_data`
    enlazada (lectura promediada de los sensores) de forma atómica, y devuelve
    el palta_id para que el ESP32 pueda asociar luego la foto JPG."""
    if db is None:
        # Sin BD: devolvemos un id ficticio para no romper el flujo de prueba.
        return {"ok": True, "palta_id": None, "sin_bd": True}

    from models import Palta, SensorData, Lote
    # El veredicto REAL lo asigna /api/captura/foto (el modelo corre en el
    # backend sobre cada foto). Aquí solo creamos la cabecera de la palta con lo
    # que mande el ESP32 (normalmente clasificacion=null hasta que llega la
    # primera foto); NO inventamos 'sana'/0.5.
    clasificacion = payload.clasificacion
    if clasificacion == "no_palta":
        clasificacion = "no_es_palta"

    palta = Palta(
        lote_id=payload.lote_id,
        clasificacion=clasificacion,   # puede ser None; se llena con la foto
        confianza=payload.confianza,
        votos_sana=payload.votos_sana or 0,
        votos_antracnosis=payload.votos_antracnosis or 0,
    )
    db.add(palta)
    db.flush()   # asigna palta.id sin cerrar la transacción

    lectura = SensorData(
        palta_id=palta.id,
        r=payload.r,
        g=payload.g,
        b=payload.b,
        lux=payload.lux,
        temp=payload.temp,
        humedad=payload.humedad,
        ir_detectado=payload.ir_detectado,
    )
    db.add(lectura)

    # Actualiza el contador del lote (atributo total_paltas) por cada palta.
    lote = db.query(Lote).filter(Lote.id == payload.lote_id).first()
    if lote is not None:
        lote.total_paltas = (lote.total_paltas or 0) + 1

    db.commit()
    db.refresh(palta)
    return {"ok": True, "palta_id": palta.id}


# ─── Captura de foto JPG (el ESP32 envía el JPEG crudo, sin ArduinoJson) ──────

@app.post("/api/captura/foto")
async def recibir_foto(
    request: Request,
    palta_id: Optional[int] = None,
    lote_id:  Optional[int] = None,
    db: Any = Depends(get_db),
):
    """Recibe una imagen JPEG cruda en el cuerpo de la petición.

    El ESP32 hace POST con Content-Type: image/jpeg y los bytes de la foto;
    palta_id y lote_id viajan como query params (fáciles de armar en Arduino,
    sin necesidad de JSON). La imagen se guarda en disco y solo la RUTA se
    persiste en la columna palta.foto_ruta."""
    img = await request.body()
    if not img or len(img) < 100:
        raise HTTPException(status_code=400, detail="Imagen vacía o demasiado pequeña")

    carpeta = CAPTURAS_DIR / f"lote_{lote_id or 0}"
    carpeta.mkdir(parents=True, exist_ok=True)

    # microsegundos en el nombre: evita que 2 fotos del mismo segundo se pisen y
    # da una URL ÚNICA por foto (el navegador ya no sirve una vieja cacheada).
    sello = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    nombre = f"palta_{palta_id or 'x'}_{sello}.jpg"
    ruta = carpeta / nombre
    ruta.write_bytes(img)

    ruta_rel = str(ruta.as_posix())

    # Clasificación con el modelo (MobileNetV2) usando ESTA foto.
    pred = None
    try:
        from inference import predecir
        pred = predecir(img)
        if pred is not None:
            print(f"[ML] palta {palta_id}: {pred['enfermedad']} "
                  f"-> {pred['clasificacion']} ({pred['confianza']*100:.1f}%)")
    except Exception as e:
        print(f"[ML] inferencia no disponible: {e}")

    # Guardar la referencia (no el blob) + el veredicto del modelo en la palta.
    clasif_agregada = None   # veredicto AGREGADO (voto de las fotos) para el firmware
    if db is not None and palta_id is not None:
        try:
            from models import Palta
            palta = db.query(Palta).filter(Palta.id == palta_id).first()
            if palta is not None:
                palta.foto_ruta = ruta_rel
                if pred is not None:
                    # Cada foto (vuelta) VOTA entre 4 salidas. La misma palta
                    # acumula los votos y gana la mayoría:
                    #   no_es_palta (gate) / sana / antracnosis / scab
                    clasif = pred["clasificacion"]
                    if clasif == "no_es_palta":
                        palta.votos_no_palta = (palta.votos_no_palta or 0) + 1
                    elif clasif == "antracnosis":
                        palta.votos_antracnosis = (palta.votos_antracnosis or 0) + 1
                    elif clasif == "scab":
                        palta.votos_scab = (palta.votos_scab or 0) + 1
                    else:  # sana
                        palta.votos_sana = (palta.votos_sana or 0) + 1

                    vn = palta.votos_no_palta or 0
                    vs = palta.votos_sana or 0
                    va = palta.votos_antracnosis or 0
                    vk = palta.votos_scab or 0
                    # gana la mayoría; empate -> prioridad antracnosis > scab > sana
                    # > no_es_palta (favorece cazar enfermas). max() toma el 1º en empate.
                    ganador = max(
                        [("antracnosis", va), ("scab", vk), ("sana", vs), ("no_es_palta", vn)],
                        key=lambda t: t[1],
                    )
                    palta.clasificacion = ganador[0]
                    total = vn + vs + va + vk
                    palta.confianza = round(ganador[1] / total, 4) if total else pred["confianza"]
                    # también: confianza del gate y probs de enfermedad (de esta foto)
                    palta.confianza_gate = pred.get("confianza_gate")
                    if pred.get("probabilidades_enfermedad") is not None:
                        palta.probabilidades = pred["probabilidades_enfermedad"]
                db.commit()
                clasif_agregada = palta.clasificacion   # veredicto votado tras esta foto
        except Exception:
            db.rollback()

    # clasificacion          = veredicto de ESTA foto (compat).
    # clasificacion_agregada = veredicto VOTADO de la palta -> lo lee el firmware
    #                          tras la 3a foto para decidir la compuerta.
    clasif_foto = pred["clasificacion"] if pred is not None else None
    return {"ok": True, "palta_id": palta_id, "foto_ruta": ruta_rel,
            "bytes": len(img), "clasificacion": clasif_foto,
            "clasificacion_agregada": clasif_agregada if clasif_agregada is not None else clasif_foto,
            "prediccion": pred}


@app.post("/api/palta/{palta_id}/sensores")
def actualizar_sensores(palta_id: int, payload: SensoresPayload, db: Any = Depends(get_db)):
    """Actualiza las lecturas de sensores de una palta. El ESP32 llama aquí SOLO
    en las rondas en que el gate confirmó que ES palta (eficiencia: los frames
    'no_es_palta' no toman ni promedian sensores). Refresca la última SensorData."""
    if db is None:
        return {"ok": True, "sin_bd": True}
    try:
        from models import SensorData
        sd = db.query(SensorData).filter(
            SensorData.palta_id == palta_id
        ).order_by(SensorData.timestamp.desc()).first()
        if sd is None:
            sd = SensorData(palta_id=palta_id)
            db.add(sd)
        if payload.r is not None:       sd.r = payload.r
        if payload.g is not None:       sd.g = payload.g
        if payload.b is not None:       sd.b = payload.b
        if payload.lux is not None:     sd.lux = payload.lux
        if payload.temp is not None:    sd.temp = payload.temp
        if payload.humedad is not None: sd.humedad = payload.humedad
        db.commit()
        return {"ok": True, "palta_id": palta_id}
    except Exception as e:
        db.rollback()
        return {"ok": False, "error": str(e)}


@app.get("/api/palta/{palta_id}/fotos")
def listar_fotos(palta_id: int, db: Any = Depends(get_db)):
    """Lista las fotos de las vueltas de UNA palta. Para no mezclar fotos de OTRO
    lote/palta (ids reciclados tras reiniciar la BD, o residuos en disco):
      1) se limita a la carpeta del lote al que pertenece ESTA palta, y
      2) descarta fotos anteriores a la creación de la palta (por fecha de archivo)."""
    lote_id, desde = None, None
    if db is not None:
        try:
            from models import Palta
            p = db.query(Palta).filter(Palta.id == palta_id).first()
            if p is not None:
                lote_id = p.lote_id
                desde = p.timestamp
        except Exception:
            pass
    patron = (f"lote_{lote_id}/palta_{palta_id}_*.jpg"
              if lote_id is not None else f"lote_*/palta_{palta_id}_*.jpg")
    fotos = []
    for ph in sorted(CAPTURAS_DIR.glob(patron)):
        if desde is not None:
            try:
                mt = datetime.fromtimestamp(ph.stat().st_mtime, tz=timezone.utc)
                if mt < desde - timedelta(seconds=60):   # foto residual de una corrida anterior
                    continue
            except Exception:
                pass
        rel = ph.relative_to(CAPTURAS_DIR).as_posix()
        fotos.append(f"/api/foto?path={rel}")
    return {"palta_id": palta_id, "fotos": fotos}


@app.get("/api/foto")
def servir_foto(path: str):
    """Sirve un archivo de captura por ruta relativa (solo dentro de CAPTURAS_DIR)."""
    full = (CAPTURAS_DIR / path).resolve()
    base = CAPTURAS_DIR.resolve()
    if not str(full).startswith(str(base)) or not full.exists():
        raise HTTPException(status_code=404, detail="Foto no encontrada")
    return FileResponse(full, media_type="image/jpeg")


@app.get("/api/palta/{palta_id}/foto")
def ver_foto(palta_id: int, db: Any = Depends(get_db)):
    """Devuelve la imagen JPG asociada a una palta (para el dashboard)."""
    if db is None:
        raise HTTPException(status_code=404, detail="Sin base de datos")
    from models import Palta
    palta = db.query(Palta).filter(Palta.id == palta_id).first()
    if not palta or not palta.foto_ruta or not Path(palta.foto_ruta).exists():
        raise HTTPException(status_code=404, detail="Foto no encontrada")
    return FileResponse(palta.foto_ruta, media_type="image/jpeg")


# ─── Probar el modelo con UNA imagen (sin BD, sin lote, sin ESP32) ────────────

@app.post("/api/modelo/probar")
async def probar_modelo(request: Request):
    """Prueba rápida del modelo BINARIO con una imagen suelta.

    Envía el JPEG/PNG CRUDO en el cuerpo (Content-Type: image/...). Devuelve el
    veredicto Palta / No-palta con la confianza. NO toca la BD ni el lote.
    Úsalo con curl (--data-binary @foto.jpg) o desde scripts/probar_modelo.html."""
    img = await request.body()
    if not img or len(img) < 100:
        raise HTTPException(status_code=400, detail="Imagen vacía o demasiado pequeña")
    try:
        from inference import predecir
        pred = predecir(img)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Inferencia no disponible: {e}")
    if pred is None:
        raise HTTPException(status_code=503, detail="Modelo no cargado / sin resultado")

    es_palta = pred.get("clasificacion") != "no_es_palta"
    return {
        "veredicto":     "Palta" if es_palta else "No palta",
        "es_palta":      es_palta,
        "clasificacion": pred.get("clasificacion"),
        "confianza":     pred.get("confianza"),
        "probabilidades": pred.get("probabilidades", {}),
    }
