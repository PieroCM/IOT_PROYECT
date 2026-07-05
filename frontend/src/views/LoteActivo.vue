<template>
  <div class="lote-root">

    <!-- ═══ SIN LOTE ACTIVO ═══ -->
    <div v-if="!store.loteActivo" class="no-lote">
      <div class="no-lote-card">
        <svg width="56" height="56" viewBox="0 0 24 24" fill="none" stroke="var(--ok)" stroke-width="1.5">
          <path d="M12 2C8 2 4 6 4 10c0 5 8 12 8 12s8-7 8-12c0-4-4-8-8-8z"/>
        </svg>
        <h2>No hay lote activo</h2>
        <p>Ingresa un código para iniciar la clasificación</p>
        <div class="no-lote-form">
          <input v-model="nuevoCodigo" type="text" placeholder="Ej: LOTE-2024-008"
                 @keyup.enter="abrirLote" />
          <button class="btn btn--primary btn--lg" @click="abrirLote" :disabled="store.loading">
            {{ store.loading ? 'Abriendo…' : 'Abrir nuevo lote' }}
          </button>
        </div>
        <p v-if="store.error" class="error-msg">{{ store.error }}</p>
      </div>
    </div>

    <!-- ═══ LOTE ACTIVO ═══ -->
    <template v-else>

      <!-- A) FRANJA DE ESTADO DEL SISTEMA -->
      <div class="status-strip" role="status" aria-label="Estado del sistema">
        <span class="chip" :class="'chip--' + salud.linea.nivel">
          <span class="chip-dot" :class="{ 'chip-dot--pulse': salud.linea.nivel==='ok' }"></span>
          <span class="chip-ico">▶</span> Línea: <b>{{ salud.linea.texto }}</b>
        </span>
        <span class="chip" :class="'chip--' + salud.servidor.nivel">
          <span class="chip-ico">🖧</span> Servidor: <b>{{ salud.servidor.texto }}</b>
        </span>
        <span class="chip" :class="'chip--' + salud.camara.nivel">
          <span class="chip-ico">📷</span> Cámara: <b>{{ salud.camara.texto }}</b>
        </span>
        <span class="chip" :class="'chip--' + salud.modelo.nivel">
          <span class="chip-ico">🧠</span> IA: <b>{{ salud.modelo.texto }}</b>
        </span>
        <span class="chip" :class="'chip--' + salud.actividad.nivel">
          <span class="chip-ico">📡</span> {{ salud.actividad.texto }}
        </span>

        <span class="status-spacer"></span>
        <span class="status-lote"># <b>{{ store.loteActivo.codigo }}</b></span>
        <span class="status-timer num">⏱ {{ elapsedTime }}</span>
      </div>

      <!-- Banner único de alerta (solo si hay condición accionable) -->
      <div v-if="alertaPrincipal" class="alert-banner" :class="'alert--' + alertaPrincipal.nivel" role="alert">
        <span class="alert-ico">{{ alertaPrincipal.nivel === 'alarm' ? '⛔' : '⚠️' }}</span>
        <div class="alert-text">
          <strong>{{ alertaPrincipal.titulo }}</strong>
          <span>{{ alertaPrincipal.accion }}</span>
        </div>
      </div>

      <!-- Cuerpo -->
      <div class="dash-body">

        <!-- B+C) HERO (Tasa de Rechazo) + KPIs secundarios -->
        <div class="kpi-row">
          <!-- HERO -->
          <div class="hero-card" :class="'hero--' + estadoRechazo">
            <div class="hero-head">
              <span class="hero-label">Tasa de Rechazo</span>
              <span class="hero-state">{{ estadoRechazo === 'alarm' ? '⛔ ALTA' : estadoRechazo === 'warn' ? '⚠️ VIGILAR' : '✓ NORMAL' }}</span>
            </div>
            <div class="hero-value num">{{ calidad.tasaRechazo }}<span class="hero-pct">%</span></div>
            <div class="hero-sub">
              <span class="hero-tag hero-tag--ok">✓ Palta <b class="num">{{ calidad.aceptadas }}</b></span>
              <span class="hero-tag hero-tag--rej">✕ No palta <b class="num">{{ calidad.rechazadas }}</b></span>
            </div>
          </div>

          <!-- Procesadas -->
          <div class="kpi-mini">
            <p class="kpi-label">Procesadas</p>
            <div class="kpi-value-row">
              <span class="kpi-number num">{{ calidad.total }}</span>
              <span class="kpi-meta">/ {{ META_LOTE }}</span>
            </div>
            <div class="progress-bar">
              <div class="progress-fill" :style="{ width: Math.min((calidad.total / META_LOTE) * 100, 100) + '%' }" />
            </div>
          </div>

          <!-- Velocidad -->
          <div class="kpi-mini">
            <p class="kpi-label">Velocidad</p>
            <div class="kpi-value-row">
              <span class="kpi-number num">{{ store.velocidadPpm }}</span>
              <span class="kpi-meta">ppm {{ tendenciaVel }}</span>
            </div>
            <div class="sparkline">
              <div v-for="(v, i) in speedHistory" :key="i" class="spark-bar"
                   :style="{ height: Math.max((v / maxSpeed) * 100, 8) + '%' }" />
            </div>
          </div>

          <!-- Confianza promedio -->
          <div class="kpi-mini">
            <p class="kpi-label">Confianza prom.</p>
            <div class="kpi-value-row">
              <span class="kpi-number num" :class="'txt--' + confianzaPromNivel">
                {{ calidad.confianzaProm != null ? Math.round(calidad.confianzaProm * 100) : '—' }}<span v-if="calidad.confianzaProm != null" class="kpi-meta">%</span>
              </span>
            </div>
            <div class="progress-bar">
              <div class="progress-fill" :class="'fill--' + confianzaPromNivel"
                   :style="{ width: (calidad.confianzaProm || 0) * 100 + '%' }" />
            </div>
          </div>
        </div>

        <!-- Dos columnas -->
        <div class="two-col">
          <!-- IZQUIERDA: cámara en vivo -->
          <div class="col-left">
            <div class="card camara-card">
              <div class="camara-head">
                <h3 class="card-title">En vivo · Cámara</h3>
                <span v-if="ultimaCaptura" class="verdict-badge"
                      :class="esNoPalta(ultimaCaptura.clasificacion) ? 'verdict--rej' : 'verdict--ok'">
                  {{ esNoPalta(ultimaCaptura.clasificacion) ? '✕' : '✓' }} {{ etiqueta(ultimaCaptura.clasificacion) }}
                </span>
              </div>
              <div class="camara-wrap">
                <template v-if="ultimaCaptura">
                  <img :src="ultimaCaptura.url" :alt="'Palta ' + ultimaCaptura.id" class="camara-img" />
                  <div v-if="bajaConfianza" class="baja-conf">⚠️ Baja confianza — verificar</div>
                  <div class="camara-meta">
                    Palta #{{ ultimaCaptura.id }} · {{ formatHora(ultimaCaptura.timestamp) }}
                  </div>
                  <div v-if="capturas.length" class="camara-thumbs">
                    <img v-for="(c, i) in capturas" :key="c.key" :src="c.url" class="camara-thumb"
                         :title="'Foto ' + (i + 1)" :alt="'Foto ' + (i + 1)" />
                  </div>
                </template>
                <div v-else class="camara-empty">
                  <span class="camara-empty-icon">📷</span>
                  <p>Esperando captura…</p>
                </div>
              </div>
            </div>
          </div>

          <!-- DERECHA: veredicto + sensores + tabla -->
          <div class="col-right">
            <!-- Última palta -->
            <div class="card ultima-card">
              <h3 class="card-title">Última clasificación</h3>
              <div v-if="store.ultimaPalta">
                <div class="result-display" :class="esNoPalta(store.ultimaPalta.clasificacion) ? 'result--rej' : 'result--ok'">
                  <span class="result-icon">{{ esNoPalta(store.ultimaPalta.clasificacion) ? '✕' : '✓' }}</span>
                  <span class="result-text">{{ etiqueta(store.ultimaPalta.clasificacion).toUpperCase() }}</span>
                </div>
                <div class="conf-wrap">
                  <div class="conf-head">
                    <span>Confianza</span>
                    <span class="num">{{ store.ultimaPalta.confianza != null ? Math.round(store.ultimaPalta.confianza * 100) + '%' : '—' }}</span>
                  </div>
                  <div class="progress-bar">
                    <div class="progress-fill" :class="'fill--' + confianzaNivel(store.ultimaPalta.confianza)"
                         :style="{ width: (store.ultimaPalta.confianza || 0) * 100 + '%' }" />
                  </div>
                </div>
                <p class="timestamp-label">hace {{ segundosDesdeUltimaPalta }}s</p>
              </div>
              <div v-else class="empty-state">Esperando primera palta…</div>
            </div>

            <!-- Sensores (riesgo primario) -->
            <div class="card sensores-card">
              <h3 class="card-title">Ambiente · Palta #{{ store.ultimaPalta?.id ?? '—' }}</h3>
              <div class="riesgo-row">
                <span class="riesgo-label">Riesgo ambiental</span>
                <span class="riesgo-badge" :class="'riesgo--' + riesgoBadge.nivel">{{ riesgoBadge.icono }} {{ riesgoBadge.label }}</span>
              </div>
              <div class="amb-grid">
                <div class="amb-tile">
                  <span class="amb-ico">🌡️</span>
                  <span class="amb-value num">{{ sensorActual?.temp != null ? sensorActual.temp.toFixed(1) + '°' : '—' }}</span>
                  <span class="amb-label">Temp.</span>
                </div>
                <div class="amb-tile">
                  <span class="amb-ico">💧</span>
                  <span class="amb-value num">{{ sensorActual?.humedad != null ? sensorActual.humedad.toFixed(0) + '%' : '—' }}</span>
                  <span class="amb-label">Humedad</span>
                </div>
                <div class="amb-tile">
                  <span class="amb-swatch" :style="{ background: rgbColor }"></span>
                  <span class="amb-value amb-value--sm num">{{ sensorActual?.lux != null ? sensorActual.lux.toFixed(0) + ' lx' : '—' }}</span>
                  <span class="amb-label">Color / Luz</span>
                </div>
              </div>
              <details class="rgb-details">
                <summary>Detalle RGB</summary>
                <div class="rgb-values num">
                  <span>R {{ sensorActual?.r ?? '—' }}</span>
                  <span>G {{ sensorActual?.g ?? '—' }}</span>
                  <span>B {{ sensorActual?.b ?? '—' }}</span>
                </div>
              </details>
            </div>

            <!-- Tabla -->
            <div class="card table-card">
              <h3 class="card-title">Paltas del lote</h3>
              <div class="table-wrap">
                <table class="paltas-table">
                  <thead>
                    <tr><th>#</th><th>Hora</th><th>Resultado</th><th>Conf.</th><th>T°</th><th>HR</th></tr>
                  </thead>
                  <tbody>
                    <tr v-for="p in ultimasOcho" :key="p.id" :class="esNoPalta(p.clasificacion) ? 'row--rej' : 'row--ok'">
                      <td class="num">{{ p.id }}</td>
                      <td class="num">{{ formatHora(p.timestamp) }}</td>
                      <td>
                        <span class="tbadge" :class="esNoPalta(p.clasificacion) ? 'tbadge--rej' : 'tbadge--ok'">
                          {{ esNoPalta(p.clasificacion) ? '✕' : '✓' }} {{ etiqueta(p.clasificacion) }}
                        </span>
                      </td>
                      <td class="num">{{ p.confianza != null ? Math.round(p.confianza * 100) + '%' : '—' }}</td>
                      <td class="num">{{ p.sensor?.temp?.toFixed(1) ?? '—' }}°</td>
                      <td class="num">{{ p.sensor?.humedad?.toFixed(0) ?? '—' }}%</td>
                    </tr>
                    <tr v-if="!store.paltas.length"><td colspan="6" class="empty-state">Sin paltas registradas</td></tr>
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Footer -->
      <footer class="bottom-bar">
        <span class="foot-sum">{{ calidad.total }} procesadas en <b class="num">{{ elapsedTime }}</b></span>
        <div class="bottom-actions">
          <button class="btn btn--ghost" @click="exportarCSV">↓ Exportar CSV</button>
          <button class="btn btn--danger" @click="cerrarLote">Cerrar Lote</button>
        </div>
      </footer>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import axios from 'axios'
