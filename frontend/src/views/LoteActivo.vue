<template>
  <div class="op-root">

    <!-- ═══ SIN LOTE ACTIVO ═══ -->
    <div v-if="!store.loteActivo" class="no-lote">
      <div class="no-lote-card">
        <svg width="56" height="56" viewBox="0 0 24 24" fill="none" stroke="var(--verde-acento)" stroke-width="1.5">
          <path d="M12 2C8 2 4 6 4 10c0 5 8 12 8 12s8-7 8-12c0-4-4-8-8-8z"/>
        </svg>
        <h2>No hay lote activo</h2>
        <p>Ingresa un código para iniciar la clasificación</p>
        <div class="no-lote-form">
          <input v-model="nuevoCodigo" type="text" placeholder="Ej: LOTE-2024-008" @keyup.enter="abrirLote" />
          <button class="btn btn--primary btn--lg" @click="abrirLote" :disabled="store.loading">
            {{ store.loading ? 'Abriendo…' : 'Abrir nuevo lote' }}
          </button>
        </div>
        <p v-if="store.error" class="error-msg">{{ store.error }}</p>
      </div>
    </div>

    <!-- ═══ LOTE ACTIVO ═══ -->
    <template v-else>
      <!-- Cabecera -->
      <header class="op-head">
        <div class="head-left">
          <span class="head-lote">Lote <b>{{ store.loteActivo.codigo }}</b></span>
          <span class="head-timer">⏱ {{ elapsedTime }}</span>
        </div>
        <div class="head-decision" :class="'v--' + decisionActual(ultima?.clasificacion).tipo">
          <span class="verdict-ico">{{ verIco(ultima?.clasificacion) }}</span>
          <div class="verdict-text">
            <span class="verdict-label">{{ decisionActual(ultima?.clasificacion).label }}</span>
            <span class="verdict-sub">{{ decisionActual(ultima?.clasificacion).sub }}</span>
          </div>
        </div>
        <button class="btn btn--danger" @click="abrirConfirmacionCierre">Cerrar lote</button>
      </header>

      <!-- (A) 4 tarjetas -->
      <div class="cards3">
        <div class="card3 card3--total"><PackageCheck class="c3-icon" :size="20" /><span class="c3-num">{{ conteo.total }}</span><span class="c3-lbl">N° Paltas</span></div>
        <div class="card3 card3--sana"><CircleCheck class="c3-icon" :size="20" /><span class="c3-num">{{ conteo.pasa }}</span><span class="c3-lbl">✓ Sanas</span></div>
        <div class="card3 card3--enf"><TriangleAlert class="c3-icon" :size="20" /><span class="c3-num">{{ conteo.enferma }}</span><span class="c3-lbl">⚠ Enfermas</span></div>
        <div class="card3 card3--exp"><Ban class="c3-icon" :size="20" /><span class="c3-num">{{ conteo.expulsada }}</span><span class="c3-lbl">No palta</span></div>
      </div>

      <div class="grid2">
        <!-- IZQUIERDA -->
        <div class="col">
          <!-- Fila superior: Sensores + Radar lado a lado (deja espacio libre debajo) -->
          <div class="top-row">
          <!-- (B) Sensores — estilo "Weather" Planto (info centrada dentro de burbujas) -->
          <section class="card sensor-weather">
            <h3 class="card-title">Sensores</h3>
            <div class="sw-panel">
              <div class="sw-item sw-item--hum">
                <span class="sw-val num">{{ ultima?.sensor?.humedad != null ? ultima.sensor.humedad.toFixed(0) + '%' : '—' }}</span>
                <span class="sw-lbl">Humedad</span>
              </div>
              <div class="sw-item sw-item--temp">
                <span class="sw-val num">{{ ultima?.sensor?.temp != null ? ultima.sensor.temp.toFixed(0) + '°' : '—' }}</span>
                <span class="sw-lbl">Temperatura</span>
              </div>
              <div class="sw-item sw-item--color">
                <span class="sw-swatch" :style="{ background: rgbColor(ultima?.sensor) }"></span>
                <span class="sw-val-sm">{{ colorEstado(ultima?.sensor) }}</span>
                <span class="sw-lbl">Color · {{ rgbText(ultima?.sensor) }}</span>
              </div>
            </div>
          </section>

          <!-- (C) Distribución del lote — RADAR (estilo Planto "Plant details"), animado -->
          <section class="card radar-card">
            <h3 class="card-title">Distribución del lote</h3>
            <div class="radar-wrap">
              <svg viewBox="0 0 240 200" class="radar" role="img" aria-label="Distribución de clases del lote (radar)">
                <!-- diamante exterior (fondo pastel) + rejilla concéntrica + radios -->
                <polygon :points="radar.rings[3]" class="radar-frame" />
                <polygon v-for="(ring, i) in radar.rings" :key="'ring'+i" :points="ring" class="radar-ring" />
                <line v-for="a in radar.axes" :key="'spk'+a.key" x1="120" y1="98" :x2="a.ex" :y2="a.ey" class="radar-spoke" />
                <!-- área de datos (animada; se re-anima al cambiar el conteo) -->
                <g class="radar-data" :key="radarKey">
                  <polygon :points="radar.polygon" class="radar-area" />
                  <circle v-for="a in radar.axes" :key="'dot'+a.key" :cx="a.x" :cy="a.y" r="3.5" :fill="a.color" class="radar-dot" />
                </g>
                <!-- número (conteo) por vértice -->
                <text v-for="a in radar.axes" :key="'val'+a.key" :x="a.vx" :y="a.vy"
                      text-anchor="middle" dominant-baseline="middle" class="radar-val">{{ a.n }}</text>
                <!-- nombres de los ejes -->
                <text v-for="a in radar.axes" :key="'lbl'+a.key" :x="a.lx" :y="a.ly"
                      :text-anchor="a.anchor" dominant-baseline="middle" class="radar-axis-lbl">{{ a.label }}</text>
              </svg>
              <ul class="radar-legend">
                <li v-for="d in distribucion" :key="d.key">
                  <span class="pie-dot" :style="{ background: radarColorOf(d.key) }"></span>
                  <span class="pie-name">{{ d.label }}</span>
                  <b class="num">{{ d.n }}</b>
                  <span class="pie-pct num">{{ d.pct }}%</span>
                </li>
              </ul>
            </div>
          </section>
          </div>

          <!-- (D) Línea de fotos de la palta — estilo Planto "Plant growth activity" -->
          <section class="card growth-card">
            <h3 class="card-title">Capturas de la palta</h3>
            <div class="growth">
              <TransitionGroup name="growth" tag="div" class="growth-track">
                <div class="growth-node" v-for="(c, i) in capturas" :key="c.key"
                     :style="{ left: growthLeft(i, capturas.length) + '%', '--i': i, '--d': (i * 0.12) + 's' }">
                  <div v-if="capturaVerdict && i === capturas.length - 1"
                       class="gbubble" :class="'gbubble--' + capturaVerdict.tipo">{{ capturaVerdict.label }}</div>
                  <img :src="c.url" class="growth-thumb" :alt="'Foto ' + (i + 1)" />
                  <div class="growth-pill">
                    <span class="growth-pill-title">Foto {{ i + 1 }}</span>
                    <span class="growth-pill-sub">Captura {{ i + 1 }}/{{ capturas.length }}</span>
                  </div>
                  <span class="growth-stem"></span>
                  <span class="growth-dot"></span>
                </div>
              </TransitionGroup>
              <p v-if="!capturas.length" class="growth-empty">Esperando la primera foto…</p>
            </div>
          </section>
        </div>

        <!-- DERECHA -->
        <div class="col">
          <section class="card ultima" :class="ultima ? 'edge--' + veredicto(ultima.clasificacion).tipo : ''">
            <h3 class="card-title">Última fruta</h3>
            <template v-if="ultima">
              <!-- IMG -->
              <div class="foto-wrap">
                <img v-if="fotoUrl" :src="fotoUrl" alt="Fruta" class="foto" />
                <div v-else class="foto-empty">📷 IMG</div>
              </div>
              <!-- Estadísticas de la palta — 4 barras 3D (estilo Planto "Soil moisture") -->
              <div class="block soil-block">
                <span class="block-title">Análisis de la palta</span>
                <svg class="soil" viewBox="0 0 420 195" role="img" aria-label="Análisis de la palta">
                  <line x1="0" y1="155" x2="420" y2="155" class="soil-base" />
                  <template v-for="b in soil" :key="b.key + '-' + b.pct">
                    <g class="soil-ghost">
                      <polygon :points="b.ghost.side"  :fill="b.c.side" />
                      <polygon :points="b.ghost.top"   :fill="b.c.top" />
                      <polygon :points="b.ghost.front" :fill="b.c.front" />
                    </g>
                    <g class="soil-fill" :style="{ '--i': b.i }">
                      <polygon :points="b.fill.side"  :fill="b.c.side" />
                      <polygon :points="b.fill.top"   :fill="b.c.top" />
                      <polygon :points="b.fill.front" :fill="b.c.front" />
                    </g>
                    <text :x="b.cx" :y="b.valY" text-anchor="middle" class="soil-val">{{ b.pct }}%</text>
                    <text :x="b.cx" y="177" text-anchor="middle" class="soil-lbl">{{ b.label }}</text>
                  </template>
                </svg>
              </div>
            </template>
            <div v-else class="empty-state">Esperando la primera fruta…</div>
          </section>
        </div>
      </div>

      <div v-if="confirmarCierre" class="confirm-overlay" @click.self="confirmarCierre = false">
        <div class="confirm-dialog" role="dialog" aria-modal="true" aria-labelledby="cerrar-lote-title">
          <h2 id="cerrar-lote-title">Cerrar lote activo</h2>
          <p class="confirm-main">
            Vas a cerrar el lote <b>{{ store.loteActivo.codigo }}</b>.
          </p>
          <p class="confirm-copy">
            La linea se detendra y el ESP32 dejara de procesar nuevas frutas para este lote.
            Los datos ya registrados se conservan en el historial.
          </p>
          <div class="confirm-stats">
            <span><b>{{ conteo.total }}</b> paltas</span>
            <span><b>{{ conteo.pasa }}</b> sanas</span>
            <span><b>{{ conteo.enferma }}</b> enfermas</span>
          </div>
          <div class="confirm-actions">
            <button class="btn btn--ghost" @click="confirmarCierre = false">Seguir trabajando</button>
            <button class="btn btn--danger" @click="confirmarCerrarLote" :disabled="store.loading">
              {{ store.loading ? 'Cerrando...' : 'Si, cerrar lote' }}
            </button>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import axios from 'axios'
