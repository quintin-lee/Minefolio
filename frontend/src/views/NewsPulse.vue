<template>
  <div class="news-page">
    <!-- Header -->
    <div class="page-header">
      <div class="header-intro">
        <h1 class="page-title">
          <Icon icon="ph:newspaper-duotone" class="title-icon" />
          市场脉搏 (AI 每日资讯分析)
        </h1>
        <p class="page-subtitle">
          每日定时抓取金融要闻，AI 自动分析对您持仓的影响并给出操作建议。
        </p>
      </div>
      <div class="header-actions">
        <el-button
          plain
          :loading="triggering"
          @click="handleTrigger"
        >
          <Icon icon="ph:play-bold" class="btn-icon" />
          立即生成
        </el-button>
        <el-button
          plain
          @click="loadReports"
        >
          <Icon icon="ph:arrows-clockwise" class="btn-icon" />
          刷新
        </el-button>
      </div>
    </div>

    <!-- Reports Timeline -->
    <div v-loading="loading" class="reports-timeline">
      <div v-if="reports.length > 0">
        <div
          v-for="report in reports"
          :key="report.id"
          class="report-card"
          :class="{ 'is-pending': report.status === 'pending', 'is-error': report.status === 'error' }"
          @click="expandReport(report)"
        >
          <div class="report-date-row">
            <span class="date-badge">{{ formatReportDate(report.report_date) }}</span>
            <el-tag
              :type="report.status === 'done' ? 'success' : report.status === 'pending' ? 'warning' : 'danger'"
              size="small"
              effect="light"
            >
              {{ report.status === 'done' ? '已完成' : report.status === 'pending' ? '生成中…' : '失败' }}
            </el-tag>
            <span v-if="report.status === 'done'" class="model-hint">
              {{ report.model_used || 'AI' }} · {{ report.token_cost > 0 ? `$${report.token_cost.toFixed(4)}` : '免费' }}
            </span>
          </div>

          <div class="report-title">{{ report.title || '每日市场脉搏' }}</div>

          <div v-if="report.summary" class="report-summary">{{ report.summary }}</div>

          <!-- 脱敏持仓概览 -->
          <div v-if="(holdingsMap.get(report.id)?.length ?? 0) > 0" class="report-holdings-row">
            <span class="holdings-label">分析基准组合 (脱敏权重)：</span>
            <div class="holdings-pills">
              <span
                v-for="(h, idx) in holdingsMap.get(report.id)"
                :key="idx"
                class="holding-pill"
              >
                {{ h.name }} <small>{{ h.weight }}</small>
              </span>
            </div>
          </div>

          <div
            v-if="expandedId === report.id && report.status === 'done' && report.analysis"
            class="report-body"
            v-html="expandedAnalysisHtml"
          ></div>
          <div v-else-if="expandedId === report.id && report.status === 'done' && fetchingId === report.id" class="report-pending">
            <el-skeleton :rows="3" animated />
          </div>
          <div v-else-if="report.status === 'pending'" class="report-pending">
            <el-skeleton :rows="3" animated />
          </div>
          <div v-else-if="report.status === 'error'" class="report-error">
            {{ report.error_msg || '生成失败，请重试' }}
          </div>
          <div v-else class="report-expand-hint">点击展开分析报告</div>
        </div>

        <!-- 服务端分页 -->
        <div v-if="total > 0" class="load-more-row">
          <el-pagination
            v-model:current-page="currentPage"
            v-model:page-size="pageSize"
            :total="total"
            :page-sizes="[7, 10, 20]"
            layout="total, sizes, prev, pager, next"
            background
            small
            @current-change="loadReports"
            @size-change="handlePageSizeChange"
          />
        </div>
      </div>

      <el-empty v-else-if="!loading" description="暂无 AI 分析报告，点击上方「立即生成」获取今日市场脉搏">
        <template #extra>
          <el-button type="primary" plain @click="handleTrigger">生成今日报告</el-button>
        </template>
      </el-empty>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { renderSafeMarkdown } from '@/utils/markdown'
import type { AiNewsReport } from '@/types'
import { listNewsReports, triggerNewsAnalysis, getNewsReportByDate } from '@/api/ai-news'

const loading = ref(false)
const triggering = ref(false)
const currentPage = ref(1)
const pageSize = ref(10)
const total = ref(0)
const reports = ref<AiNewsReport[]>([])
const expandedId = ref<number>(-1)