import { usePaltaStore } from '../stores/palta'

const store = usePaltaStore()
const apiBase = axios.defaults.baseURL || ''
const nuevoCodigo = ref('')

// ── Umbrales / constantes (ajustables) ───────────────────
const META_LOTE          = 50
const UMBRAL_RECHAZO      = 15      // % : rojo alarma por encima
const UMBRAL_CERCA        = 10      // % : ámbar (vigilar)
const UMBRAL_CONFIANZA    = 0.70    // baja confianza de una palta
const CONF_PROM_BAJA      = 0.75    // confianza promedio baja del lote
const UMBRAL_SIN_SENAL_MS = 90000   // ~90 s (un ciclo de palta dura ~45 s)

// ── Reloj (1 s) para "hace Xs", cronómetro y actividad ───
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

// ── Clasificación PALTA / NO-PALTA ───────────────────────
const esNoPalta = (c) => c === 'no_es_palta'
const etiqueta  = (c) => (esNoPalta(c) ? 'No palta' : 'Palta')

// ── Calidad (CLIENTE): kpis del backend cuentan 'antracnosis'
//    (ya no existe) -> se calcula desde store.paltas ────────
const calidad = computed(() => {
  const ps = store.paltas
  const total = ps.length
  const rechazadas = ps.filter(p => esNoPalta(p.clasificacion)).length
  const aceptadas = total - rechazadas
  const tasaRechazo = total ? Math.round((rechazadas / total) * 1000) / 10 : 0
  const conf = ps.filter(p => p.confianza != null)
  const confianzaProm = conf.length ? conf.reduce((s, p) => s + p.confianza, 0) / conf.length : null
  return { total, aceptadas, rechazadas, tasaRechazo, confianzaProm }
})