import { Ban, CircleCheck, PackageCheck, TriangleAlert } from '@lucide/vue'
import { usePaltaStore } from '../stores/palta'

const store = usePaltaStore()
const apiBase = axios.defaults.baseURL || ''
const nuevoCodigo = ref('')
const confirmarCierre = ref(false)
const demoMode = window.location.search.includes('demo=1')
const demoImageUrl = 'data:image/svg+xml;charset=UTF-8,' + encodeURIComponent(`
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 620">
    <rect width="900" height="620" fill="#20242a"/>
    <ellipse cx="450" cy="310" rx="210" ry="270" fill="#234b2d"/>
    <ellipse cx="430" cy="290" rx="160" ry="220" fill="#477b35"/>
    <ellipse cx="390" cy="210" rx="60" ry="105" fill="#7da44c" opacity=".55"/>
    <circle cx="520" cy="360" r="34" fill="#5b2d22" opacity=".95"/>
    <circle cx="530" cy="350" r="16" fill="#7b4131" opacity=".9"/>
    <ellipse cx="450" cy="310" rx="210" ry="270" fill="none" stroke="#d8e3cf" stroke-width="8" opacity=".18"/>
    <text x="450" y="570" text-anchor="middle" fill="#c8dcd6" font-family="Arial" font-size="28" font-weight="700">
      Imagen demo - palta con zona sospechosa
    </text>
  </svg>
`)

