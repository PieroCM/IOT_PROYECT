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
        # Migracion idempotente: agrega columnas nuevas a tablas ya existentes
        # (create_all NO altera tablas que ya estan creadas). Seguro de re-correr.
        try:
            from sqlalchemy import text
            with engine.begin() as conn:
                conn.execute(text(
                    "ALTER TABLE palta ADD COLUMN IF NOT EXISTS "
                    "votos_no_palta SMALLINT DEFAULT 0"
                ))
        except Exception as e:
            print(f"[DB] Migracion votos_no_palta: {e}")
