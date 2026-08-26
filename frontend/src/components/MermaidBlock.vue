<template>
  <div class="mermaid-block-wrapper" :class="{ 'has-error': !!renderError, 'is-loading': loading }">
    <!-- Header Toolbar -->
    <div class="mermaid-header">
      <div class="header-left">
        <div class="type-badge">
          <Icon :icon="diagramInfo.icon" class="type-icon" />
          <span>{{ diagramInfo.label }}</span>
        </div>
        <span v-if="isStreaming" class="streaming-pill">
          <span class="streaming-dot"></span>
          生成中
        </span>
      </div>

      <div class="header-actions">
        <!-- Switch View (Chart <-> Code) -->
        <button
          class="tool-btn"
          :class="{ active: viewMode === 'code' }"
          :title="viewMode === 'code' ? '切换为图表视图' : '查看 Mermaid 源码'"
          @click="toggleViewMode"
        >
          <Icon :icon="viewMode === 'code' ? 'ph:chart-pie-slice' : 'ph:code'" />
          <span class="btn-text">{{ viewMode === 'code' ? '图表' : '源码' }}</span>
        </button>

        <!-- Line Style Toggle for Flowcharts (直角圆角 vs 平滑曲线) -->
        <button
          v-if="diagramInfo.type === 'flowchart' && viewMode === 'chart'"
          class="tool-btn"
          :title="curveMode === 'linear' ? '当前：折线圆角 (点击切换为平滑曲线)' : '当前：平滑曲线 (点击切换为折线圆角)'"
          @click="toggleCurveMode"
        >
          <Icon :icon="curveMode === 'linear' ? 'ph:arrows-split' : 'ph:bezier-curve'" />
          <span class="btn-text">{{ curveMode === 'linear' ? '折线' : '曲线' }}</span>
        </button>

        <!-- Copy Code -->
        <button
          class="tool-btn"
          title="复制 Mermaid 源码"
          @click="handleCopyCode"
        >
          <Icon :icon="copiedCode ? 'ph:check-bold' : 'ph:copy'" :class="{ 'text-success': copiedCode }" />
          <span class="btn-text">{{ copiedCode ? '已复制' : '复制' }}</span>
        </button>

        <!-- Export Dropdown -->
        <el-dropdown trigger="click" @command="handleExport">
          <button class="tool-btn" title="导出图片">
            <Icon icon="ph:download-simple" />
            <span class="btn-text">导出</span>
          </button>
          <template #dropdown>
            <el-dropdown-menu class="mermaid-export-menu">
              <el-dropdown-item command="png">
                <Icon icon="ph:image" class="menu-icon" />
                <span>导出 PNG (高清)</span>
              </el-dropdown-item>
              <el-dropdown-item command="svg">
                <Icon icon="ph:file-svg" class="menu-icon" />
                <span>导出 SVG 矢量图</span>
              </el-dropdown-item>
              <el-dropdown-item command="copy-svg" divided>
                <Icon icon="ph:copy" class="menu-icon" />
                <span>复制 SVG 代码</span>
              </el-dropdown-item>
            </el-dropdown-menu>
          </template>
        </el-dropdown>

        <!-- Fullscreen Preview -->
        <button
          class="tool-btn expand-btn"
          title="全屏放大与交互查看"
          @click="openModal"
        >
          <Icon icon="ph:arrows-out-simple" />
        </button>
      </div>
    </div>

    <!-- Body Area -->
    <div class="mermaid-body">
      <!-- Loading State -->
      <div v-if="loading && !svgContent" class="mermaid-loading">
        <div class="loading-spinner"></div>
        <span>正在渲染图表...</span>
      </div>

      <!-- Error State -->
      <div v-else-if="renderError && viewMode === 'chart'" class="mermaid-error">
        <div class="error-header">
          <Icon icon="ph:warning-circle-fill" class="error-icon" />
          <div class="error-info">
            <div class="error-title">图表语法解析异常</div>
            <div class="error-msg">{{ renderError }}</div>
          </div>
          <el-button size="small" plain type="primary" @click="handleRetry">
            <Icon icon="ph:arrows-clockwise" class="btn-icon" />
            <span>重试</span>
          </el-button>
        </div>
        <!-- Raw code fallback in error -->
        <div class="error-code-preview">
          <div class="code-preview-bar">
            <span>Mermaid 源码：</span>
            <button class="mini-copy-btn" @click="handleCopyCode">复制源码</button>
          </div>
          <pre><code>{{ code }}</code></pre>
        </div>
      </div>

      <!-- Code View Mode -->
      <div v-else-if="viewMode === 'code'" class="mermaid-code-view">
        <pre><code class="language-mermaid">{{ code }}</code></pre>
      </div>

      <!-- Rendered Chart View Mode -->
      <div
        v-else
        class="mermaid-chart-view"
        @dblclick="openModal"
        title="双击全屏放大查看"
      >
        <div class="svg-container" v-html="svgContent"></div>
        <div class="chart-hover-hint">
          <Icon icon="ph:magnifying-glass-plus" />
          <span>点击或双击全屏交互预览</span>
        </div>
      </div>
    </div>

    <!-- Interactive Fullscreen Zoom & Pan Modal -->
    <teleport to="body">
      <transition name="mf-modal-fade">
        <div
          v-if="isModalOpen"
          class="mermaid-modal-overlay"
          @click.self="closeModal"
          @keydown.esc="closeModal"
          tabindex="-1"
          ref="modalOverlayRef"
        >
          <div class="mermaid-modal-content">
            <!-- Modal Header -->
            <div class="modal-header">
              <div class="modal-title">
                <Icon :icon="diagramInfo.icon" class="modal-type-icon" />
                <span>{{ diagramInfo.label }} - 全屏预览</span>
              </div>
              <div class="modal-header-actions">
                <button class="modal-tool-btn" title="导出 PNG" @click="handleExport('png')">
                  <Icon icon="ph:image" />
                  <span>导出 PNG</span>
                </button>
                <button class="modal-tool-btn" title="导出 SVG" @click="handleExport('svg')">
                  <Icon icon="ph:file-svg" />
                  <span>导出 SVG</span>
                </button>
                <button class="modal-close-btn" title="关闭 (ESC)" @click="closeModal">
                  <Icon icon="ph:x-bold" />
                </button>
              </div>
            </div>

            <!-- Modal Interactive Canvas -->
            <div
              class="modal-canvas"
              ref="canvasRef"
              :class="{ 'is-dragging': isDragging }"
              @wheel.prevent="handleWheel"
              @mousedown="startPan"
              @mousemove="onPan"
              @mouseup="endPan"
              @mouseleave="endPan"
            >
              <div
                class="modal-svg-wrapper"
                :style="{
                  transform: `translate(${panX}px, ${panY}px) scale(${zoomScale})`,
                  transformOrigin: 'center center',
                }"
                v-html="svgContent"
              ></div>
            </div>

            <!-- Floating Bottom Controls Pill -->
            <div class="modal-controls-pill">
              <button class="pill-btn" title="缩小 (滚轮向下)" @click="zoomOut">
                <Icon icon="ph:minus-bold" />
              </button>
              <span class="pill-zoom-text">{{ Math.round(zoomScale * 100) }}%</span>
              <button class="pill-btn" title="放大 (滚轮向上)" @click="zoomIn">
                <Icon icon="ph:plus-bold" />
              </button>
              <div class="pill-divider"></div>
              <button class="pill-btn text-btn" title="自适应窗口" @click="fitView">
                <Icon icon="ph:frame-corners" />
                <span>自适应</span>
              </button>
              <button class="pill-btn text-btn" title="重置比例 (1:1)" @click="resetTransform">
                <Icon icon="ph:arrow-counter-clockwise" />
                <span>重置</span>
              </button>
              <div class="pill-divider"></div>
              <div class="pill-hint">
                <Icon icon="ph:hand-grabbing" />
                <span>按住拖拽 · 滚轮缩放</span>
              </div>
            </div>
          </div>
        </div>
      </transition>
    </teleport>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, nextTick } from 'vue'
