import os
from sqlalchemy import create_engine
from sqlalchemy.orm import declarative_base, sessionmaker

DATABASE_URL = os.getenv(
    "DATABASE_URL",
    "postgresql://paltacheck:paltacheck123@timescaledb:5432/paltacheck_db"
)

Base = declarative_base()

try:
    engine = create_engine(DATABASE_URL)
    SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
    _db_available = True
except Exception:
    engine = None
    SessionLocal = None
    _db_available = False


def get_db():
    if not _db_available or SessionLocal is None:
        yield None
        return
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db():
    if engine is not None:
        try:
            Base.metadata.create_all(bind=engine)
        except Exception as e:
            print(f"[DB] Could not create tables: {e}")
        # Migracion idempotente: agrega columnas nuevas / actualiza el CHECK a
        # tablas ya existentes (create_all NO altera tablas ya creadas). Seguro
        # de re-correr y NO pierde datos.
        migraciones = [
            "ALTER TABLE palta ADD COLUMN IF NOT EXISTS votos_no_palta SMALLINT DEFAULT 0",
            # --- Integración del modelo de enfermedad ONNX (3 clases) ---
            "ALTER TABLE palta ADD COLUMN IF NOT EXISTS votos_scab SMALLINT DEFAULT 0",
            "ALTER TABLE palta ADD COLUMN IF NOT EXISTS confianza_gate FLOAT",
            "ALTER TABLE palta ADD COLUMN IF NOT EXISTS probabilidades JSONB",
            # Reemplaza el CHECK para admitir 'scab' (sin perder filas existentes).
            "ALTER TABLE palta DROP CONSTRAINT IF EXISTS palta_clasificacion_check",
            "ALTER TABLE palta ADD CONSTRAINT palta_clasificacion_check "
            "CHECK (clasificacion IN ('sana','antracnosis','scab','no_es_palta'))",
        ]
        for sql in migraciones:
            try:
                from sqlalchemy import text
                with engine.begin() as conn:
                    conn.execute(text(sql))
            except Exception as e:
                print(f"[DB] Migracion fallo ({sql[:48]}...): {e}")
