<template>
  <div class="historial-root">

    <!-- Header -->
    <div class="page-header">
      <h1>Historial de Lotes</h1>
      <p>Consulta y analiza lotes anteriores procesados</p>
    </div>

    <!-- Filters -->
    <div class="card filter-card">
      <div class="filter-row">
        <div class="search-wrap">
          <span class="search-icon">🔍</span>
          <input
            v-model="busqueda"
            type="text"
            placeholder="Buscar por código de lote..."
          />
        </div>
        <div class="date-wrap">
          <input type="date" v-model="fechaDesde" />
          <span class="label-gray">→</span>
          <input type="date" v-model="fechaHasta" />
        </div>
        <select v-model="filtroRechazo">
          <option value="">Todos los % rechazo</option>
          <option value="bajo">Bajo (&lt; 15%)</option>
          <option value="medio">Medio (15–30%)</option>
          <option value="alto">Alto (&gt; 30%)</option>
        </select>
      </div>
    </div>

    <!-- Summary cards -->
    <div class="summary-grid">
      <div class="card summary-card">
        <p class="sum-label">Total Lotes Procesados</p>
        <p class="sum-value">{{ store.historial.length }}</p>
      </div>
      <div class="card summary-card">
        <p class="sum-label">Promedio Tasa Rechazo</p>
        <p class="sum-value" style="color: var(--rojo-rechazo)">{{ promedioRechazo }}%</p>
      </div>
      <div class="card summary-card">
        <p class="sum-label">Mejor Lote</p>
        <p class="sum-value" style="color: var(--verde-acento)">{{ mejorLote }}</p>
      </div>
    </div>

    <!-- Table -->
    <div class="card table-card">
      <div v-if="store.loading" class="table-loading">Cargando…</div>
      <div v-else class="table-wrap">
        <table class="lotes-table">
          <thead>
            <tr>
              <th>Código</th>
              <th>Fecha</th>
              <th>Paltas</th>
              <th>Sanas</th>
              <th>Rechazadas</th>
              <th>% Rechazo</th>
              <th>T° Prom</th>
              <th>HR Prom</th>
              <th>Confianza</th>
              <th>Ver</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="(lote, idx) in lotesFiltrados"
              :key="lote.id || lote.codigo"
              :class="idx % 2 === 0 ? 'row--par' : 'row--impar'"
              @click="abrirDetalle(lote)"
              class="row--clickable"
            >
              <td class="mono-sm bold">{{ lote.codigo }}</td>
              <td class="col-gray">{{ formatFecha(lote.inicio) }}</td>
              <td>{{ lote.total }}</td>
              <td style="color: var(--verde-acento)">{{ lote.sanas }}</td>
              <td style="color: var(--rojo-rechazo)">{{ lote.rechazadas }}</td>
              <td>
                <span class="badge-pill" :class="badgeRechazo(tasaRechazo(lote)).clase">
                  {{ tasaRechazo(lote) }}%
                </span>
              </td>
              <td class="mono-sm col-gray">{{ lote.temp_promedio?.toFixed(1) ?? '—' }}°C</td>
              <td class="mono-sm col-gray">{{ lote.humedad_promedio ?? '—' }}%</td>
              <td class="mono-sm">{{ lote.confianza_promedio?.toFixed(1) ?? '—' }}%</td>
              <td>
                <button class="btn-eye" title="Ver detalle" @click.stop="abrirDetalle(lote)">👁</button>
              </td>
            </tr>
            <tr v-if="!lotesFiltrados.length">
              <td colspan="10" class="empty-state">No hay lotes que coincidan con los filtros</td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- Detail drawer -->
    <div v-if="loteSeleccionado" class="drawer-overlay" @click.self="loteSeleccionado = null">
      <div class="drawer">
        <div class="drawer-header">
          <span class="mono-sm bold" style="font-size:1rem">{{ loteSeleccionado.codigo }}</span>
          <button class="btn-close" @click="loteSeleccionado = null">✕</button>
        </div>
        <div class="drawer-body">
          <h3 class="drawer-section-title">Resumen del Lote</h3>
          <div class="detail-grid">
            <div class="detail-item">
              <p class="detail-label">Total Paltas</p>
              <p class="detail-value">{{ loteSeleccionado.total }}</p>
            </div>
            <div class="detail-item">
              <p class="detail-label">% Rechazo</p>
              <p class="detail-value" style="color: var(--rojo-rechazo)">{{ tasaRechazo(loteSeleccionado) }}%</p>
            </div>
            <div class="detail-item">
              <p class="detail-label">Temperatura Media</p>
              <p class="detail-value">{{ loteSeleccionado.temp_promedio?.toFixed(1) ?? '—' }}°C</p>
            </div>
            <div class="detail-item">
              <p class="detail-label">Confianza Media</p>
              <p class="detail-value">{{ loteSeleccionado.confianza_promedio?.toFixed(1) ?? '—' }}%</p>
            </div>
            <div class="detail-item">
              <p class="detail-label">Paltas Sanas</p>
              <p class="detail-value" style="color: var(--verde-acento)">{{ loteSeleccionado.sanas }}</p>
            </div>
            <div class="detail-item">
              <p class="detail-label">Rechazadas</p>
              <p class="detail-value" style="color: var(--rojo-rechazo)">{{ loteSeleccionado.rechazadas }}</p>
            </div>
          </div>
          <button class="btn btn--green" @click="exportarCSVLote(loteSeleccionado)">
            ↓ Exportar datos del lote (CSV)
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { usePaltaStore } from '../stores/palta'

