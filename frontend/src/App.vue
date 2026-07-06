<template>
  <div class="app-shell">
    <!-- Mobile hamburger -->
    <button class="hamburger" @click="sidebarOpen = !sidebarOpen" aria-label="Menú">
      <span></span><span></span><span></span>
    </button>

    <!-- Sidebar overlay (mobile) -->
    <div v-if="sidebarOpen" class="sidebar-overlay" @click="sidebarOpen = false" />

    <!-- Sidebar -->
    <aside class="sidebar" :class="{ 'sidebar--open': sidebarOpen }">
      <div class="sidebar-logo">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M12 2C8 2 4 6 4 10c0 5 8 12 8 12s8-7 8-12c0-4-4-8-8-8z"/>
        </svg>
        <span>PaltaCheck</span>
      </div>

      <nav class="sidebar-nav">
        <RouterLink to="/" @click="sidebarOpen = false" :class="{ active: $route.path === '/' }">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
            <polyline points="9 22 9 12 15 12 15 22"/>
          </svg>
          Lote Activo
        </RouterLink>
        <RouterLink to="/historial" @click="sidebarOpen = false" :class="{ active: $route.path === '/historial' }">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="8" y1="6" x2="21" y2="6"/><line x1="8" y1="12" x2="21" y2="12"/>
            <line x1="8" y1="18" x2="21" y2="18"/><line x1="3" y1="6" x2="3.01" y2="6"/>
            <line x1="3" y1="12" x2="3.01" y2="12"/><line x1="3" y1="18" x2="3.01" y2="18"/>
          </svg>
          Historial
        </RouterLink>
      </nav>

      <div class="sidebar-status">
        <div class="esp32-badge">
          <span class="dot-wrap">
            <span class="dot" :class="store.esp32Online ? 'dot--green' : 'dot--gray'" />
            <span v-if="store.esp32Online" class="dot ping-ring" style="position:absolute;top:0;left:0;" />
          </span>
          <span>ESP32 {{ store.esp32Online ? 'Conectado' : 'Desconectado' }}</span>
        </div>
      </div>
    </aside>

    <!-- Main content -->
    <main class="main-content">
      <RouterView />
    </main>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { usePaltaStore } from './stores/palta'

const store = usePaltaStore()
const sidebarOpen = ref(false)

let healthInterval = null

onMounted(async () => {
  store.fetchHealth()
  // Restaura el lote que siga abierto en el backend: así un F5/refresh NO pierde
  // el lote, se mantiene. (El lote solo se cierra al pulsar "Cerrar Lote" o al
  // abrir otro nuevo, que cierra los remanentes en el backend.)
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
}

/* ── Sidebar ── */
.sidebar {
  width: 240px;
  flex-shrink: 0;
  background: var(--verde-primary);
  color: #fff;
  display: flex;
  flex-direction: column;
  height: 100%;
  z-index: 200;
  transition: transform 0.25s ease;
}

.sidebar-logo {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 24px 20px;
  font-size: 1.2rem;
  font-weight: 700;
  border-bottom: 1px solid rgba(255,255,255,0.1);
}

.sidebar-nav {
  flex: 1;
  padding: 16px 12px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.sidebar-nav a {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  border-radius: 8px;
  color: rgba(255,255,255,0.8);
  text-decoration: none;
  font-size: 0.9rem;
  transition: background 0.15s, color 0.15s;
}

.sidebar-nav a:hover,
.sidebar-nav a.active {
  background: rgba(255,255,255,0.12);
  color: #fff;
}

.sidebar-status {
  padding: 20px;
  border-top: 1px solid rgba(255,255,255,0.1);
}

.esp32-badge {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 0.82rem;
  color: rgba(255,255,255,0.9);
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

.dot--green  { background: var(--verde-acento); }
.dot--gray   { background: var(--texto-secondary); }

/* ── Main ── */
.main-content {
  flex: 1;
  overflow: auto;
  background: var(--fondo-pagina);
  display: flex;
  flex-direction: column;
}

/* ── Hamburger (mobile only) ── */
.hamburger {
  display: none;
  position: fixed;
  top: 14px;
  left: 14px;
  z-index: 300;
  background: var(--verde-primary);
  border: none;
  border-radius: 6px;
  padding: 8px 10px;
  flex-direction: column;
  gap: 5px;
}

.hamburger span {
  display: block;
  width: 20px;
  height: 2px;
  background: #fff;
  border-radius: 2px;
}

.sidebar-overlay {
  display: none;
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.4);
  z-index: 150;
}

/* ── Responsive ── */
@media (max-width: 768px) {
  .hamburger { display: flex; }
  .sidebar-overlay { display: block; }

  .sidebar {
    position: fixed;
    top: 0;
    left: 0;
    height: 100%;
    transform: translateX(-100%);
  }

  .sidebar--open {
    transform: translateX(0);
  }

  .main-content {
    padding-top: 52px;
  }
}
</style>
