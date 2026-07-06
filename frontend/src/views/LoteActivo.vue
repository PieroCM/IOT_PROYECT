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
        <button class="btn btn--danger" @click="cerrarLote">Cerrar lote</button>
      </header>

      <!-- (A) 3 tarjetas -->
      <div class="cards3">
        <div class="card3 card3--total"><span class="c3-num">{{ conteo.total }}</span><span class="c3-lbl">N° Paltas</span></div>
        <div class="card3 card3--sana"><span class="c3-num">{{ conteo.pasa }}</span><span class="c3-lbl">✓ Sanas</span></div>
        <div class="card3 card3--enf"><span class="c3-num">{{ conteo.enferma }}</span><span class="c3-lbl">⚠ Enfermas</span></div>
      </div>

      <div class="grid2">
        <!-- IZQUIERDA -->
        <div class="col">
          <!-- (B) Sensores -->
          <section class="card">
            <h3 class="card-title">Sensores · última fruta</h3>
            <div class="sensor-grid">
              <div class="sensor-tile">
                <span class="sensor-val num">{{ ultima?.sensor?.temp != null ? ultima.sensor.temp.toFixed(0) + '°' : '—' }}</span>
                <span class="sensor-lbl">🌡️ Temperatura</span>
              </div>
              <div class="sensor-tile">
                <span class="sensor-val num">{{ ultima?.sensor?.humedad != null ? ultima.sensor.humedad.toFixed(0) + '%' : '—' }}</span>
                <span class="sensor-lbl">💧 Humedad</span>
              </div>
              <div class="sensor-tile">
                <span class="sensor-swatch" :style="{ background: rgbColor(ultima?.sensor) }"></span>
                <span class="sensor-rgb num">{{ rgbText(ultima?.sensor) }}</span>
                <span class="sensor-lbl">RGB</span>
              </div>
            </div>
          </section>

          <!-- (C) Modelo de enfermedad: PIE (distribución del lote) -->
          <section class="card">
            <h3 class="card-title">Modelo de enfermedad · distribución del lote</h3>
            <div class="pie-wrap">
              <svg viewBox="0 0 140 140" class="donut" role="img" aria-label="Distribución de clases del lote">
                <circle cx="70" cy="70" :r="R" fill="none" stroke="var(--borde-cards)" stroke-width="18" />
                <circle v-for="(s, i) in donut" :key="i" cx="70" cy="70" :r="R" fill="none"
                        :stroke="s.color" stroke-width="18"
                        :stroke-dasharray="s.dash + ' ' + s.gap" :stroke-dashoffset="s.offset"
                        transform="rotate(-90 70 70)" stroke-linecap="butt" />
                <text x="70" y="66" text-anchor="middle" class="donut-total num">{{ conteo.total }}</text>
                <text x="70" y="86" text-anchor="middle" class="donut-sub">paltas</text>
              </svg>
              <ul class="pie-legend">
                <li v-for="d in distribucion" :key="d.key">
                  <span class="pie-dot" :style="{ background: d.color }"></span>
                  <span class="pie-name">{{ d.label }}</span>
                  <b class="num">{{ d.n }}</b>
                  <span class="pie-pct num">{{ d.pct }}%</span>
                </li>
              </ul>
            </div>
          </section>
        </div>

        <!-- DERECHA -->
        <div class="col">
          <section class="card ultima" :class="ultima ? 'edge--' + veredicto(ultima.clasificacion).tipo : ''">
            <h3 class="card-title">Última fruta</h3>
            <template v-if="ultima">
              <!-- Veredicto -->
              <div class="verdict" :class="'v--' + veredicto(ultima.clasificacion).tipo">
                <span class="verdict-ico">{{ verIco(ultima.clasificacion) }}</span>
                <div class="verdict-text">
                  <span class="verdict-label">{{ veredicto(ultima.clasificacion).label }}</span>
                  <span class="verdict-sub">{{ veredicto(ultima.clasificacion).sub }}</span>
                </div>
              </div>
              <!-- IMG -->
              <div class="foto-wrap">
                <img v-if="fotoUrl" :src="fotoUrl" alt="Fruta" class="foto" />
                <div v-else class="foto-empty">📷 IMG</div>
              </div>
              <!-- Miniaturas del ciclo (3 vueltas) -->
              <div class="foto-thumbs">
                <img v-for="(c, i) in capturas" :key="c.key" :src="c.url"
                     class="foto-thumb" :class="{ 'foto-thumb--sel': i === capturas.length - 1 }"
                     :title="'Foto ' + (i + 1)" :alt="'Foto ' + (i + 1)" />
                <span v-if="!capturas.length" class="foto-thumbs-lbl">esperando fotos…</span>
              </div>
              <!-- Es palta (gate) -->
              <div class="block">
                <div class="block-head">
                  <span>{{ esExpulsada(ultima.clasificacion) ? 'No es palta' : 'Es palta' }}</span>
                  <span class="num">{{ gateCerteza(ultima) != null ? gateCerteza(ultima) + '%' : '—' }}</span>
                </div>
                <div class="bar"><div class="bar-fill" :class="esExpulsada(ultima.clasificacion) ? 'fill--gris' : 'fill--verde'"
                     :style="{ width: (gateCerteza(ultima) ?? 0) + '%' }"></div></div>
              </div>
              <!-- Probabilidad por clase (barras) de la palta actual -->
              <div class="block" v-if="ultima.probabilidades">
                <span class="block-title">Probabilidad de enfermedad (esta palta)</span>
                <div class="prob-row"><span class="prob-lbl">Sana</span>
                  <div class="bar"><div class="bar-fill fill--verde" :style="{ width: sanaPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ sanaPct(ultima) }}%</span></div>
                <div class="prob-row"><span class="prob-lbl">Antracnosis</span>
                  <div class="bar"><div class="bar-fill fill--rojo" :style="{ width: antracPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ antracPct(ultima) }}%</span></div>
                <div class="prob-row"><span class="prob-lbl">Scab</span>
                  <div class="bar"><div class="bar-fill fill--ambar" :style="{ width: scabPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ scabPct(ultima) }}%</span></div>
              </div>
            </template>
            <div v-else class="empty-state">Esperando la primera fruta…</div>
          </section>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import axios from 'axios'