const store = usePaltaStore()
const busqueda    = ref('')
const fechaDesde  = ref('')
const fechaHasta  = ref('')
const filtroRechazo = ref('')
const loteSeleccionado = ref(null)

onMounted(() => store.fetchHistorial())

// ── Helpers ──────────────────────────────────────────────
function tasaRechazo(lote) {
  if (!lote.total) return 0
  const r = lote.rechazadas ?? (lote.total - (lote.sanas ?? 0))
  return Number(((r / lote.total) * 100).toFixed(1))
}

function badgeRechazo(pct) {
  if (pct < 15) return { clase: 'badge-pill--verde' }
  if (pct < 30) return { clase: 'badge-pill--amarillo' }
  return { clase: 'badge-pill--rojo' }
}

function formatFecha(ts) {
  if (!ts) return '—'
  return new Date(ts).toLocaleString('es-ES', { day:'2-digit', month:'2-digit', year:'numeric', hour:'2-digit', minute:'2-digit' })
}

// ── Computed ─────────────────────────────────────────────
const lotesFiltrados = computed(() => {
  let list = store.historial.filter(l =>
    l.codigo.toLowerCase().includes(busqueda.value.toLowerCase())
  )

  if (fechaDesde.value) {
    list = list.filter(l => l.inicio && new Date(l.inicio) >= new Date(fechaDesde.value))
  }

  if (fechaHasta.value) {
    list = list.filter(l => l.inicio && new Date(l.inicio) <= new Date(fechaHasta.value + 'T23:59:59'))
  }

  if (filtroRechazo.value) {
    list = list.filter(l => {
      const t = tasaRechazo(l)
      if (filtroRechazo.value === 'bajo')  return t < 15
      if (filtroRechazo.value === 'medio') return t >= 15 && t < 30
      if (filtroRechazo.value === 'alto')  return t >= 30
      return true
    })
  }

  return [...list].sort((a, b) => new Date(b.inicio) - new Date(a.inicio))
})

const promedioRechazo = computed(() => {
  if (!store.historial.length) return '0.0'
  const sum = store.historial.reduce((s, l) => s + tasaRechazo(l), 0)
  return (sum / store.historial.length).toFixed(1)
})

const mejorLote = computed(() => {
  if (!store.historial.length) return '—'
  const best = store.historial.reduce((min, l) => tasaRechazo(l) < tasaRechazo(min) ? l : min)
  return `${tasaRechazo(best)}%`
})

// ── Acciones ─────────────────────────────────────────────
function abrirDetalle(lote) {
  loteSeleccionado.value = lote
}

