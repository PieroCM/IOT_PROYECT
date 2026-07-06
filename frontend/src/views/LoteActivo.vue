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

      <!-- Contadores -->
      <div class="counters">
        <div class="counter counter--total">
          <span class="counter-num">{{ conteo.total }}</span>
          <span class="counter-lbl">Procesadas</span>
        </div>
        <div class="counter counter--pasa">
          <span class="counter-num">{{ conteo.pasa }}</span>
          <span class="counter-lbl">✓ Pasan (sanas)</span>
        </div>
        <div class="counter counter--enferma">
          <span class="counter-num">{{ conteo.enferma }}</span>
          <span class="counter-lbl">⚠ Enfermas</span>
        </div>
        <div class="counter counter--expulsada">
          <span class="counter-num">{{ conteo.expulsada }}</span>
          <span class="counter-lbl">✖ Expulsadas</span>
        </div>
      </div>

      <div class="op-body">
        <!-- IZQUIERDA: última fruta -->
        <section class="card ultima" :class="ultima ? 'edge--' + veredicto(ultima.clasificacion).tipo : ''">
          <h3 class="card-title">Última fruta</h3>

          <template v-if="ultima">
            <!-- Foto (la más reciente) + miniaturas de las fotos del ciclo -->
            <div class="foto-wrap">
              <img v-if="fotoUrl" :src="fotoUrl" alt="Fruta" class="foto" />
              <div v-else class="foto-empty">📷 Sin imagen</div>
            </div>
            <div v-if="capturas.length" class="foto-thumbs">
              <img v-for="(c, i) in capturas" :key="c.key" :src="c.url"
                   class="foto-thumb" :class="{ 'foto-thumb--sel': i === capturas.length - 1 }"
                   :title="'Foto ' + (i + 1)" :alt="'Foto ' + (i + 1)" />
              <span class="foto-thumbs-lbl">Fotos del ciclo ({{ capturas.length }})</span>
            </div>

            <!-- Veredicto grande -->
            <div class="verdict" :class="'v--' + veredicto(ultima.clasificacion).tipo">
              <span class="verdict-ico">{{ verIco(ultima.clasificacion) }}</span>
              <div class="verdict-text">
                <span class="verdict-label">{{ veredicto(ultima.clasificacion).label }}</span>
                <span class="verdict-sub">{{ veredicto(ultima.clasificacion).sub }}</span>
              </div>
            </div>

            <!-- ¿Es palta? (gate) -->
            <div class="block">
              <div class="block-head">
                <span>{{ esExpulsada(ultima.clasificacion) ? 'No es palta' : 'Es palta' }}</span>
                <span class="num">{{ gateCerteza(ultima) != null ? gateCerteza(ultima) + '%' : '—' }}</span>
              </div>
              <div class="bar"><div class="bar-fill" :class="esExpulsada(ultima.clasificacion) ? 'fill--gris' : 'fill--verde'"
                   :style="{ width: (gateCerteza(ultima) ?? 0) + '%' }"></div></div>
            </div>

            <!-- Enfermedad -->
            <div class="block">
              <span class="block-title">Enfermedad</span>
              <template v-if="ultima.probabilidades">
                <p class="salud-line">
                  Sana <b class="txt-verde">{{ sanaPct(ultima) }}%</b>
                  · Enferma <b class="txt-rojo">{{ enfermaPct(ultima) }}%</b>
                </p>
                <div class="prob-row"><span class="prob-lbl">Sana</span>
                  <div class="bar"><div class="bar-fill fill--verde" :style="{ width: sanaPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ sanaPct(ultima) }}%</span></div>
                <div class="prob-row"><span class="prob-lbl">Antracnosis</span>
                  <div class="bar"><div class="bar-fill fill--rojo" :style="{ width: antracPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ antracPct(ultima) }}%</span></div>
                <div class="prob-row"><span class="prob-lbl">Scab</span>
                  <div class="bar"><div class="bar-fill fill--ambar" :style="{ width: scabPct(ultima) + '%' }"></div></div>
                  <span class="prob-val num">{{ scabPct(ultima) }}%</span></div>
              </template>
              <p v-else class="salud-line salud-line--muted">Expulsada — no pasó el detector de palta.</p>
            </div>

            <!-- Sensores -->
            <div class="block">
              <span class="block-title">Sensores</span>
              <div class="sensor-grid">
                <div class="sensor-tile"><span class="sensor-ico">🌡️</span>
                  <span class="sensor-val num">{{ ultima.sensor?.temp != null ? ultima.sensor.temp.toFixed(1) + '°' : '—' }}</span>
                  <span class="sensor-lbl">Temp.</span></div>
                <div class="sensor-tile"><span class="sensor-ico">💧</span>
                  <span class="sensor-val num">{{ ultima.sensor?.humedad != null ? ultima.sensor.humedad.toFixed(0) + '%' : '—' }}</span>
                  <span class="sensor-lbl">Humedad</span></div>
                <div class="sensor-tile"><span class="sensor-ico">☀️</span>
                  <span class="sensor-val num">{{ ultima.sensor?.lux != null ? ultima.sensor.lux.toFixed(0) : '—' }}</span>
                  <span class="sensor-lbl">Lux</span></div>
                <div class="sensor-tile"><span class="sensor-swatch" :style="{ background: rgbColor(ultima.sensor) }"></span>
                  <span class="sensor-val sensor-val--sm num">{{ rgbText(ultima.sensor) }}</span>
                  <span class="sensor-lbl">Color RGB</span></div>
              </div>
            </div>
          </template>

          <div v-else class="empty-state">Esperando la primera fruta…</div>
        </section>

        <!-- DERECHA: tabla de frutas -->
        <section class="card tabla">
          <h3 class="card-title">Frutas del lote</h3>
          <div class="tabla-wrap">
            <table>
              <thead>
                <tr>
                  <th>#</th><th>Hora</th><th>Veredicto</th><th>Es palta</th>
                  <th>Sana</th><th>Antrac.</th><th>Scab</th><th>Color</th><th>Lux</th><th>T°</th><th>HR</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="p in frutas" :key="p.id" :class="'row--' + veredicto(p.clasificacion).tipo">
                  <td class="num">{{ p.n }}</td>
                  <td class="num">{{ formatHora(p.timestamp) }}</td>
                  <td>
                    <span class="tbadge" :class="'tb--' + veredicto(p.clasificacion).tipo">
                      {{ verIco(p.clasificacion) }} {{ veredicto(p.clasificacion).label }}
                      <small v-if="veredicto(p.clasificacion).tipo === 'antracnosis' || veredicto(p.clasificacion).tipo === 'scab'">· {{ veredicto(p.clasificacion).sub }}</small>
                    </span>
                  </td>
                  <td class="num">{{ gateCerteza(p) != null ? gateCerteza(p) + '%' : '—' }}</td>
                  <td class="num txt-verde">{{ sanaPct(p) != null ? sanaPct(p) + '%' : '—' }}</td>
                  <td class="num txt-rojo">{{ antracPct(p) != null ? antracPct(p) + '%' : '—' }}</td>
                  <td class="num txt-ambar">{{ scabPct(p) != null ? scabPct(p) + '%' : '—' }}</td>
                  <td><span class="tbl-swatch" :style="{ background: rgbColor(p.sensor) }" :title="rgbText(p.sensor)"></span></td>
                  <td class="num">{{ p.sensor?.lux != null ? p.sensor.lux.toFixed(0) : '—' }}</td>
                  <td class="num">{{ p.sensor?.temp != null ? p.sensor.temp.toFixed(1) + '°' : '—' }}</td>
                  <td class="num">{{ p.sensor?.humedad != null ? p.sensor.humedad.toFixed(0) + '%' : '—' }}</td>
                </tr>
                <tr v-if="!frutas.length"><td colspan="11" class="empty-state">Sin frutas registradas todavía</td></tr>
              </tbody>
            </table>
          </div>
        </section>
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