/** 展开时按需拉取的正文缓存 (report_date -> 完整 markdown), 避免重复请求与重复渲染 */
const analysisCache = ref<Map<string, string>>(new Map())
const fetchingId = ref<number>(-1)

let pollTimer: ReturnType<typeof setInterval> | null = null

function formatReportDate(dateStr: string): string {
  if (!dateStr) return ''
  const d = new Date(dateStr + 'T00:00:00')
  const days = ['日', '一', '二', '三', '四', '五', '六']
  return `${d.getMonth() + 1}月${d.getDate()}日 周${days[d.getDay()]}`
}

/**
 * 展开报告: 列表是轻量投影 (无 analysis), 此处按需拉取完整正文并缓存。
 * 若该条已带 analysis (如轮询/旧数据), 直接渲染不再请求。
 */
async function expandReport(report: AiNewsReport) {
  if (report.status !== 'done') {
    return
  }
  if (expandedId.value === report.id) {
    expandedId.value = -1
    return
  }
  expandedId.value = report.id
  const cached = analysisCache.value.get(report.report_date)
  if (cached !== undefined) {
    report.analysis = cached
    return
  }
  if (report.analysis && report.analysis.length > 0) {
    analysisCache.value.set(report.report_date, report.analysis)
    return
  }
  if (fetchingId.value !== -1) return
  fetchingId.value = report.id
  try {
    const full = await getNewsReportByDate(report.report_date)
    const body = full.analysis || ''
    analysisCache.value.set(report.report_date, body)
    if (expandedId.value === report.id) {
      report.analysis = body
    }
  } catch {
    if (expandedId.value === report.id) {
      report.analysis = ''
    }
  } finally {
    if (fetchingId.value === report.id) {
      fetchingId.value = -1
    }
  }
}

type HoldingSnapshotItem = { name: string; category: string; weight: string }

function parseHoldings(snapshotStr?: string): HoldingSnapshotItem[] {
  if (!snapshotStr || snapshotStr === '[]') return []
  try {
    const arr = JSON.parse(snapshotStr)
    return Array.isArray(arr) ? arr : []
  } catch {
    return []
  }
}

let holdingsEntryCache = new Map<number, { raw: string; parsed: HoldingSnapshotItem[] }>()
const holdingsMap = computed(() => {
  const nextCache = new Map<number, { raw: string; parsed: HoldingSnapshotItem[] }>()
  const result = new Map<number, HoldingSnapshotItem[]>()
  for (const report of reports.value) {
    const raw = report.holdings_snapshot || ''
    const cached = holdingsEntryCache.get(report.id)
    const parsed = cached && cached.raw === raw ? cached.parsed : parseHoldings(raw)
    nextCache.set(report.id, { raw, parsed })
    result.set(report.id, parsed)
  }
  holdingsEntryCache = nextCache
  return result
})

const renderedMdCache = new Map<number, { raw: string; html: string }>()
const expandedAnalysisHtml = computed(() => {
  if (expandedId.value === -1) return ''
  const report = reports.value.find(r => r.id === expandedId.value)
  if (!report || report.status !== 'done' || !report.analysis) return ''
  const cached = renderedMdCache.get(report.id)
  if (cached && cached.raw === report.analysis) {
    return cached.html
  }
  const html = renderSafeMarkdown(report.analysis)
  renderedMdCache.set(report.id, { raw: report.analysis, html })
  return html
})

let loadReqSeq = 0
let isUnmounted = false

/** 加载当前页报告列表 */
async function loadReports() {
  const reqId = ++loadReqSeq
  loading.value = true
  try {
    const res = await listNewsReports(currentPage.value, pageSize.value)
    if (isUnmounted || reqId !== loadReqSeq) return
    reports.value = res.list
    total.value = typeof res.total === 'number' ? res.total : res.list.length
  } catch (err: unknown) {
    if (isUnmounted || reqId !== loadReqSeq) return
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '加载报告列表失败')
  } finally {
    if (!isUnmounted && reqId === loadReqSeq) {
      loading.value = false
    }
  }
}

function handlePageSizeChange() {
  currentPage.value = 1
  loadReports()
}