const estadoRechazo = computed(() => {
  const { total, tasaRechazo } = calidad.value
  if (!total) return 'ok'
  if (tasaRechazo > UMBRAL_RECHAZO) return 'alarm'
  if (tasaRechazo >= UMBRAL_CERCA) return 'warn'
  return 'ok'
})

const confianzaPromNivel = computed(() => {
  const c = calidad.value.confianzaProm
  if (c == null) return 'idle'
  return c < CONF_PROM_BAJA ? 'warn' : 'ok'
})
const confianzaNivel = (c) => {
  if (c == null) return 'idle'
  if (c < UMBRAL_CONFIANZA) return 'warn'
  return 'ok'
}

// ── Velocidad / tendencia ────────────────────────────────
const speedHistory = computed(() => {
  const h = store._speedHistory
  if (!h.length) return [1,1,1,1,1,1,1,1,1, store.velocidadPpm || 0]
  return h
})
const maxSpeed = computed(() => Math.max(...speedHistory.value, 0.1))
const tendenciaVel = computed(() => {
  const h = store._speedHistory
  if (h.length < 4) return '→'
  const n = h.length
  const ult = h[n-1], prev = (h[n-2] + h[n-3] + h[n-4]) / 3
  if (ult > prev * 1.05) return '↑'
  if (ult < prev * 0.95) return '↓'
  return '→'
})

