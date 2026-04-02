-- =============================================================
-- PaltaCheck — inicialización de base de datos
-- Este script es ejecutado automáticamente por TimescaleDB al
-- arrancar el contenedor por primera vez, porque está montado en
-- /docker-entrypoint-initdb.d/init.sql
-- =============================================================

-- Extensión TimescaleDB
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- -------------------------------------------------------------
-- Tabla: lote
-- Representa un lote de paltas ingresado al sistema
-- -------------------------------------------------------------
CREATE TABLE IF NOT EXISTS lote (
    id           SERIAL PRIMARY KEY,
    codigo       VARCHAR(100) NOT NULL UNIQUE,
    inicio       TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    fin          TIMESTAMPTZ  NULL,
    total_paltas INTEGER      NOT NULL DEFAULT 0,
    observacion  TEXT
);

-- -------------------------------------------------------------
-- Tabla: palta
-- Cada palta individual dentro de un lote
-- -------------------------------------------------------------
CREATE TABLE IF NOT EXISTS palta (
    id            SERIAL PRIMARY KEY,
    lote_id       INTEGER     NOT NULL REFERENCES lote(id) ON DELETE CASCADE,
    clasificacion VARCHAR(20) CHECK (clasificacion IN ('sana', 'antracnosis')),
    confianza     FLOAT       CHECK (confianza >= 0 AND confianza <= 1),
    timestamp     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- -------------------------------------------------------------
-- Tabla: sensor_data  (hypertable TimescaleDB)
-- Lecturas del ESP32: color RGB, luz, temperatura, humedad, IR
-- -------------------------------------------------------------
CREATE TABLE IF NOT EXISTS sensor_data (
    id           SERIAL  PRIMARY KEY,
    palta_id     INTEGER NOT NULL REFERENCES palta(id) ON DELETE CASCADE,
    timestamp    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    r            INTEGER CHECK (r >= 0 AND r <= 255),
    g            INTEGER CHECK (g >= 0 AND g <= 255),
    b            INTEGER CHECK (b >= 0 AND b <= 255),
    lux          FLOAT,
    temp         FLOAT,
    humedad      FLOAT,
    ir_detectado BOOLEAN NOT NULL DEFAULT TRUE
);

-- Convertir sensor_data en hypertable particionada por tiempo
SELECT create_hypertable('sensor_data', 'timestamp', if_not_exists => TRUE);

-- -------------------------------------------------------------
-- Índices
-- -------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_palta_lote_id
    ON palta (lote_id, timestamp DESC);

CREATE INDEX IF NOT EXISTS idx_sensor_palta_id
    ON sensor_data (palta_id);

CREATE INDEX IF NOT EXISTS idx_lote_activo
    ON lote (fin)
    WHERE fin IS NULL;

-- -------------------------------------------------------------
-- Vista: vista_kpis_lote
-- Agrega métricas por lote para el dashboard
-- -------------------------------------------------------------
CREATE OR REPLACE VIEW vista_kpis_lote AS
SELECT
    l.id                                        AS lote_id,
    l.codigo,
    l.inicio,
    l.fin,
    COUNT(DISTINCT p.id)                        AS total,
    COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion = 'sana')        AS sanas,
    COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion = 'antracnosis') AS rechazadas,
    ROUND(
        CASE
            WHEN COUNT(DISTINCT p.id) = 0 THEN 0
            ELSE COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion = 'antracnosis')::NUMERIC
                 / COUNT(DISTINCT p.id) * 100
        END, 2
    )                                           AS tasa_rechazo,
    ROUND(AVG(p.confianza)::NUMERIC, 4)         AS confianza_promedio,
    ROUND(AVG(sd.temp)::NUMERIC, 2)             AS temp_promedio,
    ROUND(AVG(sd.humedad)::NUMERIC, 2)          AS humedad_promedio
FROM lote l
LEFT JOIN palta      p  ON p.lote_id  = l.id
LEFT JOIN sensor_data sd ON sd.palta_id = p.id
GROUP BY l.id, l.codigo, l.inicio, l.fin;

-- -------------------------------------------------------------
-- Datos iniciales de prueba
-- -------------------------------------------------------------
INSERT INTO lote (codigo, observacion)
VALUES ('LOTE-TEST-001', 'Lote de prueba inicial')
ON CONFLICT (codigo) DO NOTHING;
