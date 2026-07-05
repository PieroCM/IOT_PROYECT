from sqlalchemy import Column, Integer, SmallInteger, String, Float, DateTime, ForeignKey, Boolean, Text
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from database import Base


class Lote(Base):
    __tablename__ = "lote"

    id = Column(Integer, primary_key=True, index=True)
    codigo = Column(String, unique=True, nullable=False, index=True)
    inicio = Column(DateTime(timezone=True), server_default=func.now())
    fin = Column(DateTime(timezone=True), nullable=True)
    total_paltas = Column(Integer, default=0)
    observacion = Column(Text, nullable=True)

    paltas = relationship("Palta", back_populates="lote")


class Palta(Base):
    __tablename__ = "palta"

    id = Column(Integer, primary_key=True, index=True)
    lote_id = Column(Integer, ForeignKey("lote.id", ondelete="CASCADE"), nullable=False)
    clasificacion = Column(String, nullable=True)   # 'sana' | 'antracnosis' | 'no_es_palta'
    confianza = Column(Float, nullable=True)
    votos_sana = Column(SmallInteger, default=0)
    votos_antracnosis = Column(SmallInteger, default=0)
    votos_no_palta = Column(SmallInteger, default=0)   # votos del filtro binario
    foto_ruta = Column(Text, nullable=True)          # ruta en disco de la foto del ciclo
    timestamp = Column(DateTime(timezone=True), server_default=func.now())

    lote = relationship("Lote", back_populates="paltas")
    sensor_data = relationship("SensorData", back_populates="palta")


class SensorData(Base):
    __tablename__ = "sensor_data"

    id = Column(Integer, primary_key=True, index=True)
    palta_id = Column(Integer, ForeignKey("palta.id", ondelete="CASCADE"), nullable=False)
    timestamp = Column(DateTime(timezone=True), server_default=func.now())
    r = Column(Integer, nullable=True)
    g = Column(Integer, nullable=True)
    b = Column(Integer, nullable=True)
    lux = Column(Float, nullable=True)
    temp = Column(Float, nullable=True)
    humedad = Column(Float, nullable=True)
    ir_detectado = Column(Boolean, default=True)

    palta = relationship("Palta", back_populates="sensor_data")
