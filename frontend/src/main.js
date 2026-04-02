import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { Chart, registerables } from 'chart.js'
import axios from 'axios'
import router from './router'
import App from './App.vue'
import './style.css'

Chart.register(...registerables)

axios.defaults.baseURL = import.meta.env.VITE_API_URL || 'http://localhost:8000'

const app = createApp(App)
app.use(createPinia())
app.use(router)
app.mount('#app')