import { Icon } from '@iconify/vue'
import { ElMessage } from 'element-plus'
import {
  detectDiagramType,
  renderMermaidSvg,
  downloadSvg,
  downloadPng,
} from '@/utils/mermaid'

const props = defineProps<{
  code: string
  isStreaming?: boolean
}>()

const loading = ref(true)
const svgContent = ref('')
const renderError = ref('')
const viewMode = ref<'chart' | 'code'>('chart')
const copiedCode = ref(false)
const curveMode = ref<'linear' | 'basis'>('linear')
let copyTimer: number | null = null

// Diagram type information
const diagramInfo = computed(() => detectDiagramType(props.code))

// Unique container ID for Mermaid render
let seq = 0
function generateId() {
  return `mermaid-${Date.now()}-${Math.random().toString(36).slice(2, 7)}-${seq++}`
}

async function renderDiagram(isSilent = false) {
  if (!props.code || !props.code.trim()) {
    loading.value = false
    svgContent.value = ''
    renderError.value = ''
    return
  }

  loading.value = true
  if (!isSilent) {
    renderError.value = ''
  }

  try {
    const id = generateId()
    const result = await renderMermaidSvg(id, props.code, {
      curve: curveMode.value,
    })
    svgContent.value = result.svg
    renderError.value = ''
  } catch (err: unknown) {
    if (!isSilent && !props.isStreaming) {
      const errorMsg = err instanceof Error ? err.message : String(err)
      renderError.value = errorMsg
    }
  } finally {
    loading.value = false
  }
}

