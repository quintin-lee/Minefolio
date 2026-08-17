<template>
  <div class="login-container">
    <canvas ref="canvasRef" class="particle-canvas"></canvas>
    <div class="login-overlay"></div>

    <div class="hud-frame">
      <span class="hud-corner hud-corner-tl"></span>
      <span class="hud-corner hud-corner-tr"></span>
      <span class="hud-corner hud-corner-bl"></span>
      <span class="hud-corner hud-corner-br"></span>

      <el-card class="login-card glass-panel fade-in">
        <template #header>
          <div class="card-header">
            <h2 class="app-title">Minefolio</h2>
            <p class="subtitle">综合资产管理</p>
            <div class="status-line">
              <span class="status-dot"></span>
              <span class="status-text">{{ statusText }}</span>
              <span class="status-cursor"></span>
            </div>
          </div>
        </template>

        <el-form ref="formRef" :model="form" :rules="rules" label-position="top" class="login-form">
          <el-form-item label="用户名" prop="username">
            <el-input v-model="form.username" placeholder="请输入用户名" prefix-icon="User" size="large" />
          </el-form-item>

          <el-form-item label="密码" prop="password">
            <el-input v-model="form.password" type="password" placeholder="请输入密码"
              prefix-icon="Lock" show-password size="large" @keyup.enter="handleSubmit" />
          </el-form-item>

          <el-form-item class="submit-item">
            <el-button type="primary" size="large" :loading="loading" class="submit-btn" @click="handleSubmit">
              登录系统
            </el-button>
          </el-form-item>
        </el-form>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { zhCN } from '@/locales/zh-CN'

const t = (key: string) => {
  const keys = key.split('.')
  let obj: any = zhCN
  for (const k of keys) obj = obj?.[k]
  return obj || key
}

const router = useRouter()
const auth = useAuthStore()
const formRef = ref()
const loading = ref(false)

const form = reactive({ username: '', password: '' })
const rules = {
  username: [
    { required: true, message: t('login.usernameRequired'), trigger: 'blur' },
    { min: 2, message: t('login.usernameMin'), trigger: 'blur' },
  ],
  password: [
    { required: true, message: t('login.passwordRequired'), trigger: 'blur' },
    { min: 4, message: t('login.passwordMin'), trigger: 'blur' },
  ],
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    loading.value = true
    try {
      await auth.login(form.username, form.password)
      ElMessage.success('登录成功')
      router.push('/dashboard')
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || '登录失败')
    } finally {
      loading.value = false
    }
  })
}

/* ---------- 科技感装饰: 状态指示灯 + 粒子网络 ---------- */

const statusText = ref('INITIALIZING')
let statusTimer = 0

const canvasRef = ref<HTMLCanvasElement | null>(null)

interface Particle {
  x: number
  y: number
  vx: number
  vy: number
  r: number
  purple: boolean
}

const LINK_DIST = 130
const MAX_PARTICLES = 110
const CYAN = '0, 212, 255'
const PURPLE = '124, 58, 237'

let ctx: CanvasRenderingContext2D | null = null
let particles: Particle[] = []
let rafId = 0
let resizeTimer = 0
let dpr = 1
const mouse = { x: -9999, y: -9999 }

function seedParticles(canvas: HTMLCanvasElement) {
  const count = Math.min(MAX_PARTICLES, Math.floor((canvas.width * canvas.height) / (16000 * dpr * dpr)))
  particles = Array.from({ length: count }, () => ({
    x: Math.random() * canvas.width,
    y: Math.random() * canvas.height,
    vx: (Math.random() - 0.5) * 0.5 * dpr,
    vy: (Math.random() - 0.5) * 0.5 * dpr,
    r: (Math.random() * 1.5 + 0.8) * dpr,
    purple: Math.random() < 0.14,
  }))
}

function initCanvas() {
  const canvas = canvasRef.value
  if (!canvas) return
  dpr = Math.min(window.devicePixelRatio || 1, 2)
  canvas.width = window.innerWidth * dpr
  canvas.height = window.innerHeight * dpr
  ctx = canvas.getContext('2d')
  seedParticles(canvas)
}

