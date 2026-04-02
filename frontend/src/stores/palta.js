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

    async abrirLote(codigo) {
      this.loading = true
      this.error = null
      try {
        const { data } = await axios.post('/api/lote', { codigo })
        this.loteActivo = { ...data, inicio: data.inicio || new Date().toISOString() }
        this.paltas = []
        this.kpis = null
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
        this.detenerPolling()
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
        // fallback mock when backend lacks the endpoint
        this.historial = [
          { id: 7, codigo: 'LOTE-2024-007', inicio: '2026-03-26T10:30:00', fin: '2026-03-26T11:20:00', total: 47, sanas: 38, rechazadas: 9, temp_promedio: 22.4, humedad_promedio: 65, confianza_promedio: 87.4 },
          { id: 6, codigo: 'LOTE-2024-006', inicio: '2026-03-25T15:45:00', fin: '2026-03-25T16:30:00', total: 50, sanas: 46, rechazadas: 4, temp_promedio: 21.8, humedad_promedio: 63, confianza_promedio: 89.2 },
          { id: 5, codigo: 'LOTE-2024-005', inicio: '2026-03-25T09:15:00', fin: '2026-03-25T10:05:00', total: 48, sanas: 42, rechazadas: 6, temp_promedio: 22.1, humedad_promedio: 64, confianza_promedio: 88.1 },
          { id: 4, codigo: 'LOTE-2024-004', inicio: '2026-03-24T14:20:00', fin: '2026-03-24T15:15:00', total: 50, sanas: 35, rechazadas: 15, temp_promedio: 23.2, humedad_promedio: 68, confianza_promedio: 85.3 },
          { id: 3, codigo: 'LOTE-2024-003', inicio: '2026-03-24T08:50:00', fin: '2026-03-24T09:45:00', total: 45, sanas: 41, rechazadas: 4, temp_promedio: 21.5, humedad_promedio: 62, confianza_promedio: 90.1 },
          { id: 2, codigo: 'LOTE-2024-002', inicio: '2026-03-23T16:10:00', fin: '2026-03-23T17:00:00', total: 50, sanas: 45, rechazadas: 5, temp_promedio: 22.0, humedad_promedio: 64, confianza_promedio: 88.7 },
          { id: 1, codigo: 'LOTE-2024-001', inicio: '2026-03-23T11:30:00', fin: '2026-03-23T12:20:00', total: 48, sanas: 44, rechazadas: 4, temp_promedio: 21.9, humedad_promedio: 63, confianza_promedio: 89.5 },
        ]
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
