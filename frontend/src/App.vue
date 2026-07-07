<template>
  <div class="app-shell">
    <button
      class="mobile-menu-toggle"
      :class="{ 'mobile-menu-toggle--hidden': sidebarOpen }"
      @click="sidebarOpen = true"
      aria-label="Abrir menu"
      title="Abrir menu"
    >
      <PanelLeft :size="21" :stroke-width="1.8" />
    </button>
    <div v-if="sidebarOpen" class="sidebar-overlay" @click="cerrarSidebarMovil" />

    <aside class="sidebar" :class="{ 'sidebar--open': sidebarOpen }">
      <div class="sidebar-logo">
        <button
          class="sidebar-toggle"
          @click="sidebarOpen = !sidebarOpen"
          :aria-label="sidebarOpen ? 'Cerrar menu' : 'Abrir menu'"
          :title="sidebarOpen ? 'Cerrar menu' : 'Abrir menu'"
        >
          <Sprout class="brand-icon" :size="21" :stroke-width="2" />
          <PanelLeft class="panel-icon" :size="21" :stroke-width="1.8" />
        </button>
        <span>PaltaCheck</span>
      </div>

      <nav class="sidebar-nav">
        <RouterLink to="/" @click="cerrarSidebarMovil" :class="{ active: $route.path === '/' }">
          <House :size="18" :stroke-width="2" />
          <span>Lote Activo</span>
        </RouterLink>
        <RouterLink to="/historial" @click="cerrarSidebarMovil" :class="{ active: $route.path === '/historial' }">
          <History :size="18" :stroke-width="2" />
          <span>Historial</span>
        </RouterLink>
      </nav>

      <div class="sidebar-status">
        <button
          class="theme-switch"
          role="switch"
          :aria-checked="theme === 'dark'"
          @click="toggleTheme"
          :title="theme === 'dark' ? 'Tema claro' : 'Tema oscuro'"
        >
          <span class="switch-track">
            <span class="switch-thumb">
              <Sun v-if="theme === 'dark'" :size="13" />
              <Moon v-else :size="13" />
            </span>
          </span>
          <span class="switch-label">{{ theme === 'dark' ? 'Oscuro' : 'Claro' }}</span>
        </button>
        <div
          class="esp32-badge"
          :class="store.esp32Online ? 'esp32-badge--online' : 'esp32-badge--offline'"
          :title="`ESP32 ${store.esp32Online ? 'Conectado' : 'Desconectado'}`"
          :aria-label="`ESP32 ${store.esp32Online ? 'Conectado' : 'Desconectado'}`"
        >
          <span class="dot-wrap">
            <span class="dot" :class="store.esp32Online ? 'dot--green' : 'dot--gray'" />
            <span v-if="store.esp32Online" class="dot ping-ring" style="position:absolute;top:0;left:0;" />
          </span>
          <span>ESP32 {{ store.esp32Online ? 'Conectado' : 'Desconectado' }}</span>
        </div>
      </div>
    </aside>

    <main class="main-content">
      <RouterView />
    </main>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { History, House, Moon, PanelLeft, Sprout, Sun } from '@lucide/vue'
import { usePaltaStore } from './stores/palta'

const store = usePaltaStore()
const sidebarOpen = ref(false)
const theme = ref('light')

let healthInterval = null

function cerrarSidebarMovil() {
  if (window.matchMedia?.('(max-width: 768px)').matches) {
    sidebarOpen.value = false
  }
}

function setTheme(value) {
  theme.value = value
  document.documentElement.dataset.theme = value
  localStorage.setItem('paltacheck-theme', value)
}

function toggleTheme() {
  setTheme(theme.value === 'dark' ? 'light' : 'dark')
}

onMounted(async () => {
  const savedTheme = localStorage.getItem('paltacheck-theme')
  const prefersDark = window.matchMedia?.('(prefers-color-scheme: dark)').matches
  setTheme(savedTheme || (prefersDark ? 'dark' : 'light'))
  store.fetchHealth()
  await store.fetchLoteActivo()
  store.iniciarPolling()
  healthInterval = setInterval(() => store.fetchHealth(), 15000)
})

onUnmounted(() => {
  store.detenerPolling()
  clearInterval(healthInterval)
})
</script>

<style scoped>
.app-shell {
  display: flex;
  height: 100vh;
  overflow: hidden;
  background: var(--fondo-pagina);
}

