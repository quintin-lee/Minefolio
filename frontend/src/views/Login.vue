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

        <div v-if="isTwoFactorStep" class="two-factor-box">
          <p class="two-factor-hint">该账号已启用两步验证 (TOTP)，请输入身份验证器中的 6 位动态验证码或应急备用码：</p>
          <el-form label-position="top" class="login-form" @submit.prevent="handle2FaSubmit">
            <el-form-item label="动态验证码 / 备用码">
              <el-input
                v-model="twoFactorCode"
                placeholder="例如 123456 或 a1b2-c3d4"
                prefix-icon="Key"
                size="large"
                autofocus
                @keyup.enter="handle2FaSubmit"
              />
            </el-form-item>
            <el-form-item class="submit-item">
              <el-button type="primary" size="large" :loading="loading" class="submit-btn" @click="handle2FaSubmit">
                验证并登录
              </el-button>
            </el-form-item>
            <div class="switch-mode">
              <el-button link type="info" class="switch-btn" @click="isTwoFactorStep = false">
                返回账号密码登录
              </el-button>
            </div>
          </el-form>
        </div>

        <el-form v-else ref="formRef" :model="form" :rules="rules" label-position="top" class="login-form">
          <el-form-item :label="isRegister ? '用户名' : '用户名'" prop="username">
            <el-input v-model="form.username" placeholder="请输入用户名" prefix-icon="User" size="large" />
          </el-form-item>

          <el-form-item label="密码" prop="password">
            <el-input
              v-model="form.password"
              type="password"
              :placeholder="isRegister ? '请设置密码 (至少6位)' : '请输入密码'"
              prefix-icon="Lock"
              show-password
              size="large"
              @keyup.enter="handleSubmit"
            />
          </el-form-item>

          <el-form-item v-if="isRegister" label="确认密码" prop="confirmPassword">
            <el-input
              v-model="form.confirmPassword"
              type="password"
              placeholder="请再次输入密码"
              prefix-icon="Lock"
              show-password
              size="large"
              @keyup.enter="handleSubmit"
            />
          </el-form-item>

          <el-form-item class="submit-item">
            <el-button type="primary" size="large" :loading="loading" class="submit-btn" @click="handleSubmit">
              {{ isRegister ? '创建账号并进入系统' : '登录系统' }}
            </el-button>
          </el-form-item>

          <div class="switch-mode">
            <span>{{ isRegister ? '已有账号？' : '还没有账号？' }}</span>
            <el-button link type="primary" class="switch-btn" @click="toggleMode">
              {{ isRegister ? '返回登录' : '立即注册' }}
            </el-button>
          </div>

          <div v-if="oauthProviders.length > 0" class="oauth-container">
            <div class="oauth-divider">
              <span>或通过第三方账号登录</span>
            </div>
            <div class="oauth-btns">
              <button
                v-for="p in oauthProviders"
                :key="p.id"
                type="button"
                class="oauth-btn"
                @click="handleOAuthLogin(p)"
              >
                <el-icon class="oauth-icon"><Key /></el-icon>
                <span>{{ p.name }}</span>
              </button>
            </div>
          </div>
        </el-form>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { Key } from '@element-plus/icons-vue'
import { useAuthStore } from '@/stores/auth'
import { authApi } from '@/api/auth'
import type { OAuthProvider } from '@/types'
import { t } from '@/utils/locale'

const router = useRouter()
const auth = useAuthStore()
const formRef = ref()
const loading = ref(false)
const isRegister = ref(false)
const oauthProviders = ref<OAuthProvider[]>([])

const form = reactive({ username: '', password: '', confirmPassword: '' })
const isTwoFactorStep = ref(false)
const tempToken = ref('')
const twoFactorCode = ref('')

function handleOAuthLogin(p: OAuthProvider) {
  if (p.auth_url) {
    window.location.href = p.auth_url
  }
}