function drawFrame() {
  const canvas = canvasRef.value
  if (!ctx || !canvas) return
  ctx.clearRect(0, 0, canvas.width, canvas.height)
  const dist = LINK_DIST * dpr

  for (const p of particles) {
    p.x += p.vx
    p.y += p.vy
    if (p.x < 0 || p.x > canvas.width) p.vx *= -1
    if (p.y < 0 || p.y > canvas.height) p.vy *= -1
  }

  // 连接线 (节点间 + 鼠标附近)
  ctx.lineWidth = dpr
  for (let i = 0; i < particles.length; i++) {
    const a = particles[i]!
    for (let j = i + 1; j < particles.length; j++) {
      const b = particles[j]!
      const dx = a.x - b.x
      const dy = a.y - b.y
      const d2 = dx * dx + dy * dy
      if (d2 < dist * dist) {
        const alpha = (1 - Math.sqrt(d2) / dist) * 0.32
        ctx.strokeStyle = `rgba(${CYAN}, ${alpha})`
        ctx.beginPath()
        ctx.moveTo(a.x, a.y)
        ctx.lineTo(b.x, b.y)
        ctx.stroke()
      }
    }
    const mdx = a.x - mouse.x * dpr
    const mdy = a.y - mouse.y * dpr
    const md2 = mdx * mdx + mdy * mdy
    if (md2 < dist * dist) {
      const alpha = (1 - Math.sqrt(md2) / dist) * 0.4
      ctx.strokeStyle = `rgba(${PURPLE}, ${alpha})`
      ctx.beginPath()
      ctx.moveTo(a.x, a.y)
      ctx.lineTo(mouse.x * dpr, mouse.y * dpr)
      ctx.stroke()
    }
  }

  // 节点
  for (const p of particles) {
    ctx.beginPath()
    ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2)
    ctx.fillStyle = p.purple ? `rgba(${PURPLE}, 0.7)` : `rgba(${CYAN}, 0.75)`
    ctx.fill()
  }

  rafId = requestAnimationFrame(drawFrame)
}

function startAnimation() {
  if (rafId) return
  rafId = requestAnimationFrame(drawFrame)
}

function stopAnimation() {
  cancelAnimationFrame(rafId)
  rafId = 0
}

function onVisibilityChange() {
  if (document.hidden) stopAnimation()
  else startAnimation()
}

function onResize() {
  window.clearTimeout(resizeTimer)
  resizeTimer = window.setTimeout(() => {
    initCanvas()
  }, 200)
}

function onPointerMove(e: PointerEvent) {
  mouse.x = e.clientX
  mouse.y = e.clientY
}

onMounted(() => {
  // 状态指示灯: 初始化 → 在线
  statusTimer = window.setTimeout(() => {
    statusText.value = 'SYSTEM ONLINE'
  }, 900)

  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches
  if (reducedMotion) return

  initCanvas()
  startAnimation()
  window.addEventListener('resize', onResize)
  window.addEventListener('pointermove', onPointerMove)
  document.addEventListener('visibilitychange', onVisibilityChange)
})

onBeforeUnmount(() => {
  window.clearTimeout(statusTimer)
  window.clearTimeout(resizeTimer)
  stopAnimation()
  window.removeEventListener('resize', onResize)
  window.removeEventListener('pointermove', onPointerMove)
  document.removeEventListener('visibilitychange', onVisibilityChange)
})
</script>

<style scoped>
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #060b18 0%, #0a1628 50%, #0d1f3c 100%);
  background-size: 400% 400%;
  animation: mf-gradient-shift 15s ease infinite;
  position: relative;
  overflow: hidden;
}
.particle-canvas {
  position: fixed;
  inset: 0;
  z-index: 0;
  pointer-events: none;
}
.login-overlay {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  z-index: 1;
  background: radial-gradient(circle at top right, rgba(0, 212, 255, 0.1), transparent 40%),
              radial-gradient(circle at bottom left, rgba(124, 58, 237, 0.1), transparent 40%);
  pointer-events: none;
}

/* HUD 四角框 */
.hud-frame {
  position: relative;
  z-index: 2;
}
.hud-corner {
  position: absolute;
  width: 26px;
  height: 26px;
  pointer-events: none;
  filter: drop-shadow(0 0 6px rgba(0, 212, 255, 0.55));
}
.hud-corner-tl {
  top: -3px;
  left: -3px;
  border-top: 2px solid rgba(0, 212, 255, 0.9);
  border-left: 2px solid rgba(0, 212, 255, 0.9);
  border-top-left-radius: 10px;
}
.hud-corner-tr {
  top: -3px;
  right: -3px;
  border-top: 2px solid rgba(0, 212, 255, 0.9);
  border-right: 2px solid rgba(0, 212, 255, 0.9);
  border-top-right-radius: 10px;
}
.hud-corner-bl {
  bottom: -3px;
  left: -3px;
  border-bottom: 2px solid rgba(0, 212, 255, 0.9);
  border-left: 2px solid rgba(0, 212, 255, 0.9);
  border-bottom-left-radius: 10px;
}
.hud-corner-br {
  bottom: -3px;
  right: -3px;
  border-bottom: 2px solid rgba(0, 212, 255, 0.9);
  border-right: 2px solid rgba(0, 212, 255, 0.9);
  border-bottom-right-radius: 10px;
}
.hud-corner {
  animation: hudPulse 3s ease-in-out infinite;
}
.hud-corner-tr,
.hud-corner-bl {
  animation-delay: 1.5s;
}
@keyframes hudPulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.45; }
}