// ── Cámara ───────────────────────────────────────────────
const ultimaCaptura = computed(() => {
  const p = store.ultimaPalta
  if (!p || !capturas.value.length) return null
  return {
    id: p.id,
    url: capturas.value[capturas.value.length - 1].url,   // la más reciente (URL única, sin caché)
    clasificacion: p.clasificacion,
    timestamp: p.timestamp,
  }
})
const capturas = ref([])
async function cargarCapturas() {
  const id = store.ultimaPalta?.id
  if (!id) { capturas.value = []; return }
  try {
    const { data } = await axios.get(`/api/palta/${id}/fotos`)
    capturas.value = (data.fotos || []).map((u, i) => ({ key: id + '-' + i, url: apiBase + u }))
  } catch { capturas.value = [] }
}
let capturasInterval = null
onMounted(() => { cargarCapturas(); capturasInterval = setInterval(cargarCapturas, 3000) })
onUnmounted(() => clearInterval(capturasInterval))

const bajaConfianza = computed(() => {
  const c = store.ultimaPalta?.confianza
  return c != null && c < UMBRAL_CONFIANZA
})

// ── Sensores ─────────────────────────────────────────────
const sensorActual = computed(() => store.ultimaPalta?.sensor || null)
const rgbColor = computed(() => {
  const s = sensorActual.value
  if (!s || s.r == null || s.g == null || s.b == null) return 'var(--borde-cards)'
  return `rgb(${s.r}, ${s.g}, ${s.b})`
})
const riesgoBadge = computed(() => {
  const s = sensorActual.value
  if (!s || s.temp == null || s.humedad == null) return { label: 'SIN DATO', nivel: 'idle', icono: '—' }
  const t = s.temp, h = s.humedad
  if (h < 70 && t < 26) return { label: 'BAJO',  nivel: 'ok',   icono: '✓' }
  if (h < 80 && t < 28) return { label: 'MEDIO', nivel: 'warn', icono: '⚠️' }
  return { label: 'ALTO', nivel: 'alarm', icono: '⛔' }
})

// ── Última palta / actividad ─────────────────────────────
const segundosDesdeUltimaPalta = computed(() => {
  const p = store.ultimaPalta
  if (!p?.timestamp) return 0
  return Math.max(0, Math.floor((ahora.value - new Date(p.timestamp).getTime()) / 1000))
})