const validateConfirmPassword = (_rule: any, value: string, callback: any) => {
  if (isRegister.value && value !== form.password) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const rules = {
  username: [
    { required: true, message: t('login.usernameRequired') || '请输入用户名', trigger: 'blur' },
    { min: 2, message: t('login.usernameMin') || '用户名至少2个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: t('login.passwordRequired') || '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少6个字符', trigger: 'blur' },
  ],
  confirmPassword: [
    {
      validator: (_rule: any, value: string, callback: any) => {
        if (isRegister.value && !value) {
          callback(new Error('请再次输入密码'))
        } else if (isRegister.value && value !== form.password) {
          callback(new Error('两次输入的密码不一致'))
        } else {
          callback()
        }
      },
      trigger: 'blur'
    }
  ]
}

function toggleMode() {
  isRegister.value = !isRegister.value
  isTwoFactorStep.value = false
  form.password = ''
  form.confirmPassword = ''
  formRef.value?.clearValidate()
  statusText.value = isRegister.value ? 'REGISTRATION_MODE' : 'AUTHENTICATION'
}

async function handleSubmit() {
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    loading.value = true
    try {
      if (isRegister.value) {
        await auth.register(form.username, form.password)
        ElMessage.success('注册成功，已自动登录')
        await router.push('/dashboard')
      } else {
        const res = await auth.login(form.username, form.password)
        if (res?.require_2fa) {
          isTwoFactorStep.value = true
          tempToken.value = res.temp_token || ''
          twoFactorCode.value = ''
          statusText.value = '2FA_CHALLENGE'
          ElMessage.info('请输入两步验证码完成登录')
          return
        }
        ElMessage.success('登录成功')
        await router.push('/dashboard')
      }
    } catch (e: any) {
      ElMessage.error(e?.response?.data?.message || (isRegister.value ? '注册失败' : '登录失败'))
    } finally {
      loading.value = false
    }
  })
}

async function handle2FaSubmit() {
  if (!twoFactorCode.value.trim()) {
    ElMessage.warning('请输入两步验证动态码或备用码')
    return
  }
  loading.value = true
  try {
    await auth.verify2FaLogin(tempToken.value, twoFactorCode.value.trim())
    ElMessage.success('两步验证通过，登录成功')
    await router.push('/dashboard')
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '两步验证码错误')
  } finally {
    loading.value = false
  }
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