async function handleTrigger() {
  if (triggering.value) return
  triggering.value = true
  try {
    await triggerNewsAnalysis()
    ElMessage.success('已触发 AI 分析，请稍候…')
    startPolling()
    /* 刷新首屏 (最新一条 pending -> 完成后转为 done) */
    await loadReports()
  } catch (err: unknown) {
    const e = err as { response?: { data?: { message?: string } }; message?: string }
    ElMessage.error(e?.response?.data?.message || e?.message || '触发失败')
  } finally {
    triggering.value = false
  }
}

function startPolling() {
  if (pollTimer) return
  let pollCount = 0
  pollTimer = setInterval(async () => {
    if (isUnmounted) {
      stopPolling()
      return
    }
    pollCount++
    if (pollCount > 30) {
      stopPolling()
      return
    }
    try {
      /* 轻量轮询: page_size=1 只探顶部一条, 不拉全量正文 */
      const res = await listNewsReports(1, 1)
      if (isUnmounted) {
        stopPolling()
        return
      }
      if (res.list.length > 0 && res.list[0] && res.list[0].status !== 'pending') {
        stopPolling()
        await loadReports()
        ElMessage.success('AI 分析报告已生成完毕')
      }
    } catch {
      // silent
    }
  }, 5000)
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

onMounted(() => {
  isUnmounted = false
  loadReports()
})

onUnmounted(() => {
  isUnmounted = true
  stopPolling()
})
</script>

<style scoped>
.news-page {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 4px 0 24px 0;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 16px;
}

.header-intro {
  max-width: 720px;
}

.page-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 22px;
  font-weight: 700;
  color: var(--el-text-color-primary);
  margin: 0 0 6px 0;
}

.title-icon {
  font-size: 26px;
  color: var(--el-color-primary);
}

.page-subtitle {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.5;
  margin: 0;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.btn-icon {
  margin-right: 4px;
  font-size: 15px;
}

.reports-timeline {
  min-height: 320px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.report-card {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 12px;
  padding: 18px 20px;
  cursor: pointer;
  transition: all 0.2s;
  /* 数据量增大时: 视口外卡片跳过布局/渲染, 降低首帧与滚动成本 (零依赖优化) */
  content-visibility: auto;
  contain-intrinsic-size: auto 120px;
}

.report-card:hover {
  border-color: var(--el-color-primary-light-5);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.06);
  transform: translateY(-1px);
}

.report-card.is-pending {
  opacity: 0.85;
}

.report-card.is-error {
  border-color: var(--el-color-danger-light-7);
}

.report-date-row {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;
}

.date-badge {
  font-size: 13px;
  font-weight: 600;
  color: var(--el-text-color-primary);
  font-family: var(--font-mono, monospace);
}

.model-hint {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
  margin-left: auto;
}

.report-title {
  font-size: 15px;
  font-weight: 500;
  color: var(--el-text-color-regular);
  margin-bottom: 10px;
}

.report-summary {
  font-size: 13px;
  color: var(--el-text-color-secondary);
  line-height: 1.6;
  margin-bottom: 8px;
}

.report-holdings-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 6px;
  margin-bottom: 8px;
  font-size: 12px;
}

.holdings-label {
  color: var(--el-text-color-placeholder);
  font-size: 12px;
}

.holdings-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.holding-pill {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: var(--el-fill-color);
  color: var(--el-text-color-regular);
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
}

.holding-pill small {
  color: var(--el-color-primary);
  font-family: var(--font-mono, monospace);
  font-weight: 500;
}

.report-body {
  margin-top: 12px;
  padding: 14px 16px;
  background: var(--el-fill-color-light);
  border-radius: 8px;
  font-size: 14px;
  line-height: 1.7;
  color: var(--el-text-color-regular);
  max-height: 480px;
  overflow-y: auto;
}

.report-pending {
  margin-top: 10px;
}

.load-more-row {
  display: flex;
  justify-content: center;
  padding: 6px 0;
}

.report-error {
  margin-top: 8px;
  font-size: 13px;
  color: var(--el-color-danger);
}

.report-expand-hint {
  font-size: 12px;
  color: var(--el-text-color-placeholder);
}

@media (max-width: 768px) {
  .page-header {
    flex-direction: column;
    align-items: stretch;
    gap: 12px;
  }
  .page-title {
    font-size: 18px;
  }
  .page-subtitle {
    display: none;
  }
  .header-actions {
    width: 100%;
  }
}
</style>