/* 扫描线扫过卡片 */
.hud-frame::after {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  border-radius: 12px;
  pointer-events: none;
  background: linear-gradient(180deg, transparent 0%, rgba(0, 212, 255, 0.07) 48%, transparent 100%);
  background-size: 100% 220px;
  background-repeat: no-repeat;
  background-position: 0 -220px;
  animation: scanSweep 7s ease-in-out infinite;
}
@keyframes scanSweep {
  0% { background-position: 0 -220px; }
  60%, 100% { background-position: 0 110%; }
}

.login-card {
  width: 420px;
  padding: 10px;
}
.glass-panel {
  background: rgba(255, 255, 255, 0.05) !important;
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border: 1px solid rgba(255, 255, 255, 0.1) !important;
  box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5) !important;
}
:deep(.el-card__header) {
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  padding-bottom: 20px;
}
.card-header {
  text-align: center;
}
.app-title {
  margin: 0;
  font-size: 32px;
  font-weight: 700;
  background: linear-gradient(to right, #00d4ff, #a78bfa);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  letter-spacing: 2px;
  animation: titleGlow 3.2s ease-in-out infinite;
}
@keyframes titleGlow {
  0%, 100% { filter: drop-shadow(0 0 5px rgba(0, 212, 255, 0.25)); }
  50% { filter: drop-shadow(0 0 16px rgba(0, 212, 255, 0.55)); }
}
.subtitle {
  margin: 8px 0 0;
  color: #64748b;
  font-size: 15px;
  letter-spacing: 3px;
}
.status-line {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  margin-top: 12px;
  font-family: 'JetBrains Mono', ui-monospace, monospace;
  font-size: 11px;
  letter-spacing: 2px;
  color: #64748b;
}
.status-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #10b981;
  box-shadow: 0 0 6px rgba(16, 185, 129, 0.9);
  animation: dotPulse 2s ease-in-out infinite;
}
@keyframes dotPulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.35; }
}
.status-cursor {
  width: 7px;
  height: 12px;
  background: rgba(0, 212, 255, 0.85);
  animation: cursorBlink 1.1s steps(1) infinite;
}
@keyframes cursorBlink {
  50% { opacity: 0; }
}
.login-form {
  margin-top: 20px;
}
:deep(.el-form-item__label) {
  color: #e2e8f0;
  font-weight: 500;
}
:deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.4) !important;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.1) inset !important;
}
:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px #00d4ff inset, 0 0 10px rgba(0, 212, 255, 0.25) !important;
}
:deep(.el-input__inner) {
  color: #e2e8f0;
}
.submit-item {
  margin-top: 30px;
  margin-bottom: 0;
}
.submit-btn {
  position: relative;
  width: 100%;
  height: 44px;
  font-size: 16px;
  border-radius: 8px;
  letter-spacing: 1px;
  overflow: hidden;
}
.submit-btn::after {
  content: '';
  position: absolute;
  top: 0;
  left: -120%;
  width: 55%;
  height: 100%;
  background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.35), transparent);
  transform: skewX(-20deg);
  transition: left 0.6s ease;
  pointer-events: none;
}
.submit-btn:hover::after {
  left: 130%;
}
.switch-mode {
  text-align: center;
  margin-top: 24px;
  color: #94a3b8;
  font-size: 14px;
}
.switch-btn {
  font-size: 14px;
  font-weight: 600;
  color: #00d4ff;
}
.switch-btn:hover {
  color: #22d3ee;
}

/* 减少动态效果偏好 */
@media (prefers-reduced-motion: reduce) {
  .particle-canvas { display: none; }
  .hud-corner,
  .app-title,
  .status-dot,
  .status-cursor,
  .hud-frame::after { animation: none; }
}
</style>