function toggleCurveMode() {
  curveMode.value = curveMode.value === 'linear' ? 'basis' : 'linear'
  renderDiagram(false)
}

function handleRetry() {
  renderDiagram()
}

function toggleViewMode() {
  viewMode.value = viewMode.value === 'chart' ? 'code' : 'chart'
}

async function handleCopyCode() {
  try {
    await navigator.clipboard.writeText(props.code)
    copiedCode.value = true
    ElMessage.success('Mermaid 源码已复制')
    if (copyTimer) clearTimeout(copyTimer)
    copyTimer = window.setTimeout(() => {
      copiedCode.value = false
    }, 2000)
  } catch {
    ElMessage.error('复制失败，请手动复制')
  }
}

async function handleExport(type: string) {
  if (!svgContent.value) {
    ElMessage.warning('暂无可导出的图表内容')
    return
  }

  const baseName = `minefolio-${diagramInfo.value.type}-${Date.now()}`

  if (type === 'svg') {
    downloadSvg(svgContent.value, `${baseName}.svg`)
    ElMessage.success('已导出 SVG 矢量图')
  } else if (type === 'png') {
    try {
      ElMessage.info('正在生成高清 PNG...')
      await downloadPng(svgContent.value, `${baseName}.png`, 2)
      ElMessage.success('已导出 PNG 图片')
    } catch {
      ElMessage.error('导出 PNG 失败')
    }
  } else if (type === 'copy-svg') {
    try {
      await navigator.clipboard.writeText(svgContent.value)
      ElMessage.success('SVG 代码已复制到剪贴板')
    } catch {
      ElMessage.error('复制失败')
    }
  }
}

// ---------------- Modal & Zoom / Pan Logic ----------------
const isModalOpen = ref(false)
const modalOverlayRef = ref<HTMLElement | null>(null)
const canvasRef = ref<HTMLElement | null>(null)
const zoomScale = ref(1)
const panX = ref(0)
const panY = ref(0)
const isDragging = ref(false)
let startX = 0
let startY = 0