// ── Reloj (cronómetro del lote) ──────────────────────────
const ahora = ref(Date.now())
let tickId = null
onMounted(() => { tickId = setInterval(() => { ahora.value = Date.now() }, 1000) })
onUnmounted(() => clearInterval(tickId))
const elapsedTime = computed(() => {
  if (!store.loteActivo?.inicio) return '00:00:00'
  const s = Math.max(0, Math.floor((ahora.value - new Date(store.loteActivo.inicio).getTime()) / 1000))
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(ss).padStart(2,'0')}`
})

// ── Veredicto ────────────────────────────────────────────
const VERE = {
  sana:        { label: 'PASA',      sub: 'Palta sana',    tipo: 'pasa' },
  antracnosis: { label: 'ENFERMA',   sub: 'Antracnosis',   tipo: 'antracnosis' },
  scab:        { label: 'ENFERMA',   sub: 'Scab (sarna)',  tipo: 'scab' },
  no_es_palta: { label: 'EXPULSADA', sub: 'No es palta',   tipo: 'expulsada' },
}
const veredicto = (c) => VERE[c] || { label: '—', sub: 'Esperando…', tipo: 'idle' }
const verIco = (c) => ({ sana: '✓', antracnosis: '⚠', scab: '⚠', no_es_palta: '✖' }[c] || '·')
const esExpulsada = (c) => c === 'no_es_palta'
const decisionActual = (c) => c ? veredicto(c) : { label: 'ESPERANDO', sub: 'Sin fruta procesada', tipo: 'idle' }

// ── Probabilidades / confianza (datos reales) ────────────
const gateCerteza = (p) => {
  if (p?.confianza_gate == null) return null
  const v = esExpulsada(p.clasificacion) ? (1 - p.confianza_gate) : p.confianza_gate
  return Math.round(v * 100)
}
const sanaPct   = (p) => p?.probabilidades ? Math.round((p.probabilidades.sana ?? 0) * 100) : null
const antracPct = (p) => p?.probabilidades ? Math.round((p.probabilidades.antracnosis ?? 0) * 100) : null
const scabPct   = (p) => p?.probabilidades ? Math.round((p.probabilidades.scab ?? 0) * 100) : null

// ── Última fruta + contadores ────────────────────────────
const ultima = computed(() => store.ultimaPalta)
const conteo = computed(() => {
  const c = { total: store.paltas.length, pasa: 0, enferma: 0, expulsada: 0 }
  for (const p of store.paltas) {
    if (p.clasificacion === 'sana') c.pasa++
    else if (p.clasificacion === 'antracnosis' || p.clasificacion === 'scab') c.enferma++
    else if (p.clasificacion === 'no_es_palta') c.expulsada++
  }
  return c
})

function cargarDemo() {
  const inicio = new Date(Date.now() - 12 * 60 * 1000).toISOString()
  store.loteActivo = { id: 9001, codigo: 'DEMO-LOTE-01', inicio }
  store.esp32Online = true
  store.paltas = [
    {
      id: 900101,
      clasificacion: 'sana',
      confianza_gate: 0.96,
      probabilidades: { sana: 0.91, antracnosis: 0.06, scab: 0.03 },
      sensor: { temp: 22.4, humedad: 67, r: 74, g: 126, b: 52 },
    },
    {
      id: 900102,
      clasificacion: 'antracnosis',
      confianza_gate: 0.94,
      probabilidades: { sana: 0.12, antracnosis: 0.78, scab: 0.10 },
      sensor: { temp: 23.1, humedad: 69, r: 122, g: 74, b: 48 },
    },
  ]
  capturas.value = [
    { key: 'demo-1', url: demoImageUrl },
    { key: 'demo-2', url: demoImageUrl },
    { key: 'demo-3', url: demoImageUrl },
  ]
}

// ── Pie chart: DISTRIBUCIÓN del lote (composición del total) ──────────────────
const CLASES_PIE = [
  { key: 'sana',        label: 'Sana',        color: 'var(--pasa)' },
  { key: 'antracnosis', label: 'Antracnosis', color: 'var(--antrac)' },
  { key: 'scab',        label: 'Scab',        color: 'var(--scab)' },
  { key: 'no_es_palta', label: 'No palta',    color: 'var(--gris)' },
]
const distribucion = computed(() => {
  const total = store.paltas.length
  const cnt = { sana: 0, antracnosis: 0, scab: 0, no_es_palta: 0 }
  for (const p of store.paltas) if (p.clasificacion in cnt) cnt[p.clasificacion]++
  return CLASES_PIE.map(c => ({
    ...c, n: cnt[c.key],
    pct: total ? Math.round(cnt[c.key] / total * 100) : 0,
    frac: total ? cnt[c.key] / total : 0,
  }))
})
const R = 54
const CIRC = 2 * Math.PI * R
const donut = computed(() => {
  let acc = 0
  const segs = []
  for (const d of distribucion.value) {
    if (d.n <= 0) continue
    const len = Math.max(0, d.frac * CIRC - 2)   // -2px: separación entre porciones
    segs.push({ color: d.color, dash: len, gap: CIRC - len, offset: -acc * CIRC })
    acc += d.frac
  }
  return segs
})

// ── Radar: DISTRIBUCIÓN del lote (estilo Planto "Plant details"), 4 ejes ──────
// Cada eje = una clase; la DISTANCIA del vértice al centro = % del lote en esa
// clase; el número junto al vértice = cuántas paltas. Así se lee de un vistazo.
const RCX = 120, RCY = 98, RMAX = 66
const RADAR_ANGLE = { sana: -90, scab: 0, antracnosis: 90, no_es_palta: 180 }  // grados
// Paleta pastel "Planto" (suave) SOLO para este radar — no toca los colores semánticos
const RADAR_COLOR = { sana: '#7cc08d', scab: '#e6b45e', antracnosis: '#e08d7f', no_es_palta: '#a9b0ad' }
const radarColorOf = (k) => RADAR_COLOR[k] || 'var(--borde-cards)'
const _rxy = (deg, r) => {
  const a = deg * Math.PI / 180
  return [RCX + r * Math.cos(a), RCY + r * Math.sin(a)]
}
const radar = computed(() => {
  const items = distribucion.value
  const axes = items.map((d) => {
    const deg = RADAR_ANGLE[d.key]
    const rad = deg * Math.PI / 180
    const r = (d.pct / 100) * RMAX
    const x = RCX + Math.cos(rad) * r, y = RCY + Math.sin(rad) * r
    // número (conteo): sobre el vértice, empujado hacia afuera; radio mínimo para
    // que los valores 0 no se amontonen todos en el centro
    const rl = Math.max(r, 0.18 * RMAX) + 12
    const vx = RCX + Math.cos(rad) * rl, vy = RCY + Math.sin(rad) * rl
    // extremo del eje + posición del nombre del eje
    const ex = RCX + Math.cos(rad) * RMAX, ey = RCY + Math.sin(rad) * RMAX
    let lx = ex, ly = ey, anchor = 'middle'
    if (deg === -90) { ly = ey - 13 }
    else if (deg === 90) { ly = ey + 15 }
    else if (deg === 0) { lx = ex + 7; anchor = 'start' }
    else { lx = ex - 7; anchor = 'end' }
    return { key: d.key, label: d.label, n: d.n, color: RADAR_COLOR[d.key], x, y, vx, vy, ex, ey, lx, ly, anchor }
  })
  const polygon = axes.map(a => a.x.toFixed(1) + ',' + a.y.toFixed(1)).join(' ')
  const rings = [0.25, 0.5, 0.75, 1].map(f =>
    [-90, 0, 90, 180].map(a => { const [x, y] = _rxy(a, f * RMAX); return x.toFixed(1) + ',' + y.toFixed(1) }).join(' ')
  )
  return { polygon, axes, rings }
})
// firma para re-disparar la animación cuando cambia el conteo
const radarKey = computed(() => distribucion.value.map(d => d.n).join('-'))

// posición X (%) de cada foto en la línea de tiempo (deja aire a la derecha para el pill)
const growthLeft = (i, n) => (n > 1 ? 9 + i * (66 / (n - 1)) : 12)

// veredicto de la palta actual → globo sobre la última foto (solo enferma / no palta)
const capturaVerdict = computed(() => {
  const c = ultima.value?.clasificacion
  if (c === 'no_es_palta') return { label: 'No Palta', tipo: 'nopalta' }
  if (c === 'antracnosis' || c === 'scab') return { label: 'Enferma', tipo: 'enferma' }
  return null   // sana → sin globo
})

// ── "Soil moisture" 3D: 4 barras isométricas de la palta actual ───────────────
const SOIL_DEFS = [
  { key: 'palta',  label: 'Es palta',    get: p => gateCerteza(p), c: { front: '#6bb6c4', top: '#8bcdd8', side: '#4f9aa8' } },
  { key: 'sana',   label: 'Sana',        get: p => sanaPct(p),     c: { front: '#7cc08d', top: '#9ad3a8', side: '#5fa876' } },
  { key: 'antrac', label: 'Antracnosis', get: p => antracPct(p),   c: { front: '#e08d7f', top: '#eaa89c', side: '#c7735f' } },
  { key: 'scab',   label: 'Scab',        get: p => scabPct(p),     c: { front: '#e6b45e', top: '#f0c983', side: '#cf9a3e' } },
]
const SOIL_W = 420, SOIL_BASE = 155, SOIL_TOP = 36, SOIL_BARW = 48, SOIL_DEPTH = 14
const SOIL_MAXH = SOIL_BASE - SOIL_TOP   // altura de la columna completa (track)
const soil = computed(() => {
  const p = ultima.value
  if (!p) return []
  const slot = SOIL_W / SOIL_DEFS.length
  const d = SOIL_DEPTH, w = SOIL_BARW, base = SOIL_BASE
  // caja isométrica (front/top/side) de una columna que arranca en y0 y baja a la base
  const box = (x, y0) => ({
    front: `${x},${y0} ${x + w},${y0} ${x + w},${base} ${x},${base}`,
    top:   `${x},${y0} ${x + d},${y0 - d} ${x + w + d},${y0 - d} ${x + w},${y0}`,
    side:  `${x + w},${y0} ${x + w + d},${y0 - d} ${x + w + d},${base - d} ${x + w},${base}`,
  })
  return SOIL_DEFS.map((b, i) => {
    const pct = Math.max(0, Math.min(100, Math.round(b.get(p) || 0)))
    const label = b.key === 'palta' ? (esExpulsada(p.clasificacion) ? 'No palta' : 'Es palta') : b.label
    const x = i * slot + (slot - w - d) / 2       // -d/2: compensa la profundidad 3D
    const fillTopY = base - (pct / 100) * SOIL_MAXH
    return {
      key: b.key, label, c: b.c, i, pct, cx: x + (w + d) / 2, valY: SOIL_TOP - d - 4,
      ghost: box(x, SOIL_TOP),   // columna completa translúcida (track)
      fill: box(x, fillTopY),    // relleno sólido desde abajo según el %
    }
  })
})

// ── Sensores ─────────────────────────────────────────────
const rgbColor = (s) => (s && s.r != null && s.g != null && s.b != null)
  ? `rgb(${s.r}, ${s.g}, ${s.b})` : 'var(--borde-cards)'
const rgbText = (s) => (s && s.r != null) ? `${s.r},${s.g},${s.b}` : '—'
const colorEstado = (s) => {
  if (!s || s.r == null || s.g == null || s.b == null) return 'Sin lectura'
  const max = Math.max(s.r, s.g, s.b)
  const min = Math.min(s.r, s.g, s.b)
  const brightness = (s.r + s.g + s.b) / 3
  if (brightness < 45) return 'Muy oscuro'
  if (brightness > 215 && max - min < 35) return 'Muy claro'
  if (s.g >= s.r + 18 && s.g >= s.b + 12) return 'Verde'
  if (s.r >= s.g + 18 && s.g >= s.b) return 'Marrón / daño'
  if (s.r >= s.g + 20 && s.b >= s.g + 10) return 'Rojizo'
  if (max - min < 28) return 'Neutro'
  return 'Mixto'
}

// ── Foto en vivo (fotos de la ÚLTIMA fruta, sus 3 vueltas) ───────────────────
const capturas = ref([])
async function cargarCapturas() {
  if (demoMode) {
    if (!capturas.value.length) cargarDemo()
    return
  }
  const id = store.ultimaPalta?.id
  if (!id) { capturas.value = []; return }
  try {
    const { data } = await axios.get(`/api/palta/${id}/fotos`)
    if (store.ultimaPalta?.id !== id) return   // cambió la palta: descarta el resultado viejo
    capturas.value = (data.fotos || []).map((u, i) => ({ key: id + '-' + i, url: apiBase + u }))
  } catch {
    if (store.ultimaPalta?.id === id) capturas.value = []
  }
}
const indiceSeleccionado = ref(null)
const fotoUrl = computed(() => {
  if (!capturas.value.length) return null
  const i = indiceSeleccionado.value
  return capturas.value[i != null && capturas.value[i] ? i : capturas.value.length - 1].url
})
watch(() => store.ultimaPalta?.id, () => { capturas.value = []; indiceSeleccionado.value = null; cargarCapturas() })
let capturasInterval = null
onMounted(() => { cargarCapturas(); capturasInterval = setInterval(cargarCapturas, 2000) })
onUnmounted(() => clearInterval(capturasInterval))

// ── Acciones ─────────────────────────────────────────────
async function abrirLote() {
  if (!nuevoCodigo.value.trim()) return
  await store.abrirLote(nuevoCodigo.value.trim())
  nuevoCodigo.value = ''
}

function abrirConfirmacionCierre() {
  if (!store.loteActivo?.id) return
  confirmarCierre.value = true
}

async function confirmarCerrarLote() {
  if (!store.loteActivo?.id) return
  await store.cerrarLote(store.loteActivo.id)
  confirmarCierre.value = false
}

async function cerrarLote() {
  if (!store.loteActivo?.id) return
  if (!window.confirm('¿Cerrar el lote actual? Esto detiene la línea.')) return
  await store.cerrarLote(store.loteActivo.id)
}
</script>

<style scoped>
.op-root {
  --pasa:   var(--verde-acento);
  --pasa-ink:#2f7d32;
  --antrac: var(--rojo-rechazo);
  --scab:   #E08A00;
  --scab-bg:#fbf0dc;
  --gris:   #8a929b;
  --page:   var(--fondo-pagina);
  display: flex; flex-direction: column; min-height: 100%;
  background: var(--page); overflow: auto;
}
.num { font-variant-numeric: tabular-nums; }

/* Sin lote */
.no-lote { flex:1; display:flex; align-items:center; justify-content:center; padding:24px; }
.no-lote-card { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:8px; padding:36px 32px; text-align:center; max-width:440px; width:100%; box-shadow:var(--shadow-card); }
.no-lote-card h2 { margin:16px 0 8px; font-size:1.5rem; }
.no-lote-card p { color:var(--texto-secondary); margin-bottom:24px; }
.no-lote-form { display:flex; flex-direction:column; gap:12px; }
.no-lote-form input { padding:14px; border:1px solid var(--borde-cards); border-radius:10px; font-size:1.05rem; outline:none; }
.no-lote-form input:focus-visible { border-color:var(--pasa); box-shadow:0 0 0 3px rgba(92,184,92,.25); }
.error-msg { color:var(--antrac); font-size:.9rem; margin-top:8px; }

/* Cabecera */
.op-head { position:relative; display:flex; align-items:center; justify-content:space-between; gap:12px; padding:12px 18px; min-height:64px; background:var(--fondo-cards); border-bottom:1px solid var(--borde-cards); flex-shrink:0; flex-wrap:wrap; }
.head-left { display:flex; align-items:center; gap:16px; }
.head-lote { font-size:1.05rem; } .head-lote b { font-family:'Courier New',monospace; }
.head-timer { font-family:'Courier New',monospace; color:var(--texto-secondary); }

/* "Ultima decision" reubicada: pill compacto centrado en el header */
.head-decision { position:absolute; left:50%; top:50%; transform:translate(-50%,-50%); display:flex; align-items:center; gap:10px; border-radius:10px; padding:6px 18px; }
.head-decision .verdict-ico { font-size:1.5rem; line-height:1; }
.head-decision .verdict-text { display:flex; flex-direction:column; }
.head-decision .verdict-label { font-size:1.2rem; font-weight:900; line-height:1.05; }
.head-decision .verdict-sub { font-size:.78rem; font-weight:600; opacity:.9; }

/* (A) 4 tarjetas */
.cards3 { display:grid; grid-template-columns:repeat(4,1fr); gap:10px; padding:12px 18px 0; }
.card3 { border-radius:8px; padding:12px 16px; color:#fff; display:grid; grid-template-columns:28px 1fr; grid-template-areas:"icon num" "icon label"; align-items:center; column-gap:12px; min-height:86px; box-shadow:var(--shadow-card); }
.c3-icon { grid-area:icon; opacity:.95; }
.c3-num { grid-area:num; font-size:2.6rem; font-weight:900; line-height:.95; font-variant-numeric:tabular-nums; }
.c3-lbl { grid-area:label; font-size:.78rem; font-weight:800; opacity:1; text-transform:uppercase; letter-spacing:.02em; }
/* Paleta "Planto" (verdes apagados/sage). Texto blanco AA-seguro (contraste calculado). */
.card3--total { background:#32544a; }   /* pino profundo   (blanco 8.39) */
.card3--sana  { background:#3c7d54; }   /* verde bosque    (blanco 4.94) */
.card3--enf   { background:#b2574a; }   /* terracota suave (blanco 4.82) */
.card3--exp   { background:#6b7a70; }   /* salvia / sage   (blanco 4.52) */

/* Grid 2 columnas */
.grid2 { flex:1; display:grid; grid-template-columns:minmax(420px,1fr) minmax(420px,1fr); gap:12px; padding:12px 18px 18px; align-items:stretch; min-height:0; }
.col { display:flex; flex-direction:column; gap:12px; min-height:0; }
.card { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:8px; box-shadow:var(--shadow-soft); padding-bottom:12px; min-height:0; }
.card-title { font-size:.86rem; font-weight:800; padding:12px 16px 8px; color:var(--texto-primary); text-transform:uppercase; letter-spacing:.02em; }

/* Sensores — estilo "Weather" Planto (info centrada dentro de burbujas) */
.sensor-weather { min-width:0; }   /* ocupa la mitad de la columna izquierda */

.sw-panel {
  position:relative;
  margin:6px 14px 8px;
  aspect-ratio:1 / 0.9;      /* casi cuadrado, como la card Weather */
  min-height:236px;
  border-radius:20px;
  overflow:hidden;
  color:#fff;
  /* Burbujas chiquitas de tamaños variados, RODEANDO cada círculo (incl. lado izq),
     pegadas entre sí sin superponerse (distancia = radio_grande + radio_chica) + base verde */
  background:
    /* --- alrededor de HUMEDAD --- */
    radial-gradient(circle at 5%  28%, rgba(255,255,255,.06) 0 12px, transparent 13px),
    radial-gradient(circle at 13% 9%,  rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 47% 9%,  rgba(255,255,255,.06) 0 11px, transparent 12px),
    radial-gradient(circle at 13% 47%, rgba(255,255,255,.05) 0 12px, transparent 13px),
    radial-gradient(circle at 48% 47%, rgba(0,0,0,.05)       0 10px, transparent 11px),
    radial-gradient(circle at 5%  13%, rgba(255,255,255,.05) 0 8px,  transparent 9px),
    radial-gradient(circle at 5%  40%, rgba(0,0,0,.05)       0 9px,  transparent 10px),
    /* --- alrededor de MIXTO/COLOR --- */
    radial-gradient(circle at 7%  76%, rgba(255,255,255,.06) 0 12px, transparent 13px),
    radial-gradient(circle at 14% 58%, rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 14% 94%, rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 46% 94%, rgba(255,255,255,.06) 0 12px, transparent 13px),
    radial-gradient(circle at 53% 76%, rgba(0,0,0,.05)       0 12px, transparent 13px),
    radial-gradient(circle at 46% 58%, rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 5%  67%, rgba(0,0,0,.05)       0 9px,  transparent 10px),
    radial-gradient(circle at 6%  85%, rgba(255,255,255,.05) 0 8px,  transparent 9px),
    /* --- alrededor de TEMPERATURA --- */
    radial-gradient(circle at 96% 46%, rgba(255,255,255,.06) 0 12px, transparent 13px),
    radial-gradient(circle at 90% 29%, rgba(255,255,255,.06) 0 14px, transparent 15px),
    radial-gradient(circle at 75% 23%, rgba(0,0,0,.05)       0 12px, transparent 13px),
    radial-gradient(circle at 60% 30%, rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 55% 46%, rgba(255,255,255,.05) 0 10px, transparent 11px),
    radial-gradient(circle at 60% 62%, rgba(0,0,0,.05)       0 11px, transparent 12px),
    radial-gradient(circle at 75% 69%, rgba(255,255,255,.05) 0 12px, transparent 13px),
    radial-gradient(circle at 90% 63%, rgba(255,255,255,.06) 0 13px, transparent 14px),
    /* --- rellenos de tamaños variados en esquinas/centro --- */
    radial-gradient(circle at 95% 13%, rgba(255,255,255,.07) 0 18px, transparent 19px),
    radial-gradient(circle at 60% 7%,  rgba(0,0,0,.05)       0 13px, transparent 14px),
    radial-gradient(circle at 97% 37%, rgba(255,255,255,.05) 0 9px,  transparent 10px),
    radial-gradient(circle at 95% 87%, rgba(255,255,255,.06) 0 16px, transparent 17px),
    radial-gradient(circle at 60% 90%, rgba(0,0,0,.05)       0 14px, transparent 15px),
    radial-gradient(circle at 86% 79%, rgba(255,255,255,.05) 0 11px, transparent 12px),
    radial-gradient(circle at 9%  54%, rgba(255,255,255,.05) 0 6px,  transparent 7px),
    linear-gradient(150deg, #40604e 0%, #2f463a 100%);
  box-shadow: inset 0 1px 0 rgba(255,255,255,.08), 0 6px 16px rgba(0,0,0,.12);
}

/* Burbujas GRANDES: cada lectura es un círculo con la info centrada */
.sw-item {
  position:absolute;
  transform:translate(-50%,-50%);
  border-radius:50%;
  display:flex; flex-direction:column; align-items:center; justify-content:center;
  text-align:center; gap:3px;
  background:radial-gradient(circle at 50% 40%, rgba(255,255,255,.13), rgba(255,255,255,.05) 62%, rgba(255,255,255,0) 100%);
  box-shadow: inset 0 0 0 1px rgba(255,255,255,.10), inset 0 2px 12px rgba(255,255,255,.05);
}
.sw-val { font-size:2rem; font-weight:800; line-height:1; }
.sw-val-sm { font-size:1.05rem; font-weight:800; line-height:1; }
.sw-lbl { font-size:.68rem; font-weight:600; color:rgba(255,255,255,.75); text-transform:uppercase; letter-spacing:.02em; }
.sw-item--hum   { left:30%; top:28%; width:140px; height:140px; }
.sw-item--temp  { left:75%; top:46%; width:116px; height:116px; }
.sw-item--color { left:30%; top:76%; width:130px; height:130px; }
.sw-item--color .sw-lbl { font-size:.62rem; }
.sw-swatch { width:40px; height:20px; border-radius:6px; border:1px solid rgba(255,255,255,.4); margin-bottom:3px; box-shadow:0 1px 3px rgba(0,0,0,.28); }

/* Pie / donut */
.pie-wrap { display:flex; align-items:center; gap:14px; padding:0 16px; flex-wrap:wrap; }
.donut { width:136px; height:136px; flex-shrink:0; }
.donut circle { transition:stroke-dasharray .4s ease, stroke-dashoffset .4s ease; }
.donut-total { font-size:26px; font-weight:800; fill:var(--texto-primary); }
.donut-sub { font-size:11px; fill:var(--texto-secondary); }
.pie-legend { list-style:none; display:flex; flex-direction:column; gap:8px; flex:1; min-width:160px; }
.pie-legend li { display:grid; grid-template-columns:14px 1fr auto auto; align-items:center; gap:8px; font-size:.9rem; }
.pie-dot { width:14px; height:14px; border-radius:4px; }
.pie-name { color:var(--texto-primary); }
.pie-pct { color:var(--texto-secondary); min-width:40px; text-align:right; }

/* Fila superior: Sensores + Radar lado a lado, CENTRADOS y a la MISMA altura
   (deja espacio libre debajo) */
.top-row { display:flex; flex-wrap:wrap; gap:12px; align-items:stretch; }
.top-row > .card { flex:1 1 0; min-width:0; }   /* Sensores y Distribución: mitad y mitad */

/* Radar "Distribución del lote" (estilo Planto "Plant details"), animado */
.radar-wrap { padding:2px 12px 12px; }
.radar { width:100%; height:auto; display:block; overflow:visible; }
.radar-frame { fill:rgba(240,209,203,.35); stroke:none; }        /* diamante pastel exterior (Planto) */
.radar-ring  { fill:none; stroke:rgba(150,140,140,.22); stroke-width:1; }
.radar-spoke { stroke:rgba(150,140,140,.18); stroke-width:1; }
.radar-axis-lbl { fill:var(--texto-secondary); font-size:10px; font-weight:600; }
.radar-val { fill:var(--texto-primary); font-size:10px; font-weight:800; }
.radar-area { fill:rgba(124,192,141,.45); stroke:#6fae86; stroke-width:2; stroke-linejoin:round; }
.radar-dot  { stroke:#fff; stroke-width:1.4; }
.radar-data { transform-box:view-box; transform-origin:120px 98px; animation:radarPop .85s cubic-bezier(.22,.7,.24,1) both; }
@keyframes radarPop { 0% { transform:scale(0); opacity:0; } 70% { opacity:1; } 100% { transform:scale(1); opacity:1; } }
.radar-legend { list-style:none; margin:0; padding:8px 14px 0; display:flex; flex-wrap:wrap; gap:6px 14px; }
.radar-legend li { display:flex; align-items:center; gap:7px; font-size:.82rem; width:calc(50% - 7px); }
.radar-legend .pie-name { flex:1; color:var(--texto-primary); }
.radar-legend .pie-pct { color:var(--texto-secondary); min-width:36px; text-align:right; }

/* (D) Línea de fotos de la palta — estilo Planto "Plant growth activity" (escalera) */
.growth-card { width:100%; flex:0 0 auto; }   /* Capturas: ancho completo de la columna */
.growth { position:relative; height:232px; margin:6px 8px 0; }
.growth-track { position:static; }
.growth::after {                        /* regla horizontal al FONDO con marcas verticales */
  content:""; position:absolute; left:14px; right:14px; bottom:20px; height:10px;
  border-bottom:2px solid var(--borde-cards);
  background-image:repeating-linear-gradient(90deg, var(--borde-cards) 0 1.5px, transparent 1.5px 13px);
  background-repeat:no-repeat; background-position:left bottom; background-size:100% 8px;
}
.growth-node {                          /* X por inline; sube en escalera con --i */
  position:absolute; bottom:20px; margin-left:-26px;
  display:flex; flex-direction:column; align-items:center;
}
.growth-thumb { width:52px; height:52px; border-radius:50%; object-fit:cover; background:#20242a;
  border:3px solid var(--fondo-cards); box-shadow:0 3px 9px rgba(0,0,0,.22), 0 0 0 1px var(--borde-cards);
  position:relative; z-index:2; }
.growth-pill {                          /* cápsula "pegada" a la derecha de la foto */
  position:absolute; left:38px; top:26px; transform:translateY(-50%);
  display:flex; flex-direction:column; justify-content:center; gap:1px;
  padding:7px 15px 7px 24px; border-radius:999px; white-space:nowrap; z-index:1;
  box-shadow:0 3px 8px rgba(0,0,0,.12); }
.growth-node:nth-child(odd)  .growth-pill { background:#eef0dd; color:#33513f; }   /* crema */
.growth-node:nth-child(even) .growth-pill { background:#33513f; color:#fff; }        /* verde */
.growth-pill-title { font-size:.82rem; font-weight:800; line-height:1.12; }
.growth-pill-sub   { font-size:.66rem; font-weight:600; opacity:.82; line-height:1.12; }
.growth-stem { width:2px; height:calc(24px + var(--i,0) * 32px); background:var(--borde-cards); }
.growth-dot  { width:11px; height:11px; border-radius:50%; background:#4a8f5a;
  border:2px solid var(--fondo-cards); box-shadow:0 0 0 1px var(--borde-cards); margin-bottom:-6px; }
.growth-empty { position:absolute; inset:0; display:flex; align-items:center; justify-content:center;
  color:var(--texto-secondary); font-size:.85rem; }

/* Globo de veredicto sobre la última foto (estilo Planto "5.10 cm") */
.gbubble { position:absolute; bottom:100%; left:50%; transform:translateX(-50%); margin-bottom:10px;
  padding:4px 13px; border-radius:999px; font-size:.72rem; font-weight:800; color:#fff; white-space:nowrap;
  box-shadow:0 3px 8px rgba(0,0,0,.2); z-index:3; animation:gbubblePop .4s cubic-bezier(.2,.7,.2,1) both; }
.gbubble::after { content:""; position:absolute; top:100%; left:50%; transform:translateX(-50%);
  border:5px solid transparent; }
.gbubble--enferma { background:#b2574a; }               /* rojo (igual que la card Enfermas) */
.gbubble--enferma::after { border-top-color:#b2574a; }
.gbubble--nopalta { background:#6f7882; }               /* plomo */
.gbubble--nopalta::after { border-top-color:#6f7882; }
@keyframes gbubblePop { 0% { opacity:0; transform:translate(-50%,6px) scale(.85); }
  100% { opacity:1; transform:translate(-50%,0) scale(1); } }

/* animación: cada foto aparece al tomarse */
.growth-enter-active { transition:opacity .5s ease, transform .5s cubic-bezier(.2,.7,.2,1); transition-delay:var(--d,0s); }
.growth-enter-from { opacity:0; transform:translateY(16px) scale(.9); }
.growth-leave-active { transition:opacity .3s ease; }
.growth-leave-to { opacity:0; }

/* Estadísticas de la palta — 4 barras 3D (estilo Planto "Soil moisture") */
.soil-block { flex:1; display:flex; flex-direction:column; justify-content:center; }
.soil { align-self:center; width:100%; max-width:640px; height:auto; display:block; margin-top:8px; overflow:visible; }
.soil-base { stroke:var(--borde-cards); stroke-width:1.5; }
.soil-ghost { opacity:.16; }                 /* columna completa translúcida (track) */
.soil-val { fill:var(--texto-primary); font-size:13px; font-weight:800; }
.soil-lbl { fill:var(--texto-secondary); font-size:12px; font-weight:600; }
.soil-fill { transform-box:fill-box; transform-origin:center bottom;
  animation:soilGrow .7s cubic-bezier(.2,.7,.2,1) both; animation-delay:calc(var(--i,0) * .09s); }
@keyframes soilGrow { from { transform:scaleY(0); } to { transform:scaleY(1); } }

/* Última fruta */

.decision-card { padding-bottom:12px; }
.decision-card .verdict { min-height:74px; }

.ultima { border-top:4px solid transparent; display:flex; flex-direction:column; min-height:100%; }
.edge--pasa       { border-top-color:var(--pasa); }
.edge--antracnosis{ border-top-color:var(--antrac); }
.edge--scab       { border-top-color:var(--scab); }
.edge--expulsada  { border-top-color:var(--gris); }
.verdict { margin:0 16px; border-radius:8px; padding:10px 14px; display:flex; align-items:center; gap:12px; }
.verdict-ico { font-size:2rem; line-height:1; }
.verdict-text { display:flex; flex-direction:column; }
.verdict-label { font-size:1.65rem; font-weight:900; line-height:1.05; }
.verdict-sub { font-size:.9rem; font-weight:600; opacity:.9; }
.v--pasa       { background:rgba(92,184,92,.14); color:var(--pasa-ink); border:2px solid rgba(92,184,92,.35); }
.v--antracnosis{ background:rgba(217,83,79,.13); color:var(--antrac);  border:2px solid rgba(217,83,79,.35); }
.v--scab       { background:rgba(224,138,0,.14);  color:var(--scab);    border:2px solid rgba(224,138,0,.32); }
.v--expulsada  { background:rgba(138,146,155,.14); color:var(--texto-secondary); border:2px solid rgba(138,146,155,.3); }
.v--idle       { background:var(--fondo-soft);     color:var(--texto-secondary); border:2px solid var(--borde-cards); }

.foto-wrap { margin:10px 16px 0; border-radius:8px; overflow:hidden; background:#20242a; border:1px solid var(--borde-cards); }
.foto { display:block; width:100%; height:clamp(230px, 32vh, 360px); object-fit:contain; background:#20242a; }
.foto-empty { height:clamp(230px, 32vh, 360px); display:flex; align-items:center; justify-content:center; color:#8b939c; font-size:1.4rem; letter-spacing:.15em; }
.foto-thumbs { display:flex; align-items:center; gap:8px; padding:8px 16px 0; flex-wrap:wrap; min-height:22px; }
.foto-thumb { width:58px; height:58px; object-fit:contain; background:#20242a; border-radius:8px; border:1px solid var(--borde-cards); cursor:pointer; }
.foto-thumb:hover { border-color:var(--pasa); }
.foto-thumb--sel { border-color:var(--pasa); box-shadow:0 0 0 2px rgba(92,184,92,.35); }
.foto-thumbs-lbl { font-size:.75rem; color:var(--texto-secondary); }

.block { margin:12px 16px 0; }
.block-title { display:block; font-size:.8rem; font-weight:700; color:var(--texto-secondary); margin-bottom:8px; text-transform:uppercase; letter-spacing:.03em; }
.block-head { display:flex; justify-content:space-between; font-size:.9rem; margin-bottom:6px; }
.bar { height:10px; background:var(--borde-cards); border-radius:5px; overflow:hidden; }
.bar-fill { height:100%; border-radius:5px; transition:width .4s ease; }
.fill--verde { background:var(--pasa); } .fill--rojo { background:var(--antrac); } .fill--ambar { background:var(--scab); } .fill--gris { background:var(--gris); }
.prob-row { display:grid; grid-template-columns:84px 1fr 44px; align-items:center; gap:8px; margin-bottom:7px; }
.prob-lbl { font-size:.82rem; color:var(--texto-secondary); }
.prob-val { font-size:.82rem; text-align:right; font-weight:700; }

.empty-state { flex:1; min-height:260px; padding:26px; display:flex; align-items:center; justify-content:center; text-align:center; color:var(--texto-secondary); }

/* Botones (área táctil ≥44px) */
.btn { min-height:44px; padding:10px 20px; border-radius:10px; border:none; font-size:.95rem; font-weight:700; transition:filter .15s, opacity .15s; }
.btn:disabled { opacity:.6; cursor:default; }
.btn:focus-visible { outline:3px solid rgba(92,184,92,.5); outline-offset:2px; }
.btn--lg { font-size:1.05rem; }
.btn--primary { background:var(--pasa); color:#fff; }
.btn--danger { background:var(--antrac); color:#fff; }
.btn--ghost { background:var(--fondo-soft); color:var(--texto-primary); border:1px solid var(--borde-cards); }
.btn--danger:hover, .btn--primary:hover:not(:disabled) { filter:brightness(.95); }

.confirm-overlay {
  position: fixed;
  inset: 0;
  z-index: 500;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 18px;
  background: rgba(0,0,0,.45);
}

.confirm-dialog {
  width: min(460px, 100%);
  background: var(--fondo-cards);
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  box-shadow: var(--shadow-card);
  padding: 20px;
}

.confirm-dialog h2 {
  font-size: 1.15rem;
  margin-bottom: 10px;
  color: var(--texto-primary);
}

.confirm-main {
  font-size: .95rem;
  color: var(--texto-primary);
  margin-bottom: 8px;
}

.confirm-copy {
  color: var(--texto-secondary);
  font-size: .88rem;
  line-height: 1.45;
}

.confirm-stats {
  display: grid;
  grid-template-columns: repeat(3,1fr);
  gap: 8px;
  margin: 16px 0;
}

.confirm-stats span {
  background: var(--fondo-soft);
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  padding: 10px;
  text-align: center;
  color: var(--texto-secondary);
  font-size: .78rem;
}

.confirm-stats b {
  display: block;
  color: var(--texto-primary);
  font-size: 1.3rem;
}

.confirm-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

/* Responsive */
@media (max-width:900px) {
  .grid2 { grid-template-columns:1fr; align-items:start; }
  .cards3 { grid-template-columns:repeat(2,1fr); }
  .ultima { min-height:auto; }
}
@media (max-width:560px) {
  .cards3 { grid-template-columns:1fr; }
}
</style>