import { usePaltaStore } from '../stores/palta'

const store = usePaltaStore()
const apiBase = axios.defaults.baseURL || ''
const nuevoCodigo = ref('')

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

// ── Sensores ─────────────────────────────────────────────
const rgbColor = (s) => (s && s.r != null && s.g != null && s.b != null)
  ? `rgb(${s.r}, ${s.g}, ${s.b})` : 'var(--borde-cards)'
const rgbText = (s) => (s && s.r != null) ? `${s.r},${s.g},${s.b}` : '—'

// ── Foto en vivo (fotos de la ÚLTIMA fruta, sus 3 vueltas) ───────────────────
const capturas = ref([])
async function cargarCapturas() {
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
const fotoUrl = computed(() => capturas.value.length ? capturas.value[capturas.value.length - 1].url : null)
watch(() => store.ultimaPalta?.id, () => { capturas.value = []; cargarCapturas() })
let capturasInterval = null
onMounted(() => { cargarCapturas(); capturasInterval = setInterval(cargarCapturas, 2000) })
onUnmounted(() => clearInterval(capturasInterval))

// ── Acciones ─────────────────────────────────────────────
async function abrirLote() {
  if (!nuevoCodigo.value.trim()) return
  await store.abrirLote(nuevoCodigo.value.trim())
  nuevoCodigo.value = ''
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
  --page:   #eceef1;
  display: flex; flex-direction: column; height: 100%;
  background: var(--page); overflow: auto;
}
.num { font-variant-numeric: tabular-nums; }

/* Sin lote */
.no-lote { flex:1; display:flex; align-items:center; justify-content:center; padding:24px; }
.no-lote-card { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:16px; padding:48px 40px; text-align:center; max-width:440px; width:100%; box-shadow:0 2px 12px rgba(0,0,0,.06); }
.no-lote-card h2 { margin:16px 0 8px; font-size:1.5rem; }
.no-lote-card p { color:var(--texto-secondary); margin-bottom:24px; }
.no-lote-form { display:flex; flex-direction:column; gap:12px; }
.no-lote-form input { padding:14px; border:1px solid var(--borde-cards); border-radius:10px; font-size:1.05rem; outline:none; }
.no-lote-form input:focus-visible { border-color:var(--pasa); box-shadow:0 0 0 3px rgba(92,184,92,.25); }
.error-msg { color:var(--antrac); font-size:.9rem; margin-top:8px; }

/* Cabecera */
.op-head { display:flex; align-items:center; justify-content:space-between; gap:12px; padding:12px 18px; background:var(--fondo-cards); border-bottom:1px solid var(--borde-cards); flex-shrink:0; flex-wrap:wrap; }
.head-left { display:flex; align-items:center; gap:16px; }
.head-lote { font-size:1.05rem; } .head-lote b { font-family:'Courier New',monospace; }
.head-timer { font-family:'Courier New',monospace; color:var(--texto-secondary); }

/* (A) 3 tarjetas */
.cards3 { display:grid; grid-template-columns:repeat(3,1fr); gap:14px; padding:16px 18px 0; }
.card3 { border-radius:12px; padding:16px 18px; color:#fff; display:flex; flex-direction:column; gap:2px; box-shadow:0 1px 4px rgba(0,0,0,.08); }
.c3-num { font-size:2.3rem; font-weight:800; line-height:1; font-variant-numeric:tabular-nums; }
.c3-lbl { font-size:.85rem; font-weight:600; opacity:.95; }
.card3--total { background:#3c4450; }
.card3--sana  { background:linear-gradient(135deg,#4c9a4c,var(--pasa-ink)); }
.card3--enf   { background:linear-gradient(135deg,#e0872b,#c0392b); }

/* Grid 2 columnas */
.grid2 { display:grid; grid-template-columns:1fr 1fr; gap:16px; padding:16px 18px 20px; align-items:start; }
.col { display:flex; flex-direction:column; gap:16px; }
.card { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:14px; box-shadow:0 1px 3px rgba(0,0,0,.05); padding-bottom:16px; }
.card-title { font-size:.9rem; font-weight:700; padding:14px 18px 10px; color:var(--texto-primary); }

/* Sensores */
.sensor-grid { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; padding:0 18px; }
.sensor-tile { background:var(--page); border:1px solid var(--borde-cards); border-radius:10px; padding:16px 8px; display:flex; flex-direction:column; align-items:center; gap:6px; }
.sensor-val { font-size:1.8rem; font-weight:800; }
.sensor-swatch { width:40px; height:28px; border-radius:6px; border:1px solid var(--borde-cards); }
.sensor-rgb { font-size:.82rem; font-weight:700; }
.sensor-lbl { font-size:.72rem; color:var(--texto-secondary); text-align:center; }

/* Pie / donut */
.pie-wrap { display:flex; align-items:center; gap:18px; padding:0 18px; flex-wrap:wrap; }
.donut { width:150px; height:150px; flex-shrink:0; }
.donut circle { transition:stroke-dasharray .4s ease, stroke-dashoffset .4s ease; }
.donut-total { font-size:26px; font-weight:800; fill:var(--texto-primary); }
.donut-sub { font-size:11px; fill:var(--texto-secondary); }
.pie-legend { list-style:none; display:flex; flex-direction:column; gap:8px; flex:1; min-width:160px; }
.pie-legend li { display:grid; grid-template-columns:14px 1fr auto auto; align-items:center; gap:8px; font-size:.9rem; }
.pie-dot { width:14px; height:14px; border-radius:4px; }
.pie-name { color:var(--texto-primary); }
.pie-pct { color:var(--texto-secondary); min-width:40px; text-align:right; }

/* Última fruta */
.ultima { border-top:4px solid transparent; }
.edge--pasa       { border-top-color:var(--pasa); }
.edge--antracnosis{ border-top-color:var(--antrac); }
.edge--scab       { border-top-color:var(--scab); }
.edge--expulsada  { border-top-color:var(--gris); }
.verdict { margin:0 18px; border-radius:12px; padding:12px 16px; display:flex; align-items:center; gap:14px; }
.verdict-ico { font-size:2rem; line-height:1; }
.verdict-text { display:flex; flex-direction:column; }
.verdict-label { font-size:1.5rem; font-weight:900; line-height:1.05; }
.verdict-sub { font-size:.9rem; font-weight:600; opacity:.9; }
.v--pasa       { background:rgba(92,184,92,.14); color:var(--pasa-ink); border:2px solid rgba(92,184,92,.35); }
.v--antracnosis{ background:rgba(217,83,79,.13); color:var(--antrac);  border:2px solid rgba(217,83,79,.35); }
.v--scab       { background:var(--scab-bg);       color:var(--scab);    border:2px solid #f0d9a8; }
.v--expulsada  { background:#eef0f2;              color:#5a626b;        border:2px solid #d7dbe0; }
.v--idle       { background:#eef0f2;              color:var(--texto-secondary); border:2px solid #d7dbe0; }

.foto-wrap { margin:12px 18px 0; border-radius:10px; overflow:hidden; background:#20242a; border:1px solid var(--borde-cards); }
.foto { display:block; width:100%; height:230px; object-fit:contain; background:#20242a; }
.foto-empty { height:230px; display:flex; align-items:center; justify-content:center; color:#8b939c; font-size:1.4rem; letter-spacing:.15em; }
.foto-thumbs { display:flex; align-items:center; gap:8px; padding:10px 18px 0; flex-wrap:wrap; min-height:22px; }
.foto-thumb { width:58px; height:58px; object-fit:contain; background:#20242a; border-radius:8px; border:1px solid var(--borde-cards); }
.foto-thumb--sel { border-color:var(--pasa); box-shadow:0 0 0 2px rgba(92,184,92,.35); }
.foto-thumbs-lbl { font-size:.75rem; color:var(--texto-secondary); }

.block { margin:14px 18px 0; }
.block-title { display:block; font-size:.8rem; font-weight:700; color:var(--texto-secondary); margin-bottom:8px; text-transform:uppercase; letter-spacing:.03em; }
.block-head { display:flex; justify-content:space-between; font-size:.9rem; margin-bottom:6px; }
.bar { height:10px; background:var(--borde-cards); border-radius:5px; overflow:hidden; }
.bar-fill { height:100%; border-radius:5px; transition:width .4s ease; }
.fill--verde { background:var(--pasa); } .fill--rojo { background:var(--antrac); } .fill--ambar { background:var(--scab); } .fill--gris { background:var(--gris); }
.prob-row { display:grid; grid-template-columns:84px 1fr 44px; align-items:center; gap:8px; margin-bottom:7px; }
.prob-lbl { font-size:.82rem; color:var(--texto-secondary); }
.prob-val { font-size:.82rem; text-align:right; font-weight:700; }

.empty-state { padding:26px; text-align:center; color:var(--texto-secondary); }

/* Botones (área táctil ≥44px) */
.btn { min-height:44px; padding:10px 20px; border-radius:10px; border:none; font-size:.95rem; font-weight:700; transition:filter .15s, opacity .15s; }
.btn:disabled { opacity:.6; cursor:default; }
.btn:focus-visible { outline:3px solid rgba(92,184,92,.5); outline-offset:2px; }
.btn--lg { font-size:1.05rem; }
.btn--primary { background:var(--pasa); color:#fff; }
.btn--danger { background:var(--antrac); color:#fff; }
.btn--danger:hover, .btn--primary:hover:not(:disabled) { filter:brightness(.95); }

/* Responsive */
@media (max-width:900px) {
  .grid2 { grid-template-columns:1fr; }
}
@media (max-width:560px) {
  .cards3 { grid-template-columns:1fr; }
  .sensor-grid { grid-template-columns:1fr 1fr; }
}
</style>