.sidebar {
  width: 64px;
  height: 100%;
  background: var(--sidebar-bg);
  color: #fff;
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  overflow: hidden;
  transition: width 0.22s ease;
  box-shadow: 1px 0 10px rgba(0,0,0,0.14);
  z-index: 100;
}

.sidebar--open {
  width: 240px;
}

.sidebar-logo {
  display: grid;
  grid-template-columns: 40px 1fr;
  align-items: center;
  gap: 10px;
  min-height: 50px;
  padding: 0 12px;
  font-size: 1.2rem;
  font-weight: 700;
  border-bottom: 1px solid rgba(255,255,255,0.1);
  white-space: nowrap;
}

.sidebar:not(.sidebar--open) .sidebar-logo {
  grid-template-columns: 40px;
  justify-content: center;
  justify-items: center;
  padding: 0;
}

.sidebar:not(.sidebar--open) .sidebar-nav {
  align-items: center;
  padding-left: 0;
  padding-right: 0;
}

.sidebar:not(.sidebar--open) .sidebar-nav a {
  width: 40px;
  height: 40px;
  max-width: 40px;
  max-height: 40px;
  min-height: 40px;
  flex: 0 0 40px;
  aspect-ratio: 1 / 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  overflow: hidden;
}

.sidebar:not(.sidebar--open) .sidebar-nav a span {
  display: none;
}

.sidebar-logo span,
.sidebar-nav a span,
.esp32-badge > span:last-child {
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.14s ease;
}

.sidebar--open .sidebar-logo span,
.sidebar--open .sidebar-nav a span,
.sidebar--open .esp32-badge > span:last-child {
  opacity: 1;
  pointer-events: auto;
}

.sidebar-toggle {
  width: 40px;
  height: 40px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid rgba(255,255,255,0.18);
  border-radius: 8px;
  background: transparent;
  color: #fff;
  transition: background 0.15s, border-color 0.15s;
}

.mobile-menu-toggle {
  display: none;
}

.sidebar-toggle:hover {
  background: rgba(255,255,255,0.18);
  border-color: rgba(255,255,255,0.32);
}

.panel-icon {
  display: none;
}

.sidebar-toggle:hover .brand-icon,
.sidebar--open .sidebar-toggle .brand-icon {
  display: none;
}

.sidebar-toggle:hover .panel-icon,
.sidebar--open .sidebar-toggle .panel-icon {
  display: block;
}