onMounted(async () => {
  // 状态指示灯: 初始化 → 在线
  statusTimer = window.setTimeout(() => {
    statusText.value = 'SYSTEM ONLINE'
  }, 900)

  try {
    const res = await authApi.getOAuthProviders()
    if (res && res.providers) {
      oauthProviders.value = res.providers
    }
  } catch {}

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
  --login-bg-start: var(--mf-background);
  --login-bg-mid: color-mix(in srgb, var(--mf-background) 70%, var(--mf-primary));
  --login-bg-end: color-mix(in srgb, var(--mf-background) 60%, var(--mf-accent));
  --login-overlay-start: var(--mf-primary-light);
  --login-overlay-end: transparent;
  --login-card-bg: var(--mf-surface-card);
  --login-card-border: var(--mf-border);
  --login-card-shadow: var(--mf-shadow-lg);
  --login-text: var(--mf-text-regular);
  --login-muted: var(--mf-text-muted);
  --login-input-bg: var(--mf-surface-blank);
  --login-input-border: var(--mf-border);
  --login-input-text: var(--mf-text-main);
  --login-divider: var(--mf-border-subtle);
  --login-btn-bg: rgba(30, 41, 59, 0.6);
  --login-btn-border: var(--mf-border);
  --login-btn-text: var(--mf-text-regular);
  --login-btn-hover-bg: var(--mf-surface-hover);
  --login-btn-hover-border: var(--mf-primary);
  --login-glow: var(--mf-primary);

  background: linear-gradient(135deg, var(--login-bg-start) 0%, var(--login-bg-mid) 50%, var(--login-bg-end) 100%);
  background-size: 400% 400%;
  animation: mf-gradient-shift 15s ease infinite;
  position: relative;
  overflow: hidden;
}
.login-overlay {
  position: absolute;
  top: 0; left: 0; width: 100%; height: 100%;
  z-index: 1;
  background: radial-gradient(circle at top right, var(--login-overlay-start), var(--login-overlay-end) 40%),
              radial-gradient(circle at bottom left, var(--login-overlay-start), var(--login-overlay-end) 40%);
  pointer-events: none;
}
.glass-panel {
  background: var(--login-card-bg) !important;
  border: 1px solid var(--login-card-border) !important;
  box-shadow: var(--login-card-shadow) !important;
}
:deep(.el-card__header) {
  border-bottom: 1px solid var(--login-card-border);
  padding-bottom: 20px;
}
.app-title {
  margin: 0;
  font-size: 32px;
  font-weight: 700;
  background: linear-gradient(to right, var(--login-glow), var(--mf-accent));
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  letter-spacing: 2px;
  animation: titleGlow 3.2s ease-in-out infinite;
}
.subtitle,
.status-line,
.two-factor-hint {
  color: var(--login-muted);
}
.switch-mode {
  color: var(--login-muted);
}
.switch-btn {
  color: var(--login-glow);
}
.switch-btn:hover {
  color: var(--mf-primary-hover);
}
:deep(.el-form-item__label) {
  color: var(--login-text);
  font-weight: 500;
}
:deep(.el-input__wrapper) {
  background-color: var(--login-input-bg) !important;
  box-shadow: 0 0 0 1px var(--login-input-border) inset !important;
}
:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--login-glow) inset, 0 0 10px var(--mf-primary-light) !important;
}
:deep(.el-input__inner) {
  color: var(--login-input-text);
}
.oauth-divider::before,
.oauth-divider::after {
  border-bottom: 1px solid var(--login-divider);
}
.oauth-divider span {
  color: var(--login-muted);
}
.oauth-btn {
  background: var(--login-btn-bg);
  border: 1px solid var(--login-btn-border);
  color: var(--login-btn-text);
}
.oauth-btn:hover {
  background: var(--login-btn-hover-bg);
  border-color: var(--login-btn-hover-border);
  color: var(--mf-text-main);
}
.hud-corner {
  position: absolute;
  width: 26px;
  height: 26px;
  pointer-events: none;
  filter: drop-shadow(0 0 6px var(--mf-primary-light));
}
.hud-corner-tl,
.hud-corner-tr,
.hud-corner-bl,
.hud-corner-br {
  border-top-color: var(--login-glow);
  border-left-color: var(--login-glow);
  border-right-color: var(--login-glow);
  border-bottom-color: var(--login-glow);
}
.hud-frame::after {
  background: linear-gradient(180deg, transparent 0%, var(--mf-primary-light) 48%, transparent 100%);
}
.status-dot {
  background: var(--mf-success);
  box-shadow: 0 0 6px var(--mf-success);
}
.status-cursor {
  background: var(--login-glow);
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
  box-shadow: var(--login-card-shadow) !important;
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
  color: var(--login-input-text);
  font-weight: 500;
}
:deep(.el-input__wrapper) {
  background-color: var(--login-input-bg) !important;
  box-shadow: 0 0 0 1px var(--login-input-border) inset !important;
}
:deep(.el-input__wrapper.is-focus) {
  box-shadow: 0 0 0 1px var(--login-glow) inset, 0 0 10px var(--mf-primary-light) !important;
}
:deep(.el-input__inner) {
  color: var(--login-input-text);
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
  color: var(--mf-text-muted);
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

.oauth-container {
  margin-top: 24px;
}
.oauth-divider {
  display: flex;
  align-items: center;
  text-align: center;
  margin-bottom: 16px;
}
.oauth-divider::before,
.oauth-divider::after {
  content: '';
  flex: 1;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}
.oauth-divider span {
  padding: 0 10px;
  color: #64748b;
  font-size: 12px;
}
.oauth-btns {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.oauth-btn {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  width: 100%;
  height: 40px;
  background: rgba(30, 41, 59, 0.6);
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 8px;
  color: var(--login-input-text);
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
}
.oauth-btn:hover {
  background: rgba(51, 65, 85, 0.8);
  border-color: #00d4ff;
  color: #fff;
  transform: translateY(-1px);
}
.oauth-icon {
  font-size: 16px;
}

.two-factor-hint {
  font-size: 13px;
  line-height: 1.6;
  color: var(--mf-text-muted);
  margin-bottom: 16px;
  text-align: center;
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