// ── Veredicto (mapa de clasificación → PASA / ENFERMA / EXPULSADA) ──
const VERE = {
  sana:        { label: 'PASA',      sub: 'Palta sana',    tipo: 'pasa' },
  antracnosis: { label: 'ENFERMA',   sub: 'Antracnosis',   tipo: 'antracnosis' },
  scab:        { label: 'ENFERMA',   sub: 'Scab (sarna)',  tipo: 'scab' },
  no_es_palta: { label: 'EXPULSADA', sub: 'No es palta',   tipo: 'expulsada' },
}
const veredicto = (c) => VERE[c] || { label: '—', sub: 'Esperando…', tipo: 'idle' }
const verIco = (c) => ({ sana: '✓', antracnosis: '⚠', scab: '⚠', no_es_palta: '✖' }[c] || '·')
const esExpulsada = (c) => c === 'no_es_palta'

// ── Probabilidades / confianza (datos reales del backend) ──
const gateCerteza = (p) => {
  if (p?.confianza_gate == null) return null
  const v = esExpulsada(p.clasificacion) ? (1 - p.confianza_gate) : p.confianza_gate
  return Math.round(v * 100)
}
const sanaPct   = (p) => p?.probabilidades ? Math.round((p.probabilidades.sana ?? 0) * 100) : null
const antracPct = (p) => p?.probabilidades ? Math.round((p.probabilidades.antracnosis ?? 0) * 100) : null
const scabPct   = (p) => p?.probabilidades ? Math.round((p.probabilidades.scab ?? 0) * 100) : null
const enfermaPct = (p) => p?.probabilidades
  ? Math.round(((p.probabilidades.antracnosis ?? 0) + (p.probabilidades.scab ?? 0)) * 100) : null