function openModal() {
  if (!svgContent.value) return
  isModalOpen.value = true
  resetTransform()
  nextTick(() => {
    modalOverlayRef.value?.focus()
  })
}

function closeModal() {
  isModalOpen.value = false
}

function resetTransform() {
  zoomScale.value = 1
  panX.value = 0
  panY.value = 0
}

function fitView() {
  zoomScale.value = 1.15
  panX.value = 0
  panY.value = 0
}

function zoomIn() {
  zoomScale.value = Math.min(5, Number((zoomScale.value * 1.25).toFixed(2)))
}

function zoomOut() {
  zoomScale.value = Math.max(0.2, Number((zoomScale.value / 1.25).toFixed(2)))
}

function handleWheel(e: WheelEvent) {
  const delta = e.deltaY < 0 ? 1.15 : 0.85
  const newScale = Math.min(5, Math.max(0.2, zoomScale.value * delta))
  zoomScale.value = Number(newScale.toFixed(2))
}

function startPan(e: MouseEvent) {
  // Only left mouse button initiates dragging
  if (e.button !== 0) return
  isDragging.value = true
  startX = e.clientX - panX.value
  startY = e.clientY - panY.value
}

function onPan(e: MouseEvent) {
  if (!isDragging.value) return
  panX.value = e.clientX - startX
  panY.value = e.clientY - startY
}

function endPan() {
  isDragging.value = false
}

let streamDebounceTimer: number | null = null

watch(
  () => [props.code, props.isStreaming] as const,
  ([newCode, isStreaming]) => {
    if (isStreaming) {
      if (streamDebounceTimer) clearTimeout(streamDebounceTimer)
      streamDebounceTimer = window.setTimeout(() => {
        renderDiagram(true)
      }, 350)
    } else {
      if (streamDebounceTimer) clearTimeout(streamDebounceTimer)
      renderDiagram(false)
    }
  },
  { immediate: true }
)

onMounted(() => {
  renderDiagram(Boolean(props.isStreaming))
})
</script>

<style scoped>
.mermaid-block-wrapper {
  margin: 14px 0;
  background: var(--mf-surface-muted, rgba(15, 23, 42, 0.75));
  border: 1px solid var(--mf-border, rgba(0, 212, 255, 0.15));
  border-radius: var(--mf-radius-lg, 12px);
  overflow: hidden;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.35);
  backdrop-filter: blur(12px);
  transition: border-color 0.2s, box-shadow 0.2s;
}

.mermaid-block-wrapper:hover {
  border-color: var(--mf-border-hover, rgba(0, 212, 255, 0.35));
  box-shadow: 0 6px 24px rgba(0, 212, 255, 0.1);
}

.mermaid-block-wrapper.has-error {
  border-color: rgba(239, 68, 68, 0.4);
}

/* Header */
.mermaid-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  background: rgba(15, 23, 42, 0.9);
  border-bottom: 1px solid var(--mf-border, rgba(0, 212, 255, 0.1));
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.type-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 2px 8px;
  border-radius: 6px;
  background: rgba(0, 212, 255, 0.08);
  border: 1px solid rgba(0, 212, 255, 0.2);
  color: var(--mf-primary, #00d4ff);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.2px;
}

.type-icon {
  font-size: 13px;
}

.streaming-pill {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: #38bdf8;
}

.streaming-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--mf-primary);
  animation: mf-pulse 1.2s infinite;
}

@keyframes mf-pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.3; transform: scale(0.8); }
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.tool-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: rgba(30, 41, 59, 0.6);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 6px;
  padding: 3px 8px;
  color: var(--mf-text-muted, #94a3b8);
  font-size: 11px;
  cursor: pointer;
  transition: all 0.15s;
}

