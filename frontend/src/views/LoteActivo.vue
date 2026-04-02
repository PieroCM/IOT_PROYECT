<template>
  <div class="lote-root">

    <!-- ═══ PANTALLA: SIN LOTE ACTIVO ═══ -->
    <div v-if="!store.loteActivo" class="no-lote">
      <div class="no-lote-card">
        <svg width="56" height="56" viewBox="0 0 24 24" fill="none" stroke="var(--verde-acento)" stroke-width="1.5">
          <path d="M12 2C8 2 4 6 4 10c0 5 8 12 8 12s8-7 8-12c0-4-4-8-8-8z"/>
        </svg>
        <h2>No hay lote activo</h2>
        <p>Ingresa un código para iniciar el proceso de clasificación</p>
        <div class="no-lote-form">
          <input
            v-model="nuevoCodigo"
            type="text"
            placeholder="Ej: LOTE-2024-008"
            @keyup.enter="abrirLote"
          />
          <button class="btn btn--primary" @click="abrirLote" :disabled="store.loading">
            {{ store.loading ? 'Abriendo…' : 'Abrir nuevo lote' }}
          </button>
        </div>
        <p v-if="store.error" class="error-msg">{{ store.error }}</p>
      </div>
    </div>

    <!-- ═══ PANTALLA: LOTE ACTIVO ═══ -->
    <template v-else>
      <!-- Header -->
      <header class="dash-header">
        <div class="dash-header__left">
          <span class="label-gray">Lote Activo —</span>
          <span class="codigo-mono">#{{ store.loteActivo.codigo }}</span>
        </div>
        <div class="dash-header__center">
          <span class="badge-proceso">
            <span class="dot dot--pulse" />
            EN PROCESO
          </span>
        </div>
        <div class="dash-header__right">
          <span class="timer-display">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>
            </svg>
            {{ elapsedTime }}
          </span>
          <button class="btn btn--danger-outline" @click="cerrarLote">Cerrar Lote</button>
        </div>
      </header>

      <!-- Scrollable content -->
      <div class="dash-body">

        <!-- KPI Cards -->
        <div class="kpi-grid">
          <!-- Card 1: Procesadas -->
          <div class="card kpi-card">
            <p class="kpi-label">Paltas Procesadas</p>
            <div class="kpi-value-row">
              <span class="kpi-number">{{ kpis.total }}</span>
              <span class="label-gray">/ 50</span>
            </div>
            <div class="progress-bar">
              <div class="progress-fill" :style="{ width: Math.min((kpis.total / 50) * 100, 100) + '%' }" />
            </div>
            <p class="kpi-sub">de este lote</p>
          </div>

          <!-- Card 2: Rechazo -->
          <div class="card kpi-card">
            <p class="kpi-label">Tasa de Rechazo</p>
            <div class="kpi-value-row">
              <span class="kpi-number" style="color: var(--rojo-rechazo)">{{ kpis.tasa_rechazo }}%</span>
              <span
                class="badge-pill"
                :class="kpis.tasa_rechazo > 15 ? 'badge-pill--rojo' : 'badge-pill--verde'"
              >
                {{ kpis.tasa_rechazo > 15 ? 'ALTO' : 'NORMAL' }}
              </span>
            </div>
          </div>

          <!-- Card 3: Velocidad -->
          <div class="card kpi-card">
            <p class="kpi-label">Velocidad de Línea</p>
            <div class="kpi-value-row">
              <span class="kpi-number" style="color: var(--verde-acento)">{{ store.velocidadPpm }}</span>
              <span class="label-gray" style="font-size:0.8rem">ppm</span>
            </div>
            <div class="sparkline">
              <div
                v-for="(v, i) in speedHistory"
                :key="i"
                class="spark-bar"
                :style="{ height: Math.max((v / maxSpeed) * 100, 8) + '%' }"
              />
            </div>
          </div>

          <!-- Card 4: Confianza -->
          <div class="card kpi-card">
            <p class="kpi-label">Confianza del Modelo</p>
            <div class="kpi-value-row">
              <span class="kpi-number">{{ kpis.confianza_promedio !== null ? (kpis.confianza_promedio * 100).toFixed(1) : '—' }}%</span>
              <svg width="56" height="56" viewBox="0 0 64 64" class="donut">
                <circle cx="32" cy="32" r="26" fill="none" stroke="var(--borde-cards)" stroke-width="7"/>
                <circle
                  cx="32" cy="32" r="26" fill="none"
                  stroke="var(--verde-acento)" stroke-width="7"
                  stroke-dasharray="163.36" stroke-dashoffset="0"
                  :style="{ strokeDashoffset: 163.36 - (confianzaPct / 100) * 163.36 }"
                  stroke-linecap="round"
                  transform="rotate(-90 32 32)"
                />
              </svg>
            </div>
          </div>
        </div>

        <!-- Two-column layout -->
        <div class="two-col">
          <!-- LEFT -->
          <div class="col-left">
            <!-- Chart -->
            <div class="card chart-card">
              <h3 class="card-title">Evolución de rechazos</h3>
              <div class="chart-wrap">
                <Line v-if="chartData.labels.length > 1" :data="chartData" :options="chartOptions" />
                <div v-else class="chart-empty">Sin datos suficientes aún…</div>
              </div>
              <div class="chart-legend">
                <span class="legend-item legend-item--rojo">% Rechazo acumulado</span>
                <span class="legend-item legend-item--verde">Confianza promedio</span>
              </div>
            </div>

            <!-- Sensor mini cards -->
            <div class="sensor-row">
              <div class="card sensor-card">
                <div class="sensor-card__inner">
                  <span class="sensor-icon">🌡️</span>
                  <span class="sensor-label">Temperatura</span>
                </div>
                <span class="sensor-value">
                  {{ kpis.temp_promedio !== null ? kpis.temp_promedio + '°C' : '—' }}
                </span>
              </div>
              <div class="card sensor-card">
                <div class="sensor-card__inner">
                  <span class="sensor-icon">💧</span>
                  <span class="sensor-label">Humedad</span>
                </div>
                <span class="sensor-value">
                  {{ kpis.humedad_promedio !== null ? kpis.humedad_promedio + '%' : '—' }}
                </span>
              </div>
              <div class="card sensor-card">
                <div class="sensor-card__inner">
                  <span class="sensor-icon">⚠️</span>
                  <span class="sensor-label">Riesgo ambiental</span>
                </div>
                <span class="badge-pill" :class="riesgoBadge.clase">{{ riesgoBadge.label }}</span>
              </div>
            </div>
          </div>

          <!-- RIGHT -->
          <div class="col-right">
            <!-- Última palta -->
            <div class="card ultima-card">
              <h3 class="card-title">Última Palta Clasificada</h3>
              <div v-if="store.ultimaPalta">
                <div
                  class="result-display"
                  :class="store.ultimaPalta.clasificacion === 'sana' ? 'result-display--sana' : 'result-display--antracnosis'"
                >
                  <span class="result-icon">
                    {{ store.ultimaPalta.clasificacion === 'sana' ? '✓' : '✕' }}
                  </span>
                  <span class="result-text">
                    {{ store.ultimaPalta.clasificacion?.toUpperCase() || '—' }}
                  </span>
                </div>

                <div class="rgb-block" v-if="store.ultimaPalta.sensor">
                  <p class="label-gray" style="font-size:0.75rem;margin-bottom:6px">Valores RGB</p>
                  <div class="rgb-values">
                    <span>R: {{ store.ultimaPalta.sensor.r ?? '—' }}</span>
                    <span>G: {{ store.ultimaPalta.sensor.g ?? '—' }}</span>
                    <span>B: {{ store.ultimaPalta.sensor.b ?? '—' }}</span>
                    <span style="border-top:1px solid var(--borde-cards);padding-top:4px;margin-top:2px">
                      Lux: {{ store.ultimaPalta.sensor.lux ?? '—' }}
                    </span>
                  </div>
                </div>

                <div class="confianza-bar-wrap">
                  <div class="confianza-bar-header">
                    <span class="label-gray" style="font-size:0.82rem">Confianza</span>
                    <span class="mono-sm">{{ store.ultimaPalta.confianza ? (store.ultimaPalta.confianza * 100).toFixed(0) + '%' : '—' }}</span>
                  </div>
                  <div class="progress-bar">
                    <div
                      class="progress-fill"
                      :style="{ width: (store.ultimaPalta.confianza || 0) * 100 + '%' }"
                    />
                  </div>
                </div>

                <p class="timestamp-label">hace {{ segundosDesdeUltimaPalta }}s</p>
              </div>
              <div v-else class="empty-state">Esperando primera palta…</div>
            </div>

            <!-- Tabla paltas -->
            <div class="card table-card">
              <h3 class="card-title">Paltas del lote</h3>
              <div class="table-wrap">
                <table class="paltas-table">
                  <thead>
                    <tr>
                      <th>#</th><th>Hora</th><th>Resultado</th>
                      <th>Conf.</th><th>T°</th><th>HR</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr
                      v-for="p in ultimasOcho"
                      :key="p.id"
                      :class="p.clasificacion === 'sana' ? 'row--sana' : 'row--antracnosis'"
                    >
                      <td class="mono-sm">{{ p.id }}</td>
                      <td class="mono-sm">{{ formatHora(p.timestamp) }}</td>
                      <td>
                        <span class="badge-pill badge-pill--sm"
                          :class="p.clasificacion === 'sana' ? 'badge-pill--verde' : 'badge-pill--rojo'">
                          {{ p.clasificacion === 'sana' ? 'Sana' : 'Antracnosis' }}
                        </span>
                      </td>
                      <td class="mono-sm">{{ p.confianza ? (p.confianza * 100).toFixed(0) + '%' : '—' }}</td>
                      <td class="mono-sm">{{ p.sensor?.temp?.toFixed(1) ?? '—' }}°</td>
                      <td class="mono-sm">{{ p.sensor?.humedad?.toFixed(0) ?? '—' }}%</td>
                    </tr>
                    <tr v-if="!store.paltas.length">
                      <td colspan="6" class="empty-state">Sin paltas registradas</td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Bottom bar -->
      <footer class="bottom-bar">
        <span class="label-gray">
          {{ kpis.total }} paltas procesadas en {{ elapsedTime }}
        </span>
        <div class="bottom-actions">
          <button class="btn btn--green-outline" @click="exportarCSV">
            ↓ Exportar CSV
          </button>
          <button class="btn btn--danger" @click="cerrarLote">Cerrar Lote</button>
        </div>
      </footer>
    </template>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { Line } from 'vue-chartjs'