// ── Salud del sistema (chips) ────────────────────────────
const salud = computed(() => {
  const ps = store.paltas
  const up = store.ultimaPalta

  const linea = store.loteActivo ? { nivel: 'ok', texto: 'EN PROCESO' } : { nivel: 'idle', texto: 'DETENIDA' }
  const servidor = store.esp32Online ? { nivel: 'ok', texto: 'OK' } : { nivel: 'alarm', texto: 'SIN CONEXIÓN' }

  // Cámara: OK si alguna de las últimas 3 paltas trae foto (gracia anti-flaps)
  let camara
  if (!ps.length) camara = { nivel: 'idle', texto: 'ESPERANDO' }
  else camara = ps.slice(-3).some(p => p.foto_url) ? { nivel: 'ok', texto: 'OK' } : { nivel: 'alarm', texto: 'ERROR' }

  // Modelo: degradado si el veredicto llega sin confianza
  let modelo
  if (!up) modelo = { nivel: 'idle', texto: '—' }
  else modelo = (up.confianza == null || up.confianza === 0) ? { nivel: 'warn', texto: 'DEGRADADO' } : { nivel: 'ok', texto: 'OK' }

  // Actividad: heurística (idle es normal → advertencia, no alarma)
  let actividad
  if (!store.loteActivo) actividad = { nivel: 'idle', texto: 'DETENIDA' }
  else if (!up) actividad = { nivel: 'idle', texto: 'ESPERANDO 1ª PALTA' }
  else if (segundosDesdeUltimaPalta.value * 1000 > UMBRAL_SIN_SENAL_MS)
    actividad = { nivel: 'warn', texto: `SIN ACTIVIDAD ${segundosDesdeUltimaPalta.value}s` }
  else actividad = { nivel: 'ok', texto: 'ACTIVO' }

  return { linea, servidor, camara, modelo, actividad }
})

// Banner ÚNICO: la condición accionable más crítica
const alertaPrincipal = computed(() => {
  const s = salud.value
  if (!store.loteActivo) return null
  if (s.servidor.nivel === 'alarm') return { nivel: 'alarm', titulo: 'Sin conexión al servidor', accion: 'Revisa que el backend (Docker) esté encendido.' }
  if (s.camara.nivel === 'alarm')   return { nivel: 'alarm', titulo: 'Cámara sin imagen',        accion: 'Revisar la cámara del ESP32.' }
  if (s.modelo.nivel === 'warn')    return { nivel: 'warn',  titulo: 'Modelo degradado',         accion: 'Los veredictos llegan sin confianza; revisar el modelo en el backend.' }
  if (estadoRechazo.value === 'alarm') return { nivel: 'alarm', titulo: `Tasa de rechazo alta (${calidad.value.tasaRechazo}%)`, accion: 'Revisar el producto o la calibración de la línea.' }
  if (s.actividad.nivel === 'warn') return { nivel: 'warn',  titulo: 'Sin actividad reciente',   accion: `La línea no procesa hace ${segundosDesdeUltimaPalta.value}s.` }
  return null
})

// ── Tabla ────────────────────────────────────────────────
const ultimasOcho = computed(() => [...store.paltas].reverse().slice(0, 8))
function formatHora(ts) {
  if (!ts) return '—'
  return new Date(ts).toLocaleTimeString('es-ES', { hour:'2-digit', minute:'2-digit', second:'2-digit' })
}