.tool-btn:hover {
  background: rgba(0, 212, 255, 0.12);
  border-color: rgba(0, 212, 255, 0.3);
  color: var(--mf-primary, #00d4ff);
}

.tool-btn.active {
  background: rgba(0, 212, 255, 0.15);
  border-color: var(--mf-primary, #00d4ff);
  color: var(--mf-primary, #00d4ff);
}

.expand-btn {
  padding: 3px 6px;
}

.text-success {
  color: var(--mf-success, #10b981) !important;
}

/* Body */
.mermaid-body {
  position: relative;
  min-height: 80px;
}

.mermaid-loading {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 36px 16px;
  color: var(--mf-text-muted);
  gap: 10px;
  font-size: 12px;
}

.loading-spinner {
  width: 24px;
  height: 24px;
  border: 2px solid rgba(0, 212, 255, 0.2);
  border-top-color: var(--mf-primary);
  border-radius: 50%;
  animation: mf-spin 0.8s linear infinite;
}

@keyframes mf-spin {
  to { transform: rotate(360deg); }
}

/* Chart View */
.mermaid-chart-view {
  position: relative;
  display: flex;
  justify-content: center;
  align-items: center;
  padding: 24px 20px;
  overflow-x: auto;
  cursor: zoom-in;
  background-color: #070e1c;
  background-image: radial-gradient(rgba(0, 212, 255, 0.08) 1.2px, transparent 1.2px);
  background-size: 16px 16px;
}

.svg-container {
  display: flex;
  justify-content: center;
  align-items: center;
  max-width: 100%;
  width: 100%;
}

.svg-container :deep(svg) {
  max-width: 100%;
  height: auto;
  display: block;
  margin: 0 auto;
}

.svg-container :deep(foreignObject) {
  overflow: visible;
}

.svg-container :deep(foreignObject div),
.svg-container :deep(foreignObject span),
.svg-container :deep(.nodeLabel),
.svg-container :deep(.label) {
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'PingFang SC', 'Hiragino Sans GB', 'Microsoft YaHei', sans-serif;
  color: #e2e8f0;
}

.chart-hover-hint {
  position: absolute;
  bottom: 8px;
  right: 10px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  color: var(--mf-text-muted);
  background: rgba(15, 23, 42, 0.85);
  border: 1px solid rgba(255, 255, 255, 0.08);
  padding: 2px 6px;
  border-radius: 4px;
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.2s;
}

.mermaid-chart-view:hover .chart-hover-hint {
  opacity: 0.9;
}

/* Code View */
.mermaid-code-view {
  padding: 12px 16px;
  background: #020617;
  overflow-x: auto;
}

.mermaid-code-view pre {
  margin: 0;
  padding: 0;
  background: transparent;
  border: none;
}

.mermaid-code-view code {
  font-family: var(--mf-font-mono, monospace);
  font-size: 12px;
  color: #38bdf8;
  line-height: 1.6;
}

/* Error View */
.mermaid-error {
  padding: 14px 16px;
  background: rgba(239, 68, 68, 0.06);
}

.error-header {
  display: flex;
  align-items: flex-start;
  gap: 10px;
}

.error-icon {
  font-size: 18px;
  color: var(--mf-danger, #ef4444);
  flex-shrink: 0;
  margin-top: 2px;
}

.error-info {
  flex: 1;
  min-width: 0;
}

.error-title {
  font-size: 13px;
  font-weight: 600;
  color: #fca5a5;
  margin-bottom: 2px;
}

.error-msg {
  font-size: 11px;
  color: var(--mf-text-muted);
  font-family: var(--mf-font-mono, monospace);
  word-break: break-all;
}

.error-code-preview {
  margin-top: 10px;
  border-top: 1px dashed rgba(239, 68, 68, 0.2);
  padding-top: 8px;
}

.code-preview-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 11px;
  color: var(--mf-text-muted);
  margin-bottom: 4px;
}

.mini-copy-btn {
  background: transparent;
  border: none;
  color: var(--mf-primary);
  font-size: 11px;
  cursor: pointer;
  text-decoration: underline;
}

.error-code-preview pre {
  margin: 0;
  padding: 8px 10px;
  background: rgba(0, 0, 0, 0.4);
  border-radius: 6px;
  font-size: 11px;
  color: #94a3b8;
  max-height: 140px;
  overflow-y: auto;
}

/* Modal / Fullscreen */
.mermaid-modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  z-index: 9999;
  background: rgba(4, 8, 18, 0.85);
  backdrop-filter: blur(16px);
  display: flex;
  flex-direction: column;
  outline: none;
}

.mermaid-modal-content {
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100%;
  position: relative;
  overflow: hidden;
}

.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 24px;
  background: rgba(15, 23, 42, 0.85);
  border-bottom: 1px solid var(--mf-border);
  z-index: 10;
}

.modal-title {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 15px;
  font-weight: 600;
  color: var(--mf-text-main);
}

.modal-type-icon {
  font-size: 18px;
  color: var(--mf-primary);
}

.modal-header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.modal-tool-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: var(--mf-surface-muted);
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 6px 12px;
  color: var(--mf-text-main);
  font-size: 12px;
  cursor: pointer;
  transition: all 0.15s;
}

.modal-tool-btn:hover {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}

.modal-close-btn {
  background: transparent;
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 6px;
  color: var(--mf-text-muted);
  font-size: 18px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.15s;
}

.modal-close-btn:hover {
  background: rgba(239, 68, 68, 0.15);
  border-color: var(--mf-danger);
  color: var(--mf-danger);
}

/* Modal Canvas */
.modal-canvas {
  flex: 1;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
  cursor: grab;
  user-select: none;
  background-color: #060b16;
  background-image: radial-gradient(rgba(0, 212, 255, 0.08) 1.2px, transparent 1.2px);
  background-size: 20px 20px;
}

.modal-canvas.is-dragging {
  cursor: grabbing;
}

.modal-svg-wrapper {
  display: flex;
  align-items: center;
  justify-content: center;
  transition: transform 0.05s linear;
  max-width: 90%;
  max-height: 90%;
}

.modal-svg-wrapper :deep(svg) {
  max-width: 100%;
  max-height: 100%;
  display: block;
}

.modal-svg-wrapper :deep(foreignObject) {
  overflow: visible;
}

.modal-svg-wrapper :deep(foreignObject div),
.modal-svg-wrapper :deep(foreignObject span),
.modal-svg-wrapper :deep(.nodeLabel),
.modal-svg-wrapper :deep(.label) {
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'PingFang SC', 'Hiragino Sans GB', 'Microsoft YaHei', sans-serif;
  color: #e2e8f0;
}

/* Floating Controls Pill */
.modal-controls-pill {
  position: absolute;
  bottom: 28px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid var(--mf-border-hover);
  border-radius: 30px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.6), 0 0 16px rgba(0, 212, 255, 0.15);
  backdrop-filter: blur(20px);
  z-index: 10;
}

.pill-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  background: rgba(30, 41, 59, 0.8);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  padding: 5px 10px;
  color: var(--mf-text-main);
  font-size: 13px;
  cursor: pointer;
  transition: all 0.15s;
}

.pill-btn:hover {
  background: var(--mf-primary-light);
  border-color: var(--mf-primary);
  color: var(--mf-primary);
}

.pill-btn.text-btn {
  font-size: 11px;
  padding: 5px 12px;
}

.pill-zoom-text {
  font-size: 12px;
  font-weight: 600;
  color: var(--mf-primary);
  min-width: 44px;
  text-align: center;
  font-family: var(--mf-font-mono, monospace);
}

.pill-divider {
  width: 1px;
  height: 16px;
  background: rgba(255, 255, 255, 0.15);
  margin: 0 4px;
}

.pill-hint {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--mf-text-muted);
  padding: 0 4px;
}

.mf-modal-fade-enter-active,
.mf-modal-fade-leave-active {
  transition: opacity 0.25s ease;
}

.mf-modal-fade-enter-from,
.mf-modal-fade-leave-to {
  opacity: 0;
}
</style>