import { usePaltaStore } from '../stores/palta'

const store = usePaltaStore()
const nuevoCodigo = ref('')
const elapsedSeconds = ref(0)
let timerInterval = null

// ── Timer ──────────────────────────────────────────────────
function actualizarTimer() {
  if (store.loteActivo?.inicio) {
    elapsedSeconds.value = Math.floor(
      (Date.now() - new Date(store.loteActivo.inicio).getTime()) / 1000
    )
  }
}

onMounted(() => {
  actualizarTimer()
  timerInterval = setInterval(actualizarTimer, 1000)
})

onUnmounted(() => clearInterval(timerInterval))

watch(() => store.loteActivo, actualizarTimer)

const elapsedTime = computed(() => {
  const h = Math.floor(elapsedSeconds.value / 3600)
  const m = Math.floor((elapsedSeconds.value % 3600) / 60)
  const s = elapsedSeconds.value % 60
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`
})

// ── KPIs con defaults seguros ────────────────────────────
const kpis = computed(() => ({
  total:              store.kpis?.total              ?? 0,
  tasa_rechazo:       store.kpis?.tasa_rechazo       ?? 0,
  confianza_promedio: store.kpis?.confianza_promedio ?? null,
  temp_promedio:      store.kpis?.temp_promedio      ?? null,
  humedad_promedio:   store.kpis?.humedad_promedio   ?? null,
}))

const confianzaPct = computed(() => {
  const v = kpis.value.confianza_promedio
  if (v === null) return 0
  return v <= 1 ? v * 100 : v
})

// ── Sparkline velocidad ──────────────────────────────────
const speedHistory = computed(() => {
  const h = store._speedHistory
  if (!h.length) return [1, 1, 1, 1, 1, 1, 1, 1, 1, store.velocidadPpm || 0]
  return h
})
const maxSpeed = computed(() => Math.max(...speedHistory.value, 0.1))

// ── Riesgo ambiental ─────────────────────────────────────
const riesgoBadge = computed(() => {
  const t = kpis.value.temp_promedio
  const h = kpis.value.humedad_promedio
  if (t === null || h === null) return { label: '—', clase: '' }
  if (h < 70 && t < 26) return { label: 'BAJO',  clase: 'badge-pill--verde' }
  if (h < 80 && t < 28) return { label: 'MEDIO', clase: 'badge-pill--amarillo' }
  return { label: 'ALTO', clase: 'badge-pill--rojo' }
})

// ── Última palta / segundos ──────────────────────────────
const segundosDesdeUltimaPalta = computed(() => {
  const p = store.ultimaPalta
  if (!p?.timestamp) return 0
  return Math.floor((Date.now() - new Date(p.timestamp).getTime()) / 1000)
})

// ── Tabla ────────────────────────────────────────────────
const ultimasOcho = computed(() => [...store.paltas].reverse().slice(0, 8))

function formatHora(ts) {
  if (!ts) return '—'
  return new Date(ts).toLocaleTimeString('es-ES', { hour:'2-digit', minute:'2-digit', second:'2-digit' })
}

// ── Chart.js ─────────────────────────────────────────────
const chartData = computed(() => {
  const paltas = store.paltas
  return {
    labels: paltas.map((_, i) => i + 1),
    datasets: [
      {
        label: '% Rechazo acumulado',
        data: paltas.map((_, i) => {
          const slice = paltas.slice(0, i + 1)
          const rej = slice.filter(p => p.clasificacion === 'antracnosis').length
          return Number(((rej / slice.length) * 100).toFixed(1))
        }),
        borderColor: '#D9534F',
        borderDash: [5, 5],
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: false,
      },
      {
        label: 'Confianza promedio',
        data: paltas.map((_, i) => {
          const slice = paltas.slice(0, i + 1)
          const avg = slice.reduce((s, p) => s + (p.confianza ?? 0), 0) / slice.length
          return Number((avg * 100).toFixed(1))
        }),
        borderColor: '#5CB85C',
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.3,
        fill: false,
      },
    ],
  }
})

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  interaction: { mode: 'index', intersect: false },
  plugins: {
    legend: { display: false },
    tooltip: {
      backgroundColor: '#fff',
      borderColor: '#E9ECEF',
      borderWidth: 1,
      titleColor: '#1A1A2E',
      bodyColor: '#6C757D',
    },
  },
  scales: {
    x: {
      title: { display: true, text: 'Número de palta', color: '#6C757D', font: { size: 11 } },
      grid: { color: '#E9ECEF' },
      ticks: { color: '#6C757D' },
    },
    y: {
      min: 0,
      max: 100,
      title: { display: true, text: 'Porcentaje (%)', color: '#6C757D', font: { size: 11 } },
      grid: { color: '#E9ECEF' },
      ticks: { color: '#6C757D' },
    },
  },
}

// ── Acciones ─────────────────────────────────────────────
async function abrirLote() {
  if (!nuevoCodigo.value.trim()) return
  await store.abrirLote(nuevoCodigo.value.trim())
  nuevoCodigo.value = ''
}

async function cerrarLote() {
  if (store.loteActivo?.id) {
    await store.cerrarLote(store.loteActivo.id)
  }
}

function exportarCSV() {
  const headers = ['#', 'Hora', 'Resultado', 'Confianza', 'R', 'G', 'B', 'Lux', 'Temp', 'Humedad']
  const rows = store.paltas.map((p, i) => [
    i + 1,
    formatHora(p.timestamp),
    p.clasificacion || '',
    p.confianza ? (p.confianza * 100).toFixed(1) + '%' : '',
    p.sensor?.r ?? '',
    p.sensor?.g ?? '',
    p.sensor?.b ?? '',
    p.sensor?.lux ?? '',
    p.sensor?.temp ?? '',
    p.sensor?.humedad ?? '',
  ])
  const csv = [headers, ...rows].map(r => r.join(',')).join('\n')
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${store.loteActivo?.codigo || 'lote'}.csv`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
/* ── Layout ── */
.lote-root {
  display: flex;
  flex-direction: column;
  height: 100%;
}

/* ── No-lote screen ── */
.no-lote {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
}

.no-lote-card {
  background: var(--fondo-cards);
  border: 1px solid var(--borde-cards);
  border-radius: 16px;
  padding: 48px 40px;
  text-align: center;
  max-width: 420px;
  width: 100%;
  box-shadow: 0 2px 12px rgba(0,0,0,0.06);
}

.no-lote-card h2 {
  margin: 16px 0 8px;
  color: var(--texto-primary);
  font-size: 1.4rem;
}

.no-lote-card p {
  color: var(--texto-secondary);
  font-size: 0.9rem;
  margin-bottom: 24px;
}

.no-lote-form {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.no-lote-form input {
  padding: 10px 14px;
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  font-size: 0.95rem;
  outline: none;
  transition: border-color 0.15s;
}

.no-lote-form input:focus { border-color: var(--verde-acento); }

.error-msg {
  color: var(--rojo-rechazo);
  font-size: 0.82rem;
  margin-top: 8px;
}

/* ── Dashboard header ── */
.dash-header {
  height: 60px;
  background: var(--fondo-cards);
  border-bottom: 1px solid var(--borde-cards);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  flex-shrink: 0;
  gap: 12px;
}

.dash-header__left {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 0.9rem;
}

.dash-header__right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.label-gray { color: var(--texto-secondary); }

.codigo-mono {
  font-family: 'Courier New', monospace;
  font-weight: 600;
  color: var(--texto-primary);
}

.badge-proceso {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: rgba(92,184,92,0.1);
  color: var(--verde-acento);
  border: 1px solid rgba(92,184,92,0.3);
  border-radius: 20px;
  padding: 4px 12px;
  font-size: 0.78rem;
  font-weight: 600;
  letter-spacing: 0.05em;
}

.dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; }
.dot--pulse { background: var(--verde-acento); animation: pulse-dot 1.5s ease infinite; }

.timer-display {
  display: flex;
  align-items: center;
  gap: 6px;
  font-family: 'Courier New', monospace;
  font-size: 0.88rem;
  color: var(--texto-primary);
}

/* ── Body ── */
.dash-body {
  flex: 1;
  overflow: auto;
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* ── Cards base ── */
.card {
  background: var(--fondo-cards);
  border: 1px solid var(--borde-cards);
  border-radius: 12px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.05);
}

.card-title {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--texto-primary);
  padding: 16px 20px 0;
  margin-bottom: 12px;
}

/* ── KPI grid ── */
.kpi-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.kpi-card {
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.kpi-label { font-size: 0.82rem; color: var(--texto-secondary); }

.kpi-value-row {
  display: flex;
  align-items: baseline;
  gap: 8px;
}

.kpi-number {
  font-size: 2rem;
  font-weight: 600;
  color: var(--texto-primary);
  line-height: 1;
}

.kpi-sub { font-size: 0.75rem; color: var(--texto-secondary); }

/* Progress bar */
.progress-bar {
  height: 6px;
  background: var(--borde-cards);
  border-radius: 3px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: var(--verde-acento);
  border-radius: 3px;
  transition: width 0.4s ease;
}

/* Sparkline */
.sparkline {
  display: flex;
  align-items: flex-end;
  gap: 3px;
  height: 32px;
}

.spark-bar {
  flex: 1;
  background: rgba(92,184,92,0.35);
  border-radius: 2px 2px 0 0;
  transition: height 0.3s;
}

/* Donut gauge */
.donut { transform: scaleX(-1); }

/* ── Badges ── */
.badge-pill {
  display: inline-block;
  padding: 2px 10px;
  border-radius: 20px;
  font-size: 0.72rem;
  font-weight: 700;
  letter-spacing: 0.04em;
}

.badge-pill--verde    { background: rgba(92,184,92,0.12);  color: var(--verde-acento); }
.badge-pill--rojo     { background: rgba(217,83,79,0.12);  color: var(--rojo-rechazo); }
.badge-pill--amarillo { background: rgba(240,173,78,0.15); color: #c87f0a; }
.badge-pill--sm       { font-size: 0.68rem; padding: 1px 8px; }

/* ── Two-column ── */
.two-col {
  display: grid;
  grid-template-columns: 3fr 2fr;
  gap: 20px;
}

.col-left, .col-right {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* Chart */
.chart-card { padding-bottom: 16px; }

.chart-wrap {
  padding: 0 20px;
  height: 260px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.chart-empty {
  color: var(--texto-secondary);
  font-size: 0.85rem;
}

.chart-legend {
  display: flex;
  gap: 20px;
  justify-content: center;
  padding: 8px 20px 0;
}

.legend-item {
  font-size: 0.75rem;
  display: flex;
  align-items: center;
  gap: 6px;
}

.legend-item::before {
  content: '';
  display: inline-block;
  width: 20px;
  height: 2px;
  border-radius: 1px;
}

.legend-item--rojo::before  { background: var(--rojo-rechazo); border-top: 2px dashed var(--rojo-rechazo); }
.legend-item--verde::before { background: var(--verde-acento); }

/* Sensor row */
.sensor-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
}

.sensor-card {
  padding: 14px 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.sensor-card__inner {
  display: flex;
  align-items: center;
  gap: 8px;
}

.sensor-icon { font-size: 1.1rem; }

.sensor-label {
  font-size: 0.8rem;
  color: var(--texto-secondary);
}

.sensor-value {
  font-family: 'Courier New', monospace;
  font-size: 1rem;
  font-weight: 600;
  color: var(--texto-primary);
}

/* Última palta card */
.ultima-card { padding: 0 20px 20px; }

.result-display {
  border-radius: 10px;
  padding: 20px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  margin-bottom: 16px;
}

.result-display--sana        { background: rgba(92,184,92,0.1);  border: 2px solid rgba(92,184,92,0.25); }
.result-display--antracnosis { background: rgba(217,83,79,0.1);  border: 2px solid rgba(217,83,79,0.25); }

.result-icon {
  font-size: 2.4rem;
  line-height: 1;
  font-weight: 700;
}

.result-display--sana .result-icon        { color: var(--verde-acento); }
.result-display--antracnosis .result-icon { color: var(--rojo-rechazo); }

.result-text {
  font-size: 1.2rem;
  font-weight: 700;
}

.result-display--sana .result-text        { color: var(--verde-acento); }
.result-display--antracnosis .result-text { color: var(--rojo-rechazo); }

.rgb-block {
  background: var(--fondo-pagina);
  border-radius: 8px;
  padding: 10px 12px;
  margin-bottom: 12px;
}

.rgb-values {
  font-family: 'Courier New', monospace;
  font-size: 0.82rem;
  color: var(--texto-primary);
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.confianza-bar-wrap { margin-bottom: 10px; }

.confianza-bar-header {
  display: flex;
  justify-content: space-between;
  margin-bottom: 6px;
}

.mono-sm {
  font-family: 'Courier New', monospace;
  font-size: 0.82rem;
  color: var(--texto-primary);
}

.timestamp-label {
  font-size: 0.75rem;
  color: var(--texto-secondary);
  text-align: center;
}

.empty-state {
  padding: 20px;
  text-align: center;
  color: var(--texto-secondary);
  font-size: 0.85rem;
}

/* Tabla */
.table-card { padding-bottom: 0; overflow: hidden; }

.table-wrap {
  overflow-y: auto;
  max-height: 280px;
}

.paltas-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.82rem;
}

.paltas-table thead {
  background: var(--fondo-pagina);
  position: sticky;
  top: 0;
  z-index: 1;
}

.paltas-table th {
  padding: 8px 12px;
  text-align: left;
  color: var(--texto-secondary);
  font-weight: 500;
  border-bottom: 1px solid var(--borde-cards);
}

.paltas-table td {
  padding: 10px 12px;
  border-bottom: 1px solid var(--borde-cards);
}

.row--sana        { background: rgba(92,184,92,0.04); }
.row--antracnosis { background: rgba(217,83,79,0.04); }

/* ── Bottom bar ── */
.bottom-bar {
  height: 60px;
  background: var(--fondo-cards);
  border-top: 1px solid var(--borde-cards);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  flex-shrink: 0;
}

.bottom-actions { display: flex; gap: 10px; }

/* ── Buttons ── */
.btn {
  padding: 8px 18px;
  border-radius: 8px;
  border: none;
  font-size: 0.85rem;
  font-weight: 500;
  transition: background 0.15s, opacity 0.15s;
}

.btn:disabled { opacity: 0.6; cursor: default; }

.btn--primary {
  background: var(--verde-acento);
  color: #fff;
}
.btn--primary:hover:not(:disabled) { background: #4da64d; }

.btn--danger {
  background: var(--rojo-rechazo);
  color: #fff;
}
.btn--danger:hover { background: #c9302c; }

.btn--danger-outline {
  background: transparent;
  border: 1px solid var(--rojo-rechazo);
  color: var(--rojo-rechazo);
}
.btn--danger-outline:hover { background: rgba(217,83,79,0.08); }

.btn--green-outline {
  background: transparent;
  border: 1px solid var(--verde-acento);
  color: var(--verde-acento);
}
.btn--green-outline:hover { background: rgba(92,184,92,0.08); }

/* ── Responsive ── */
@media (max-width: 1100px) {
  .kpi-grid { grid-template-columns: repeat(2, 1fr); }
  .two-col  { grid-template-columns: 1fr; }
}

@media (max-width: 640px) {
  .kpi-grid    { grid-template-columns: 1fr; }
  .sensor-row  { grid-template-columns: 1fr; }
  .dash-header { flex-wrap: wrap; height: auto; padding: 10px 16px; gap: 8px; }
  .dash-body   { padding: 12px 16px; }
  .bottom-bar  { padding: 0 16px; }
}
</style>