// ── Acciones ─────────────────────────────────────────────
async function abrirLote() {
  if (!nuevoCodigo.value.trim()) return
  await store.abrirLote(nuevoCodigo.value.trim())
  nuevoCodigo.value = ''
}
async function cerrarLote() {
  if (!store.loteActivo?.id) return
  if (!window.confirm('¿Cerrar el lote actual? Esto detiene el procesamiento de la línea.')) return
  await store.cerrarLote(store.loteActivo.id)
}
function exportarCSV() {
  const headers = ['#', 'Hora', 'Resultado', 'Confianza', 'R', 'G', 'B', 'Lux', 'Temp', 'Humedad']
  const rows = store.paltas.map((p, i) => [
    i + 1, formatHora(p.timestamp), etiqueta(p.clasificacion),
    p.confianza != null ? (p.confianza * 100).toFixed(1) + '%' : '',
    p.sensor?.r ?? '', p.sensor?.g ?? '', p.sensor?.b ?? '', p.sensor?.lux ?? '',
    p.sensor?.temp ?? '', p.sensor?.humedad ?? '',
  ])
  const csv = [headers, ...rows].map(r => r.join(',')).join('\n')
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url; a.download = `${store.loteActivo?.codigo || 'lote'}.csv`; a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
/* ── Tokens HMI (color reservado al ESTADO) ── */
.lote-root {
  --ok:        var(--verde-acento);
  --ok-ink:    #2f7d32;
  --warn:      #b8730a;      /* ámbar legible para texto */
  --warn-bg:   #fbf0dc;
  --alarm:     #d11a1a;      /* rojo SATURADO: solo alarmas */
  --alarm-bg:  #fbe3e3;
  --idle:      var(--texto-secondary);
  --rej-soft:  rgba(217,83,79,0.10);  /* rechazo rutinario (marcador tenue) */
  --page:      #eceef1;      /* neutro apagado */

  display: flex;
  flex-direction: column;
  height: 100%;
  background: var(--page);
}
.num { font-variant-numeric: tabular-nums; }

/* ── Sin lote ── */
.no-lote { flex:1; display:flex; align-items:center; justify-content:center; padding:24px; }
.no-lote-card {
  background: var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:16px;
  padding:48px 40px; text-align:center; max-width:440px; width:100%; box-shadow:0 2px 12px rgba(0,0,0,.06);
}
.no-lote-card h2 { margin:16px 0 8px; font-size:1.5rem; }
.no-lote-card p { color:var(--texto-secondary); margin-bottom:24px; }
.no-lote-form { display:flex; flex-direction:column; gap:12px; }
.no-lote-form input { padding:14px; border:1px solid var(--borde-cards); border-radius:10px; font-size:1.05rem; outline:none; }
.no-lote-form input:focus-visible { border-color:var(--ok); box-shadow:0 0 0 3px rgba(92,184,92,.25); }
.error-msg { color:var(--alarm); font-size:.9rem; margin-top:8px; }

/* ── A) Franja de estado ── */
.status-strip {
  display:flex; align-items:center; gap:10px; flex-wrap:wrap;
  background: var(--fondo-cards); border-bottom:1px solid var(--borde-cards);
  padding:8px 16px; flex-shrink:0;
}
.chip {
  display:inline-flex; align-items:center; gap:6px;
  font-size:.82rem; color:var(--texto-primary);
  background:#f1f3f5; border:1px solid var(--borde-cards);
  border-radius:20px; padding:5px 11px; min-height:30px; white-space:nowrap;
}
.chip b { font-weight:700; }
.chip-ico { font-size:.85rem; opacity:.85; }
.chip-dot { width:8px; height:8px; border-radius:50%; background:var(--idle); }
.chip-dot--pulse { background:var(--ok); animation:pulse-dot 1.6s ease infinite; }
.chip--ok    { }
.chip--ok    b { color:var(--ok-ink); }
.chip--idle  { color:var(--idle); }
.chip--warn  { background:var(--warn-bg); border-color:#f0d9a8; }
.chip--warn  b { color:var(--warn); }
.chip--alarm { background:var(--alarm-bg); border-color:#f2b8b8; }
.chip--alarm b { color:var(--alarm); }
.status-spacer { flex:1; }
.status-lote { font-family:'Courier New',monospace; font-size:.9rem; }
.status-timer { font-family:'Courier New',monospace; font-size:.9rem; color:var(--texto-secondary); }

/* Banner único */
.alert-banner {
  display:flex; align-items:center; gap:12px; padding:12px 18px; flex-shrink:0;
  border-bottom:1px solid var(--borde-cards);
}
.alert--alarm { background:var(--alarm-bg); color:var(--alarm); }
.alert--warn  { background:var(--warn-bg);  color:var(--warn); }
.alert-ico { font-size:1.5rem; }
.alert-text { display:flex; flex-direction:column; line-height:1.25; }
.alert-text strong { font-size:1rem; }
.alert-text span { font-size:.85rem; opacity:.9; color:var(--texto-primary); }

/* ── Cuerpo ── */
.dash-body { flex:1; overflow:auto; padding:16px; display:flex; flex-direction:column; gap:16px; }

.card {
  background:var(--fondo-cards); border:1px solid var(--borde-cards);
  border-radius:12px; box-shadow:0 1px 3px rgba(0,0,0,.05);
}
.card-title { font-size:.95rem; font-weight:700; padding:14px 18px 0; margin-bottom:10px; }

/* ── B+C) HERO + KPIs ── */
.kpi-row { display:grid; grid-template-columns:2fr 1fr 1fr 1fr; gap:16px; align-items:stretch; }

.hero-card {
  border-radius:14px; padding:18px 22px; color:#fff;
  display:flex; flex-direction:column; justify-content:center; gap:6px;
  background:linear-gradient(135deg, #4c9a4c, var(--ok-ink));
  box-shadow:0 2px 10px rgba(0,0,0,.08);
}
.hero--warn  { background:linear-gradient(135deg, #d99a3a, var(--warn)); }
.hero--alarm { background:linear-gradient(135deg, #e04b4b, var(--alarm)); animation:hero-alarm 1.6s ease infinite; }
@keyframes hero-alarm { 0%,100%{ box-shadow:0 2px 10px rgba(209,26,26,.25);} 50%{ box-shadow:0 2px 22px rgba(209,26,26,.6);} }
.hero-head { display:flex; justify-content:space-between; align-items:center; }
.hero-label { font-size:.95rem; font-weight:600; opacity:.95; }
.hero-state { font-size:.78rem; font-weight:800; background:rgba(255,255,255,.22); padding:2px 10px; border-radius:20px; }
.hero-value { font-size:4rem; font-weight:800; line-height:1; }
.hero-pct { font-size:1.6rem; font-weight:700; margin-left:2px; }
.hero-sub { display:flex; gap:16px; margin-top:4px; }
.hero-tag { font-size:.9rem; opacity:.96; }
.hero-tag b { font-size:1.05rem; }

.kpi-mini { background:var(--fondo-cards); border:1px solid var(--borde-cards); border-radius:12px; padding:16px; display:flex; flex-direction:column; gap:8px; }
.kpi-label { font-size:.82rem; color:var(--texto-secondary); font-weight:600; }
.kpi-value-row { display:flex; align-items:baseline; gap:6px; }
.kpi-number { font-size:2.1rem; font-weight:800; line-height:1; }
.kpi-meta { font-size:.8rem; color:var(--texto-secondary); }
.txt--ok { color:var(--ok-ink); } .txt--warn { color:var(--warn); } .txt--idle { color:var(--idle); }

.progress-bar { height:8px; background:var(--borde-cards); border-radius:4px; overflow:hidden; }
.progress-fill { height:100%; background:var(--ok); border-radius:4px; transition:width .4s ease; }
.fill--warn { background:var(--warn); } .fill--ok { background:var(--ok); } .fill--idle { background:var(--idle); }

.sparkline { display:flex; align-items:flex-end; gap:3px; height:34px; }
.spark-bar { flex:1; background:rgba(92,184,92,.4); border-radius:2px 2px 0 0; transition:height .3s; }

/* ── Dos columnas ── */
.two-col { display:grid; grid-template-columns:3fr 2fr; gap:16px; }
.col-left, .col-right { display:flex; flex-direction:column; gap:16px; }

/* Cámara */
.camara-card { padding-bottom:14px; }
.camara-head { display:flex; align-items:center; justify-content:space-between; padding-right:18px; }
.verdict-badge { font-size:.9rem; font-weight:800; padding:5px 14px; border-radius:20px; }
.verdict--ok  { background:rgba(92,184,92,.15); color:var(--ok-ink); }
.verdict--rej { background:var(--rej-soft); color:var(--rojo-rechazo); }
.camara-wrap { margin:0 18px; border-radius:10px; overflow:hidden; background:#20242a; border:1px solid var(--borde-cards); position:relative; }
.camara-img { display:block; width:100%; height:320px; object-fit:contain; background:#20242a; }
.baja-conf { position:absolute; top:10px; left:10px; background:var(--warn-bg); color:var(--warn); font-weight:700; font-size:.82rem; padding:5px 12px; border-radius:8px; box-shadow:0 1px 4px rgba(0,0,0,.2); }
.camara-meta { font-family:'Courier New',monospace; font-size:.8rem; color:var(--texto-secondary); padding:8px 12px; background:var(--fondo-cards); }
.camara-thumbs { display:flex; gap:8px; padding:8px 12px 4px; background:var(--fondo-cards); }
.camara-thumb { width:72px; height:72px; object-fit:contain; background:#20242a; border-radius:6px; border:1px solid var(--borde-cards); }
.camara-empty { height:320px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:10px; color:var(--texto-secondary); }
.camara-empty-icon { font-size:2.6rem; opacity:.5; }

/* Última clasificación */
.ultima-card { padding:0 18px 18px; }
.result-display { border-radius:12px; padding:22px; display:flex; flex-direction:column; align-items:center; gap:6px; margin-bottom:14px; }
.result--ok  { background:rgba(92,184,92,.12); border:2px solid rgba(92,184,92,.3); }
.result--rej { background:var(--rej-soft); border:2px solid rgba(217,83,79,.3); }
.result-icon { font-size:2.6rem; font-weight:800; line-height:1; }
.result--ok  .result-icon { color:var(--ok-ink); }
.result--rej .result-icon { color:var(--rojo-rechazo); }
.result-text { font-size:1.5rem; font-weight:800; }
.result--ok  .result-text { color:var(--ok-ink); }
.result--rej .result-text { color:var(--rojo-rechazo); }
.conf-wrap { margin-bottom:8px; }
.conf-head { display:flex; justify-content:space-between; font-size:.85rem; color:var(--texto-secondary); margin-bottom:6px; }
.timestamp-label { font-size:.78rem; color:var(--texto-secondary); text-align:center; }

/* Sensores / ambiente */
.sensores-card { padding-bottom:14px; }
.riesgo-row { display:flex; align-items:center; justify-content:space-between; padding:0 18px 12px; }
.riesgo-label { font-size:.88rem; color:var(--texto-secondary); }
.riesgo-badge { font-size:1rem; font-weight:800; padding:6px 16px; border-radius:20px; }
.riesgo--ok    { background:rgba(92,184,92,.15); color:var(--ok-ink); }
.riesgo--warn  { background:var(--warn-bg); color:var(--warn); }
.riesgo--alarm { background:var(--alarm-bg); color:var(--alarm); }
.riesgo--idle  { background:#f1f3f5; color:var(--idle); }
.amb-grid { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; padding:0 18px; }
.amb-tile { background:var(--page); border:1px solid var(--borde-cards); border-radius:10px; padding:12px; display:flex; flex-direction:column; align-items:center; gap:4px; }
.amb-ico { font-size:1.3rem; }
.amb-swatch { width:26px; height:26px; border-radius:6px; border:1px solid var(--borde-cards); }
.amb-value { font-size:1.2rem; font-weight:800; }
.amb-value--sm { font-size:.95rem; }
.amb-label { font-size:.72rem; color:var(--texto-secondary); }
.rgb-details { padding:10px 18px 0; font-size:.82rem; color:var(--texto-secondary); }
.rgb-details summary { cursor:pointer; }
.rgb-values { display:flex; gap:14px; padding-top:6px; }

/* Tabla */
.table-card { padding-bottom:0; overflow:hidden; }
.table-wrap { overflow-y:auto; max-height:260px; }
.paltas-table { width:100%; border-collapse:collapse; font-size:.85rem; }
.paltas-table thead { background:var(--page); position:sticky; top:0; z-index:1; }
.paltas-table th { padding:9px 12px; text-align:left; color:var(--texto-secondary); font-weight:600; border-bottom:1px solid var(--borde-cards); }
.paltas-table td { padding:10px 12px; border-bottom:1px solid var(--borde-cards); }
.row--ok  { background:rgba(92,184,92,.04); }
.row--rej { background:rgba(217,83,79,.05); }
.tbadge { font-size:.75rem; font-weight:700; padding:2px 8px; border-radius:12px; }
.tbadge--ok  { background:rgba(92,184,92,.15); color:var(--ok-ink); }
.tbadge--rej { background:var(--rej-soft); color:var(--rojo-rechazo); }

.empty-state { padding:20px; text-align:center; color:var(--texto-secondary); font-size:.9rem; }

/* Footer */
.bottom-bar { min-height:58px; background:var(--fondo-cards); border-top:1px solid var(--borde-cards); display:flex; align-items:center; justify-content:space-between; padding:8px 18px; flex-shrink:0; gap:12px; flex-wrap:wrap; }
.foot-sum { color:var(--texto-secondary); font-size:.9rem; }
.bottom-actions { display:flex; gap:10px; }

/* Botones (área táctil >=44px) */
.btn { min-height:44px; padding:10px 20px; border-radius:10px; border:none; font-size:.95rem; font-weight:700; transition:filter .15s, opacity .15s; }
.btn:disabled { opacity:.6; cursor:default; }
.btn:focus-visible { outline:3px solid rgba(92,184,92,.5); outline-offset:2px; }
.btn--lg { font-size:1.05rem; }
.btn--primary { background:var(--ok); color:#fff; }
.btn--primary:hover:not(:disabled) { filter:brightness(.95); }
.btn--danger { background:var(--alarm); color:#fff; }
.btn--danger:hover { filter:brightness(.95); }
.btn--danger:focus-visible { outline-color:rgba(209,26,26,.5); }
.btn--ghost { background:transparent; border:1px solid var(--ok); color:var(--ok-ink); }
.btn--ghost:hover { background:rgba(92,184,92,.08); }

/* Responsive */
@media (max-width:1100px) {
  .kpi-row { grid-template-columns:1fr 1fr; }
  .hero-card { grid-column:1 / -1; }
  .two-col  { grid-template-columns:1fr; }
}
@media (max-width:640px) {
  .kpi-row { grid-template-columns:1fr; }
  .amb-grid { grid-template-columns:repeat(3,1fr); }
  .hero-value { font-size:3.2rem; }
}
</style>