function exportarCSVLote(lote) {
  const headers = ['Código', 'Inicio', 'Fin', 'Total', 'Sanas', 'Rechazadas', '% Rechazo', 'Temp Prom', 'HR Prom', 'Confianza']
  const row = [
    lote.codigo,
    lote.inicio || '',
    lote.fin || '',
    lote.total,
    lote.sanas,
    lote.rechazadas,
    tasaRechazo(lote) + '%',
    lote.temp_promedio?.toFixed(1) || '',
    lote.humedad_promedio || '',
    lote.confianza_promedio?.toFixed(1) || '',
  ]
  const csv = [headers, row].map(r => r.join(',')).join('\n')
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${lote.codigo}.csv`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
.historial-root {
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.page-header h1 {
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--texto-primary);
}

.page-header p {
  color: var(--texto-secondary);
  font-size: 0.88rem;
  margin-top: 4px;
}

/* ── Filter card ── */
.card {
  background: var(--fondo-cards);
  border: 1px solid var(--borde-cards);
  border-radius: 12px;
  box-shadow: 0 1px 4px rgba(0,0,0,0.04);
}

.filter-card { padding: 16px 20px; }

.filter-row {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: center;
}

.search-wrap {
  flex: 1;
  min-width: 200px;
  position: relative;
  display: flex;
  align-items: center;
}

.search-icon {
  position: absolute;
  left: 10px;
  font-size: 0.9rem;
}

.search-wrap input {
  width: 100%;
  padding: 8px 12px 8px 32px;
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  font-size: 0.88rem;
  outline: none;
  transition: border-color 0.15s;
}

.search-wrap input:focus { border-color: var(--verde-acento); }

.date-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.date-wrap input {
  padding: 8px 10px;
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  font-size: 0.85rem;
  color: var(--texto-primary);
  outline: none;
}

.date-wrap input:focus { border-color: var(--verde-acento); }

.filter-row select {
  padding: 8px 12px;
  border: 1px solid var(--borde-cards);
  border-radius: 8px;
  font-size: 0.85rem;
  color: var(--texto-primary);
  background: #fff;
  outline: none;
  cursor: pointer;
}

/* ── Summary ── */
.summary-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
}

.summary-card {
  padding: 20px 24px;
}

.sum-label {
  font-size: 0.82rem;
  color: var(--texto-secondary);
  margin-bottom: 8px;
}

.sum-value {
  font-size: 2rem;
  font-weight: 600;
  color: var(--texto-primary);
}

/* ── Table ── */
.table-card { overflow: hidden; }

.table-loading {
  padding: 40px;
  text-align: center;
  color: var(--texto-secondary);
}

.table-wrap {
  overflow-x: auto;
  max-height: 580px;
  overflow-y: auto;
}

.lotes-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.84rem;
}

.lotes-table thead {
  background: var(--fondo-pagina);
  position: sticky;
  top: 0;
  z-index: 1;
}

.lotes-table th {
  padding: 12px 16px;
  text-align: left;
  color: var(--texto-secondary);
  font-weight: 500;
  border-bottom: 1px solid var(--borde-cards);
  white-space: nowrap;
}

.lotes-table td {
  padding: 14px 16px;
  border-bottom: 1px solid var(--borde-cards);
  color: var(--texto-primary);
  white-space: nowrap;
}

.row--par    { background: #fff; }
.row--impar  { background: #fcfcfc; }

.row--clickable {
  cursor: pointer;
  transition: background 0.12s;
}

.row--clickable:hover { background: var(--fondo-pagina) !important; }

.mono-sm {
  font-family: 'Courier New', monospace;
  font-size: 0.82rem;
}

.bold { font-weight: 600; }
.col-gray { color: var(--texto-secondary); }

/* Badges */
.badge-pill {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 20px;
  font-size: 0.75rem;
  font-weight: 600;
  letter-spacing: 0.03em;
}

.badge-pill--verde    { background: rgba(92,184,92,0.12);  color: var(--verde-acento); }
.badge-pill--rojo     { background: rgba(217,83,79,0.12);  color: var(--rojo-rechazo); }
.badge-pill--amarillo { background: rgba(240,173,78,0.15); color: #c87f0a; }

.btn-eye {
  background: none;
  border: none;
  cursor: pointer;
  font-size: 1rem;
  padding: 4px 8px;
  border-radius: 6px;
  transition: background 0.12s;
}

.btn-eye:hover { background: rgba(92,184,92,0.1); }

.empty-state {
  padding: 40px;
  text-align: center;
  color: var(--texto-secondary);
  font-size: 0.88rem;
}

/* ── Drawer ── */
.drawer-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.35);
  z-index: 500;
  display: flex;
  justify-content: flex-end;
}

.drawer {
  width: 400px;
  max-width: 95vw;
  height: 100%;
  background: #fff;
  display: flex;
  flex-direction: column;
  animation: slideIn 0.22s ease;
}

@keyframes slideIn {
  from { transform: translateX(100%); }
  to   { transform: translateX(0); }
}

.drawer-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 20px 24px;
  border-bottom: 1px solid var(--borde-cards);
}

.btn-close {
  background: none;
  border: none;
  cursor: pointer;
  font-size: 1rem;
  color: var(--texto-secondary);
  padding: 4px 8px;
  border-radius: 6px;
}

.btn-close:hover { background: var(--fondo-pagina); }

.drawer-body {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.drawer-section-title {
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--texto-primary);
  margin-bottom: 12px;
}

.detail-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.detail-item {
  background: var(--fondo-pagina);
  border-radius: 8px;
  padding: 12px 14px;
}

.detail-label {
  font-size: 0.75rem;
  color: var(--texto-secondary);
  margin-bottom: 4px;
}

.detail-value {
  font-size: 1.2rem;
  font-weight: 600;
  color: var(--texto-primary);
}

.label-gray { color: var(--texto-secondary); }

/* Buttons */
.btn {
  padding: 10px 20px;
  border-radius: 8px;
  border: none;
  font-size: 0.88rem;
  font-weight: 500;
  cursor: pointer;
  width: 100%;
  transition: background 0.15s;
}

.btn--green {
  background: var(--verde-acento);
  color: #fff;
}

.btn--green:hover { background: #4da64d; }

/* ── Responsive ── */
@media (max-width: 900px) {
  .summary-grid { grid-template-columns: 1fr 1fr; }
}

@media (max-width: 640px) {
  .historial-root   { padding: 16px; }
  .summary-grid     { grid-template-columns: 1fr; }
  .filter-row       { flex-direction: column; }
  .search-wrap      { width: 100%; }
  .date-wrap        { width: 100%; flex-wrap: wrap; }
}
</style>
