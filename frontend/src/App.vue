<template>
  <div class="app-shell">
    <!-- Hamburguesa (solo móvil / táctil) -->
    <button class="hamburger" @click="sidebarOpen = !sidebarOpen" aria-label="Menú">
      <span></span><span></span><span></span>
    </button>
    <div v-if="sidebarOpen" class="sidebar-overlay" @click="sidebarOpen = false" />

    <!-- Zona de hover: acerca el mouse al borde izquierdo y el menú se abre solo -->
    <div class="edge-hover" aria-hidden="true"><span class="edge-hint">☰</span></div>

    <!-- Sidebar (oculto; se muestra al hacer hover en el borde o con la hamburguesa) -->
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

    <!-- Contenido principal (ocupa todo el ancho; el menú flota encima) -->
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
  position: relative;
}

/* ── Zona de hover (borde izquierdo) ── */
.edge-hover {
  position: fixed;
  left: 0; top: 0;
  width: 16px; height: 100%;
  z-index: 250;
}
.edge-hover::before {   /* franjita visible: pista de que hay un menú */
  content: '';
  position: absolute; left: 0; top: 0;
  width: 5px; height: 100%;
  background: linear-gradient(180deg, var(--verde-acento), var(--verde-primary));
}
.edge-hint {
  position: absolute; top: 50%; left: 5px; transform: translateY(-50%);
  color: #fff; background: var(--verde-primary);
  border-radius: 0 8px 8px 0; padding: 10px 7px;
  font-size: 1rem; line-height: 1;
  box-shadow: 1px 1px 5px rgba(0,0,0,.25);
  transition: opacity .2s;
}

/* ── Sidebar (oculto por defecto; slide-in al hover o hamburguesa) ── */
.sidebar {
  position: fixed;
  left: 0; top: 0;
  height: 100%;
  width: 240px;
  background: var(--verde-primary);
  color: #fff;
  display: flex;
  flex-direction: column;
  z-index: 300;
  transform: translateX(-100%);
  transition: transform 0.25s ease;
  box-shadow: 2px 0 18px rgba(0,0,0,0.2);
}
/* Se abre al: acercar el mouse al borde, pasar sobre el propio menú, o hamburguesa */
.edge-hover:hover ~ .sidebar,
.sidebar:hover,
.sidebar--open {
  transform: translateX(0);
}
/* Oculta la pista mientras el menú está abierto por hover */
.edge-hover:hover .edge-hint { opacity: 0; }

.sidebar-logo {
  display: flex; align-items: center; gap: 10px;
  padding: 24px 20px; font-size: 1.2rem; font-weight: 700;
  border-bottom: 1px solid rgba(255,255,255,0.1);
}
.sidebar-nav { flex: 1; padding: 16px 12px; display: flex; flex-direction: column; gap: 4px; }
.sidebar-nav a {
  display: flex; align-items: center; gap: 12px;
  padding: 10px 14px; border-radius: 8px;
  color: rgba(255,255,255,0.8); text-decoration: none; font-size: 0.9rem;
  transition: background 0.15s, color 0.15s;
}
.sidebar-nav a:hover, .sidebar-nav a.active { background: rgba(255,255,255,0.12); color: #fff; }
.sidebar-status { padding: 20px; border-top: 1px solid rgba(255,255,255,0.1); }
.esp32-badge { display: flex; align-items: center; gap: 10px; font-size: 0.82rem; color: rgba(255,255,255,0.9); }
.dot-wrap { position: relative; width: 10px; height: 10px; flex-shrink: 0; }
.dot { display: block; width: 10px; height: 10px; border-radius: 50%; }
.dot--green { background: var(--verde-acento); }
.dot--gray  { background: var(--texto-secondary); }

/* ── Main ── */
.main-content {
  flex: 1;
  width: 100%;
  overflow: auto;
  background: var(--fondo-pagina);
  display: flex;
  flex-direction: column;
}

/* ── Hamburguesa (móvil) ── */
.hamburger {
  display: none;
  position: fixed; top: 14px; left: 14px; z-index: 320;
  background: var(--verde-primary); border: none; border-radius: 6px;
  padding: 8px 10px; flex-direction: column; gap: 5px;
}
.hamburger span { display: block; width: 20px; height: 2px; background: #fff; border-radius: 2px; }
.sidebar-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.4); z-index: 290; }

/* ── Responsive: en móvil no hay hover -> hamburguesa ── */
@media (max-width: 768px) {
  .edge-hover { display: none; }
  .hamburger { display: flex; }
  .sidebar-overlay { display: block; }
  .main-content { padding-top: 52px; }
}
</style>