// ── Frutas (nº secuencial, más reciente primero) + contadores ──
const frutas = computed(() => store.paltas.map((p, i) => ({ ...p, n: i + 1 })).slice().reverse())
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
    // Si mientras cargábamos cambió la palta (siguiente fruta), descarta el
    // resultado viejo -> nunca mostramos fotos de la palta anterior.
    if (store.ultimaPalta?.id !== id) return
    capturas.value = (data.fotos || []).map((u, i) => ({ key: id + '-' + i, url: apiBase + u }))
  } catch {
    if (store.ultimaPalta?.id === id) capturas.value = []
  }
}
const fotoUrl = computed(() => capturas.value.length ? capturas.value[capturas.value.length - 1].url : null)
// Al cambiar de fruta: LIMPIA ya las fotos (no arrastrar las de la anterior) y recarga.
watch(() => store.ultimaPalta?.id, () => { capturas.value = []; cargarCapturas() })
let capturasInterval = null
onMounted(() => { cargarCapturas(); capturasInterval = setInterval(cargarCapturas, 2000) })
onUnmounted(() => clearInterval(capturasInterval))

// ── Utils / acciones ─────────────────────────────────────
function formatHora(ts) {
  if (!ts) return '—'
  return new Date(ts).toLocaleTimeString('es-ES', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}
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
  --scab:   #E08A00;         /* ámbar oscuro legible */
  --scab-bg:#fbf0dc;
  --gris:   #8a929b;
  --page:   #eceef1;
  display: flex; flex-direction: column; height: 100%;
  background: var(--page);
}
.num { font-variant-numeric: tabular-nums; }
.txt-verde { color: var(--pasa-ink); } .txt-rojo { color: var(--antrac); } .txt-ambar { color: var(--scab); }

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

