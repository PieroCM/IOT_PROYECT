import { createRouter, createWebHistory } from 'vue-router'
import LoteActivo from '../views/LoteActivo.vue'
import Historial from '../views/Historial.vue'

export default createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/',          component: LoteActivo },
    { path: '/historial', component: Historial },
  ],
})
