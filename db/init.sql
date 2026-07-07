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
    id                 SERIAL PRIMARY KEY,
    lote_id            INTEGER     NOT NULL REFERENCES lote(id) ON DELETE CASCADE,
    -- clasificacion: 'sana' | 'antracnosis' | 'scab' | 'no_es_palta'. NULL hasta
    -- que llega la primera foto y el modelo (gate Keras + enfermedad ONNX) decide.
    clasificacion      VARCHAR(20) CHECK (clasificacion IN ('sana', 'antracnosis', 'scab', 'no_es_palta')),
    confianza          FLOAT       CHECK (confianza >= 0 AND confianza <= 1),   -- clase ganadora
    confianza_gate     FLOAT,           -- prob del gate de que ES palta
    probabilidades     JSONB,           -- probs de enfermedad {sana,antracnosis,scab}
    votos_sana         SMALLINT    NOT NULL DEFAULT 0,
    votos_antracnosis  SMALLINT    NOT NULL DEFAULT 0,
    votos_scab         SMALLINT    NOT NULL DEFAULT 0,
    votos_no_palta     SMALLINT    NOT NULL DEFAULT 0,
    -- Ruta en disco de la foto representativa del ciclo (la imagen NO se
    -- guarda dentro de la BD: el JPEG ya viene comprimido y meterlo en la
    -- hypertable la infla. Aqui solo va la referencia).
    foto_ruta          TEXT,
    timestamp          TIMESTAMPTZ NOT NULL DEFAULT NOW()
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
-- COMPRESIÓN NATIVA (columnstore) de la hypertable sensor_data
-- -------------------------------------------------------------
-- TimescaleDB comprime los chunks por columnas, agrupando (segmentby) por
-- palta_id para que todas las lecturas de una misma palta queden juntas y
-- ordenadas por tiempo. Reduce el almacenamiento hasta ~90-98% sobre datos
-- numéricos de series temporales como estos (r,g,b,lux,temp,humedad).
--
-- Nota de versión: en TimescaleDB reciente esto también se puede escribir
-- como `timescaledb.enable_columnstore = true` + add_columnstore_policy().
-- La sintaxis `timescaledb.compress` + add_compression_policy() de abajo
-- sigue siendo válida y es la más compatible entre versiones.
ALTER TABLE sensor_data SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'palta_id',
    timescaledb.compress_orderby   = 'timestamp DESC'
);

-- Política automática: comprime cualquier chunk con datos más viejos que el
-- intervalo indicado. 7 días es un valor sano para producción; para una demo
-- puedes bajarlo (p.ej. INTERVAL '1 hour') o comprimir un chunk a mano con:
--   SELECT compress_chunk(c) FROM show_chunks('sensor_data') c;
SELECT add_compression_policy('sensor_data', INTERVAL '7 days', if_not_exists => TRUE);

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
    COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion = 'sana')                       AS sanas,
    COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion IN ('antracnosis','scab'))      AS rechazadas,
    COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion = 'no_es_palta')                AS no_palta,
    ROUND(
        CASE
            WHEN COUNT(DISTINCT p.id) = 0 THEN 0
            ELSE COUNT(DISTINCT p.id) FILTER (WHERE p.clasificacion IN ('antracnosis','scab'))::NUMERIC
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
-- IMPORTANTE: el lote semilla queda CERRADO (fin = NOW()) para que el sistema
-- arranque INACTIVO. Así el ESP32 (que pollea /api/lote/activo) no arranca solo
-- al encender: espera a que el frontend abra un lote nuevo.
-- -------------------------------------------------------------
INSERT INTO lote (codigo, observacion, fin)
VALUES ('LOTE-TEST-001', 'Lote de prueba inicial (cerrado)', NOW())
ON CONFLICT (codigo) DO NOTHING;