/* Contadores */
.counters { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; padding:14px 18px 0; flex-shrink:0; }
.counter { border-radius:12px; padding:14px 16px; display:flex; flex-direction:column; gap:2px; color:#fff; box-shadow:0 1px 4px rgba(0,0,0,.08); }
.counter-num { font-size:2.1rem; font-weight:800; line-height:1; font-variant-numeric:tabular-nums; }
.counter-lbl { font-size:.82rem; font-weight:600; opacity:.95; }
.counter--total    { background:#3c4450; }
.counter--pasa     { background:linear-gradient(135deg,#4c9a4c,var(--pasa-ink)); }
.counter--enferma  { background:linear-gradient(135deg,#e0872b,#c0392b); }
.counter--expulsada{ background:linear-gradient(135deg,#9aa1a9,var(--gris)); }

/* Cuerpo */
.op-body { flex:1; overflow:auto; padding:14px 18px 18px; display:grid; grid-template-columns:minmax(340px,1fr) 1.4fr; gap:16px; align-items:start; }
.card { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:14px; box-shadow:0 1px 3px rgba(0,0,0,.05); }
.card-title { font-size:.95rem; font-weight:700; padding:14px 18px 0; }

/* Última fruta */
.ultima { padding-bottom:16px; border-top:4px solid transparent; }
.edge--pasa       { border-top-color:var(--pasa); }
.edge--antracnosis{ border-top-color:var(--antrac); }
.edge--scab       { border-top-color:var(--scab); }
.edge--expulsada  { border-top-color:var(--gris); }
.foto-wrap { margin:10px 18px 0; border-radius:10px; overflow:hidden; background:#20242a; border:1px solid var(--borde-cards); }
.foto { display:block; width:100%; height:240px; object-fit:contain; background:#20242a; }
.foto-empty { height:240px; display:flex; align-items:center; justify-content:center; color:var(--texto-secondary); }
.foto-thumbs { display:flex; align-items:center; gap:8px; padding:10px 18px 0; flex-wrap:wrap; }
.foto-thumb { width:60px; height:60px; object-fit:contain; background:#20242a; border-radius:8px; border:1px solid var(--borde-cards); }
.foto-thumb--sel { border-color:var(--pasa); box-shadow:0 0 0 2px rgba(92,184,92,.35); }
.foto-thumbs-lbl { font-size:.72rem; color:var(--texto-secondary); }

.verdict { margin:14px 18px 0; border-radius:12px; padding:16px 20px; display:flex; align-items:center; gap:16px; }
.verdict-ico { font-size:2.4rem; line-height:1; }
.verdict-text { display:flex; flex-direction:column; }
.verdict-label { font-size:1.8rem; font-weight:900; line-height:1.05; letter-spacing:.02em; }
.verdict-sub { font-size:.95rem; font-weight:600; opacity:.9; }
.v--pasa       { background:rgba(92,184,92,.14); color:var(--pasa-ink); border:2px solid rgba(92,184,92,.35); }
.v--antracnosis{ background:rgba(217,83,79,.13); color:var(--antrac);  border:2px solid rgba(217,83,79,.35); }
.v--scab       { background:var(--scab-bg);       color:var(--scab);    border:2px solid #f0d9a8; }
.v--expulsada  { background:#eef0f2;              color:#5a626b;        border:2px solid #d7dbe0; }
.v--idle       { background:#eef0f2;              color:var(--texto-secondary); border:2px solid #d7dbe0; }

.block { margin:16px 18px 0; }
.block-title { display:block; font-size:.82rem; font-weight:700; color:var(--texto-secondary); margin-bottom:8px; text-transform:uppercase; letter-spacing:.03em; }
.block-head { display:flex; justify-content:space-between; font-size:.9rem; margin-bottom:6px; }
.bar { height:10px; background:var(--borde-cards); border-radius:5px; overflow:hidden; }
.bar-fill { height:100%; border-radius:5px; transition:width .4s ease; }
.fill--verde { background:var(--pasa); } .fill--rojo { background:var(--antrac); } .fill--ambar { background:var(--scab); } .fill--gris { background:var(--gris); }
.salud-line { font-size:.95rem; margin-bottom:10px; }
.salud-line--muted { color:var(--texto-secondary); }
.prob-row { display:grid; grid-template-columns:84px 1fr 44px; align-items:center; gap:8px; margin-bottom:7px; }
.prob-lbl { font-size:.82rem; color:var(--texto-secondary); }
.prob-val { font-size:.82rem; text-align:right; font-weight:700; }

.sensor-grid { display:grid; grid-template-columns:repeat(4,1fr); gap:10px; }
.sensor-tile { background:var(--page); border:1px solid var(--borde-cards); border-radius:10px; padding:12px 8px; display:flex; flex-direction:column; align-items:center; gap:4px; }
.sensor-ico { font-size:1.2rem; }
.sensor-swatch { width:24px; height:24px; border-radius:6px; border:1px solid var(--borde-cards); }
.sensor-val { font-size:1.15rem; font-weight:800; }
.sensor-val--sm { font-size:.82rem; }
.sensor-lbl { font-size:.7rem; color:var(--texto-secondary); text-align:center; }

/* Tabla */
.tabla { overflow:hidden; }
.tabla-wrap { overflow:auto; max-height:calc(100vh - 220px); margin-top:10px; }
.tabla table { width:100%; border-collapse:collapse; font-size:.84rem; white-space:nowrap; }
.tabla thead { background:var(--page); position:sticky; top:0; z-index:1; }
.tabla th { padding:9px 12px; text-align:left; color:var(--texto-secondary); font-weight:600; border-bottom:1px solid var(--borde-cards); }
.tabla td { padding:9px 12px; border-bottom:1px solid var(--borde-cards); }
.row--pasa       { background:rgba(92,184,92,.05); }
.row--antracnosis{ background:rgba(217,83,79,.06); }
.row--scab       { background:rgba(224,138,0,.07); }
.row--expulsada  { background:rgba(138,146,155,.07); }
.tbadge { display:inline-flex; align-items:center; gap:4px; font-size:.74rem; font-weight:800; padding:3px 9px; border-radius:12px; }
.tbadge small { font-weight:600; opacity:.85; }
.tb--pasa       { background:rgba(92,184,92,.16); color:var(--pasa-ink); }
.tb--antracnosis{ background:rgba(217,83,79,.15); color:var(--antrac); }
.tb--scab       { background:var(--scab-bg);       color:var(--scab); }
.tb--expulsada  { background:#e7eaee;              color:#5a626b; }
.tbl-swatch { display:inline-block; width:22px; height:22px; border-radius:5px; border:1px solid var(--borde-cards); vertical-align:middle; }

.empty-state { padding:26px; text-align:center; color:var(--texto-secondary); }

/* Botones (área táctil ≥44px) */
.btn { min-height:44px; padding:10px 20px; border-radius:10px; border:none; font-size:.95rem; font-weight:700; transition:filter .15s, opacity .15s; }
.btn:disabled { opacity:.6; cursor:default; }
.btn:focus-visible { outline:3px solid rgba(92,184,92,.5); outline-offset:2px; }
.btn--lg { font-size:1.05rem; }
.btn--primary { background:var(--pasa); color:#fff; }
.btn--primary:hover:not(:disabled) { filter:brightness(.95); }
.btn--danger { background:var(--antrac); color:#fff; }
.btn--danger:hover { filter:brightness(.95); }

/* Responsive */
@media (max-width:1000px) {
  .op-body { grid-template-columns:1fr; }
  .tabla-wrap { max-height:none; }
}
@media (max-width:620px) {
  .counters { grid-template-columns:1fr 1fr; }
  .sensor-grid { grid-template-columns:1fr 1fr; }
}
</style>
