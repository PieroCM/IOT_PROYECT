import { defineStore } from 'pinia'
import axios from 'axios'

export const usePaltaStore = defineStore('palta', {
  state: () => ({
    loteActivo: null,
    paltas: [],
    kpis: null,
    historial: [],
    esp32Online: false,
    loading: false,
    error: null,
    _pollingId: null,
    _speedHistory: [],
  }),

  getters: {
    ultimaPalta: (state) => state.paltas.length ? state.paltas[state.paltas.length - 1] : null,
    velocidadPpm: (state) => {
      if (!state.loteActivo?.inicio || !state.paltas.length) return 0
      const mins = (Date.now() - new Date(state.loteActivo.inicio).getTime()) / 60000
      return mins > 0 ? Number((state.paltas.length / mins).toFixed(1)) : 0
    },
  },

  actions: {
    async fetchHealth() {
      try {
        await axios.get('/health')
        this.esp32Online = true
      } catch {
        this.esp32Online = false
      }
    },

    // Restaura el lote activo desde el backend (al cargar/refrescar la página),
    // así un F5 NO pierde el lote: se reconecta al que sigue abierto.
    async fetchLoteActivo() {
      try {
        const { data } = await axios.get('/api/lote/activo')
        if (data?.activo) {
          this.loteActivo = { id: data.id, codigo: data.codigo, inicio: data.inicio }
        } else {
          this.loteActivo = null
        }
      } catch { /* silencioso: si falla, deja el estado como está */ }
    },

    async abrirLote(codigo) {
      this.loading = true
      this.error = null
      try {
        // El backend cierra automáticamente cualquier lote remanente abierto
        // antes de crear este nuevo (ver crear_lote en main.py).
        const { data } = await axios.post('/api/lote', { codigo })
        this.loteActivo = { ...data, inicio: data.inicio || new Date().toISOString() }
        this.paltas = []
        this.kpis = null
        this.iniciarPolling()   // asegura el polling activo (idempotente)
        return data
      } catch (e) {
        this.error = e.response?.data?.detail || e.message
        throw e
      } finally {
        this.loading = false
      }
    },

    async cerrarLote(id) {
      this.loading = true
      try {
        await axios.post(`/api/lote/${id}/cerrar`)
        this.loteActivo = null
        this.paltas = []
        this.kpis = null
        // El polling sigue vivo (no hace nada sin loteActivo) para que al abrir
        // otro lote se reanude solo. Se detiene al desmontar la app.
      } catch (e) {
        this.error = e.response?.data?.detail || e.message
      } finally {
        this.loading = false
      }
    },

    async fetchKpis(id) {
      try {
        const { data } = await axios.get(`/api/lote/${id}/kpis`)
        this.kpis = data
        // track speed history for sparkline
        if (this.velocidadPpm > 0) {
          this._speedHistory.push(this.velocidadPpm)
          if (this._speedHistory.length > 10) this._speedHistory.shift()
        }
      } catch { /* silently fail, keep previous kpis */ }
    },

    async fetchPaltas(id) {
      try {
        const { data } = await axios.get(`/api/lote/${id}/paltas`)
        this.paltas = data
      } catch { /* silently fail */ }
    },

    async fetchHistorial() {
      this.loading = true
      try {
        const { data } = await axios.get('/api/lotes')
        this.historial = data
      } catch {
        // Sin datos inventados: si falla, se deja vacío (solo lotes REALES).
        this.historial = []
      } finally {
        this.loading = false
      }
    },

    async registrarPalta(data) {
      try {
        await axios.post('/api/palta', data)
      } catch { /* silent */ }
    },

    iniciarPolling() {
      if (this._pollingId) return
      this._pollingId = setInterval(() => {
        if (this.loteActivo?.id) {
          this.fetchKpis(this.loteActivo.id)
          this.fetchPaltas(this.loteActivo.id)
        }
      }, 3000)
    },

    detenerPolling() {
      if (this._pollingId) {
        clearInterval(this._pollingId)
        this._pollingId = null
      }
    },
  },
})