.sidebar-nav {
  flex: 1;
  padding: 8px 12px;
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.sidebar-nav a {
  display: grid;
  grid-template-columns: 40px 1fr;
  align-items: center;
  gap: 8px;
  min-height: 40px;
  padding: 1px 0;
  border-radius: 8px;
  color: rgba(255,255,255,0.8);
  text-decoration: none;
  font-size: 0.9rem;
  white-space: nowrap;
  transition: background 0.15s, color 0.15s;
}

.sidebar-nav a > svg {
  justify-self: center;
}

.sidebar-nav a:hover,
.sidebar-nav a.active {
  background: rgba(255,255,255,0.12);
  color: #fff;
}

.sidebar-status {
  padding: 12px 12px 18px;
  border-top: 1px solid rgba(255,255,255,0.1);
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.theme-switch {
  min-height: 40px;
  border: none;
  border-radius: 8px;
  background: transparent;
  color: rgba(255,255,255,0.9);
  display: grid;
  grid-template-columns: 40px 1fr;
  align-items: center;
  gap: 10px;
  padding: 1px 0;
  font-size: 0.82rem;
  white-space: nowrap;
}

.theme-switch:hover {
  background: rgba(255,255,255,0.14);
  color: #fff;
}

.sidebar:not(.sidebar--open) .theme-switch {
  width: 40px;
  min-height: 24px;
  grid-template-columns: auto;
  justify-content: center;
  padding: 0;
  border-radius: 999px;
  background: transparent;
}

.sidebar:not(.sidebar--open) .theme-switch:hover {
  background: transparent;
}

.switch-track {
  width: 36px;
  height: 22px;
  border-radius: 999px;
  background: rgba(255,255,255,0.18);
  border: 1px solid rgba(255,255,255,0.22);
  display: flex;
  align-items: center;
  padding: 2px;
  transition: background .18s ease, border-color .18s ease;
}

.switch-thumb {
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  color: var(--sidebar-bg);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  transform: translateX(0);
  transition: transform .18s ease;
}

.theme-switch[aria-checked="true"] .switch-track {
  background: var(--verde-acento);
  border-color: var(--verde-acento);
}

.theme-switch[aria-checked="true"] .switch-thumb {
  transform: translateX(14px);
}

.switch-label {
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.14s ease;
}

.sidebar--open .switch-label {
  opacity: 1;
  pointer-events: auto;
}

.esp32-badge {
  display: flex;
  align-items: center;
  gap: 10px;
  min-height: 32px;
  font-size: 0.82rem;
  color: rgba(255,255,255,0.9);
  white-space: nowrap;
  padding: 0 10px;
}

.sidebar:not(.sidebar--open) .esp32-badge {
  width: 40px;
  height: 40px;
  justify-content: center;
  align-self: center;
  padding: 0;
  border: 1px solid rgba(255,255,255,0.18);
  border-radius: 8px;
  background: rgba(255,255,255,0.06);
}

.sidebar:not(.sidebar--open) .esp32-badge--online {
  background:
    radial-gradient(circle at center, var(--verde-acento) 0 7px, rgba(166,255,77,0.22) 8px 11px, transparent 12px),
    rgba(255,255,255,0.06);
  box-shadow: inset 0 0 0 1px rgba(166,255,77,0.14), 0 0 12px rgba(166,255,77,0.32);
}

.sidebar:not(.sidebar--open) .esp32-badge--offline {
  background:
    radial-gradient(circle at center, #d4dde0 0 7px, rgba(255,255,255,0.16) 8px 11px, transparent 12px),
    rgba(255,255,255,0.06);
}

.sidebar:not(.sidebar--open) .esp32-badge::before {
  content: none;
}

.sidebar:not(.sidebar--open) .esp32-badge:hover {
  border-color: rgba(255,255,255,0.32);
}

.sidebar:not(.sidebar--open) .esp32-badge--online:hover {
  background:
    radial-gradient(circle at center, var(--verde-acento) 0 7px, rgba(166,255,77,0.28) 8px 11px, transparent 12px),
    rgba(255,255,255,0.14);
}

.sidebar:not(.sidebar--open) .esp32-badge--offline:hover {
  background:
    radial-gradient(circle at center, #d4dde0 0 7px, rgba(255,255,255,0.22) 8px 11px, transparent 12px),
    rgba(255,255,255,0.14);
}

.sidebar:not(.sidebar--open) .esp32-badge .dot-wrap {
  display: none;
}

.dot-wrap {
  position: relative;
  width: 10px;
  height: 10px;
  flex-shrink: 0;
}

.dot {
  display: block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

.sidebar:not(.sidebar--open) .dot-wrap,
.sidebar:not(.sidebar--open) .dot {
  width: 16px;
  height: 16px;
}

.dot--green {
  background: var(--verde-acento);
  box-shadow: 0 0 0 3px rgba(166,255,77,0.18), 0 0 12px rgba(166,255,77,0.65);
}

.dot--gray {
  background: #d4dde0;
  box-shadow: 0 0 0 3px rgba(255,255,255,0.12);
}

.main-content {
  flex: 1;
  min-width: 0;
  overflow: auto;
  background: var(--fondo-pagina);
  display: flex;
  flex-direction: column;
}

.sidebar-overlay {
  display: none;
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.4);
  z-index: 290;
}

@media (max-width: 768px) {
  .app-shell {
    display: block;
  }

  .mobile-menu-toggle {
    position: fixed;
    top: 14px;
    left: 14px;
    z-index: 280;
    width: 42px;
    height: 38px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: none;
    border-radius: 8px;
    background: var(--verde-primary);
    color: #fff;
    box-shadow: 0 2px 8px rgba(0,0,0,0.18);
  }

  .mobile-menu-toggle--hidden {
    display: none;
  }

  .sidebar {
    position: fixed;
    left: 0;
    top: 0;
    width: 240px;
    transform: translateX(-100%);
    transition: transform 0.22s ease;
    z-index: 300;
  }

  .sidebar--open {
    width: 240px;
    transform: translateX(0);
  }

  .sidebar-logo span,
  .sidebar-nav a span,
  .esp32-badge > span:last-child {
    opacity: 1;
    pointer-events: auto;
  }

  .sidebar-overlay {
    display: block;
  }

  .main-content {
    height: 100vh;
    padding-top: 52px;
  }
}
</style>
