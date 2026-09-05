<template>
  <div class="settings-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>{{ t('settings.title') }}</h2>
      </div>
    </div>
    <div class="settings-content">
      <!-- 1. User Profile Hero Section -->
      <div class="user-hero-card">
        <div class="user-hero-main">
          <div class="user-avatar-wrap">
            <div class="user-avatar-large">
              {{ (auth.user?.username || 'U').charAt(0).toUpperCase() }}
            </div>
            <div class="online-indicator" title="当前在线" />
          </div>
          <div class="user-info-text">
            <div class="user-name-row">
              <span class="user-display-name">{{ auth.user?.username || '-' }}</span>
              <el-tag size="small" effect="dark" class="role-badge">
                <Icon icon="ph:shield-check-fill" width="14" />
                <span>已认证</span>
              </el-tag>
              <el-tag size="small" type="info" effect="plain" class="self-host-badge">
                <Icon icon="ph:hard-drives" width="14" />
                <span>自建私有化</span>
              </el-tag>
            </div>
            <div class="user-meta-sub">
              <span class="uid-tag" @click="copyUserId" title="点击复制账号 ID">
                <Icon icon="ph:identification-badge" width="14" />
                <span>UID: {{ auth.user?.id || '-' }}</span>
                <Icon icon="ph:copy" width="12" class="copy-icon" />
              </span>
              <span class="divider-dot">•</span>
              <span class="meta-item">
                <Icon icon="ph:calendar-blank" width="14" />
                <span>注册于 {{ formatDate(auth.user?.created_at || '') }}</span>
              </span>
            </div>
          </div>
        </div>

        <div class="user-stats-strip">
          <div class="stat-pill">
            <div class="stat-icon-box cyan">
              <Icon icon="ph:book-bookmark-bold" width="18" />
            </div>
            <div class="stat-content">
              <div class="stat-label">当前活跃账本</div>
              <div class="stat-val">
                <span class="ledger-name-text">{{ ledgerStore.currentLedger?.name || '默认账本' }}</span>
                <span v-if="ledgerStore.currentLedger?.my_role" class="ledger-role-pill">
                  {{ ledgerStore.currentLedger.my_role === 'owner' ? '所有者' : (ledgerStore.currentLedger.my_role === 'editor' ? '记账者' : '只读') }}
                </span>
              </div>
            </div>
          </div>

          <div class="stat-pill">
            <div class="stat-icon-box purple">
              <Icon icon="ph:users-three-bold" width="18" />
            </div>
            <div class="stat-content">
              <div class="stat-label">参与空间账本</div>
              <div class="stat-val mono">{{ ledgerStore.ledgers.length || 1 }} 个</div>
            </div>
          </div>

          <div class="stat-pill">
            <div class="stat-icon-box green">
              <Icon icon="ph:lock-key-bold" width="18" />
            </div>
            <div class="stat-content">
              <div class="stat-label">传输与数据安全</div>
              <div class="stat-val">RSA-OAEP + HS256</div>
            </div>
          </div>

          <div class="stat-pill">
            <div class="stat-icon-box purple">
              <Icon icon="ph:info-bold" width="18" />
            </div>
            <div class="stat-content">
              <div class="stat-label">应用版本</div>
              <div class="stat-val mono">v{{ __APP_VERSION__ }}</div>
            </div>
          </div>
        </div>
      </div>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.changePassword') }}</h3>
        </div>
        <el-form :model="form" :rules="rules" ref="formRef" label-width="120px" class="premium-form" @keyup.enter="submit">
          <el-form-item :label="t('settings.oldPassword')" prop="old_password">
            <el-input v-model="form.old_password" type="password" show-password :placeholder="t('settings.oldPassword')" />
          </el-form-item>
          <el-form-item :label="t('settings.newPassword')" prop="new_password">
            <el-input v-model="form.new_password" type="password" show-password :placeholder="t('settings.newPassword')" />
          </el-form-item>
          <el-form-item :label="t('settings.confirmPassword')" prop="confirmPassword">
            <el-input v-model="form.confirmPassword" type="password" show-password :placeholder="t('settings.confirmPassword')" />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" class="action-btn" @click="submit" :loading="loading">
              {{ t('settings.savePassword') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>

      <!-- 2FA Settings Card -->
      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header" style="display: flex; justify-content: space-between; align-items: center;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <h3>两步验证 (TOTP 2FA)</h3>
            <el-tag :type="twoFactorEnabled ? 'success' : 'info'" size="small" effect="dark">
              {{ twoFactorEnabled ? '已开启安全保护' : '未开启' }}
            </el-tag>
          </div>
          <div class="header-actions">
            <el-button
              v-if="!twoFactorEnabled"
              type="primary"
              size="small"
              class="action-btn"
              @click="openTwoFactorSetup"
              :loading="twoFactorLoading"
            >
              开启两步验证
            </el-button>
            <el-button
              v-else
              type="danger"
              plain
              size="small"
              @click="handleDisableTwoFactor"
              :loading="twoFactorLoading"
            >
              关闭两步验证
            </el-button>
          </div>
        </div>
        <p class="export-hint">
          两步验证通过基于时间的一次性密码 (TOTP) 保护您的自建金融账本安全。开启后，登录系统时需输入 Google Authenticator、1Password、微软验证器等应用生成的 6 位动态验证码。
        </p>
      </div>

      <!-- 2FA Setup Dialog -->
      <el-dialog
        v-model="twoFactorSetupVisible"
        title="设置两步验证 (TOTP)"
        width="460px"
        append-to-body
        :close-on-click-modal="false"
      >
        <div class="two-factor-setup-content">
          <div class="setup-step">
            <span class="step-num">1</span>
            <span class="step-desc">使用验证器 App（如 Google Authenticator / 1Password）扫描二维码：</span>
          </div>

          <div class="qr-canvas-wrapper">
            <canvas ref="qrCanvasRef" class="qr-canvas"></canvas>
          </div>

          <div class="setup-step">
            <span class="step-num">2</span>
            <span class="step-desc">或手动在验证器中输入密钥：</span>
          </div>

          <div class="secret-copy-box">
            <span class="secret-text">{{ twoFactorSecret }}</span>
            <el-button link type="primary" size="small" @click="copySecret">复制</el-button>
          </div>

          <div class="setup-step" style="margin-top: 16px;">
            <span class="step-num">3</span>
            <span class="step-desc">输入验证器 App 显示的 6 位动态验证码以激活：</span>
          </div>

          <el-input
            v-model="twoFactorVerifyCode"
            placeholder="请输入 6 位动态验证码"
            maxlength="6"
            size="large"
            style="margin-top: 10px; font-family: monospace; font-size: 16px; text-align: center;"
            @keyup.enter="confirmEnableTwoFactor"
          />
        </div>

        <template #footer>
          <div class="dialog-footer">
            <el-button @click="twoFactorSetupVisible = false">取消</el-button>
            <el-button type="primary" :loading="twoFactorLoading" @click="confirmEnableTwoFactor">
              确认并激活
            </el-button>
          </div>
        </template>
      </el-dialog>

      <!-- 2FA Backup Codes Dialog -->
      <el-dialog
        v-model="backupCodesVisible"
        title="两步验证应急备用码"
        width="480px"
        append-to-body
        :close-on-click-modal="false"
      >
        <div class="backup-codes-content">
          <el-alert
            title="请立即保存这些备用码！"
            type="warning"
            :closable="false"
            description="当您遗失手机或无法获取动态验证码时，可以使用应急备用码登录。每个备用码仅限单次使用。"
            show-icon
            style="margin-bottom: 16px;"
          />

          <div class="backup-codes-grid">
            <div v-for="(code, idx) in backupCodes" :key="idx" class="backup-code-item">
              <span class="code-idx">{{ idx + 1 }}.</span>
              <span class="code-val">{{ code }}</span>
            </div>
          </div>
        </div>

        <template #footer>
          <div class="dialog-footer" style="display: flex; justify-content: space-between;">
            <el-button type="info" plain @click="downloadBackupCodes">下载备用码 (.txt)</el-button>
            <div style="display: flex; gap: 8px;">
              <el-button type="primary" plain @click="copyBackupCodes">复制全部</el-button>
              <el-button type="primary" @click="backupCodesVisible = false">我已妥善保存</el-button>
            </div>
          </div>
        </template>
      </el-dialog>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.exportData') }}</h3>
        </div>
        <p class="export-hint">{{ t('settings.exportHint') }}</p>
        <el-button type="primary" class="action-btn" @click="handleExport" :loading="exporting">
          {{ t('settings.exportButton') }}
        </el-button>
      </div>

      <!-- Import Rules Panel -->
      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header" style="display: flex; justify-content: space-between; align-items: center;">
          <h3>账单导入智能规则</h3>
          <div class="header-actions">
            <el-button size="small" @click="handleResetRules" :loading="rulesLoading">恢复默认规则</el-button>
            <el-button type="primary" size="small" @click="openRuleDialog(null)">新建规则</el-button>
          </div>
        </div>
        <p class="export-hint">
          导入 CSV 账单时，系统将按优先级自动匹配关键词，智能归类到对应分类，减少手工调整。
        </p>

        <el-table :data="importRules" v-loading="rulesLoading" size="small" style="margin-top: 12px;">
          <el-table-column prop="keyword" label="匹配关键词" min-width="120" />
          <el-table-column prop="match_field" label="匹配范围" width="100">
            <template #default="{ row }">
              {{ ({ all: '全部', description: '描述', counterparty: '交易方', note: '备注' } as Record<string, string>)[row.match_field] || row.match_field }}
            </template>
          </el-table-column>
          <el-table-column prop="category_name" label="归入分类" min-width="100">
            <template #default="{ row }">
              <span>{{ row.category_name || '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column prop="target_type" label="类型" width="80">
            <template #default="{ row }">
              <el-tag :type="row.target_type === 'income' ? 'success' : 'warning'" size="small" effect="plain">
                {{ row.target_type === 'income' ? '收入' : '支出' }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column prop="priority" label="优先级" width="70" align="center" />
          <el-table-column prop="is_active" label="启用" width="60" align="center">
            <template #default="{ row }">
              <el-tag :type="row.is_active ? 'success' : 'info'" size="small" effect="dark">
                {{ row.is_active ? '是' : '否' }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="操作" width="100" align="center">
            <template #default="{ row }">
              <el-button link type="primary" size="small" @click="openRuleDialog(row as ImportRule)">编辑</el-button>
              <el-button link type="danger" size="small" @click="handleDeleteRule(row.id)">删除</el-button>
            </template>
          </el-table-column>
        </el-table>
      </div>

      <!-- Import Rule Edit Dialog -->
      <el-dialog
        v-model="ruleDialogVisible"
        :title="editingRule?.id ? '编辑导入规则' : '新建导入规则'"
        width="480px"
        append-to-body
      >
        <el-form :model="ruleForm" label-width="90px" size="default">
          <el-form-item label="关键词" required>
            <el-input v-model="ruleForm.keyword" placeholder="例如 美团、滴滴、工资" />
          </el-form-item>
          <el-form-item label="匹配范围">
            <el-select v-model="ruleForm.match_field" style="width: 100%;">
              <el-option label="全部字段" value="all" />
              <el-option label="描述" value="description" />
              <el-option label="交易方" value="counterparty" />
              <el-option label="备注" value="note" />
            </el-select>
          </el-form-item>
          <el-form-item label="目标分类">
            <el-select v-model="ruleForm.category_id" style="width: 100%;" clearable filterable placeholder="选择分类">
              <el-option v-for="cat in flatCategories" :key="cat.id" :label="cat.label" :value="cat.id" />
            </el-select>
          </el-form-item>
          <el-form-item label="交易类型">
            <el-select v-model="ruleForm.target_type" style="width: 100%;">
              <el-option label="支出" value="expense" />
              <el-option label="收入" value="income" />
            </el-select>
          </el-form-item>
          <el-form-item label="优先级">
            <el-input-number v-model="ruleForm.priority" :min="1" :max="999" />
            <span style="margin-left: 8px; color: var(--mf-text-muted); font-size: 12px;">数值越小越优先</span>
          </el-form-item>
          <el-form-item label="启用">
            <el-switch v-model="ruleForm.is_active" />
          </el-form-item>
        </el-form>
        <template #footer>
          <el-button @click="ruleDialogVisible = false">取消</el-button>
          <el-button type="primary" :loading="ruleSaving" @click="saveRule">保存</el-button>
        </template>
      </el-dialog>

      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>{{ t('settings.aiTitle') }}</h3>
        </div>
        <p class="export-hint">{{ t('settings.aiDesc') }}</p>
        
        <div class="provider-list">
          <div v-for="(provider, index) in providerList" :key="index" class="provider-item">
            <div class="provider-header">
              <div class="provider-title-wrap">
                <span class="provider-name">{{ provider.name || provider.id }}</span>
                <el-tag size="small" :type="provider.testResult?.success ? 'success' : 'danger'" v-if="provider.testResult">
                  {{ provider.testResult.success ? `${provider.testResult.latency_ms}ms` : provider.testResult.message }}
                </el-tag>
              </div>
              <div class="provider-actions">
                <el-button text size="small" @click="toggleEdit(index)" title="编辑">
                  <el-icon><Edit /></el-icon>
                </el-button>
                <el-button text size="small" class="delete-btn" @click="removeProvider(index)" title="删除">
                  <el-icon><Delete /></el-icon>
                </el-button>
              </div>
            </div>
            <el-form v-if="editIndex === index" :model="provider" label-width="100px" class="provider-form">
              <el-form-item :label="t('settings.aiProviderId')">
                <el-input v-model="provider.id" :disabled="provider.id.length > 0 && provider.has_api_key" placeholder="例如: qwen, openai, deepseek" />
              </el-form-item>
              <el-form-item :label="t('settings.aiProviderName')">
                <el-input v-model="provider.name" placeholder="显示名称 (如: 通义千问 27B)" />
              </el-form-item>
              <el-form-item :label="t('settings.aiBaseUrl')">
                <el-input v-model="provider.base_url" placeholder="https://api.openai.com/v1 或 http://host:port/v1" />
              </el-form-item>
              <el-form-item :label="t('settings.aiApiKey')">
                <el-input
                  v-model="provider.api_key"
                  type="password"
                  show-password
                  :placeholder="provider.has_api_key ? '•••••••• (已加密存储，留空保持不变)' : 'sk-...'"
                />
              </el-form-item>
              <el-form-item :label="t('settings.aiModels')">
                <div class="models-input-wrap">
                  <el-input v-model="provider.modelsStr" type="textarea" :rows="2" :placeholder="t('settings.aiModelPlaceholder')" />
                  <div class="models-actions">
                    <el-button
                      size="small"
                      :loading="fetchingModelsIndex === index"
                      @click="handleFetchModels(index)"
                    >
                      <Icon icon="ph:cloud-arrow-down-bold" style="margin-right: 4px" />
                      自动拉取模型
                    </el-button>
                  </div>
                </div>
              </el-form-item>
              <el-form-item>
                <div class="form-actions-bar">
                  <div class="left-actions">
                    <el-button
                      size="small"
                      :loading="testingIndex === index"
                      @click="handleTestConnection(index)"
                    >
                      <Icon icon="ph:plug-bold" style="margin-right: 4px" />
                      测试连接
                    </el-button>
                  </div>
                  <div class="right-actions">
                    <el-button type="primary" size="small" @click="saveProvider(index)">确定</el-button>
                    <el-button size="small" @click="cancelEdit">取消</el-button>
                  </div>
                </div>
              </el-form-item>
            </el-form>
            <div v-else class="provider-details">
              <el-tag size="small" class="meta-tag">ID: {{ provider.id }}</el-tag>
              <el-tag size="small" class="meta-tag" v-if="provider.modelsStr">模型: {{ provider.modelsStr }}</el-tag>
              <el-tag size="small" :type="(provider.has_api_key || (provider.api_key && provider.api_key.trim())) ? 'success' : 'info'" class="meta-tag">
                {{ (provider.has_api_key || (provider.api_key && provider.api_key.trim())) ? 'API Key 已配置 (传输加密)' : '未设置 API Key' }}
              </el-tag>
              <el-button
                size="small"
                text
                class="card-test-btn"
                :loading="testingIndex === index"
                @click.stop="handleTestConnection(index)"
              >
                <Icon icon="ph:plug" style="margin-right: 2px" />
                测试连接
              </el-button>
            </div>
          </div>
          <el-button type="primary" plain @click="addProvider" class="add-provider-btn">
            <el-icon><Plus /></el-icon>
            {{ t('settings.aiAddProvider') }}
          </el-button>
        </div>
        
        <el-divider />
        
        <el-form :model="aiForm" label-width="120px" class="premium-form">
          <el-form-item :label="t('settings.aiDefaultProvider')">
            <el-select v-model="aiForm.default_provider" style="width: 100%">
              <el-option
                v-for="p in providerList"
                :key="p.id"
                :label="p.name || p.id"
                :value="p.id"
              />
            </el-select>
          </el-form-item>
          <el-form-item :label="t('settings.aiDefaultModel')">
            <el-select v-model="aiForm.default_model" style="width: 100%">
              <el-option
                v-for="m in allModels"
                :key="m"
                :label="m"
                :value="m"
              />
            </el-select>
          </el-form-item>
          <el-form-item :label="t('settings.aiContextSize')">
            <el-input-number v-model="aiForm.context_size" :min="5" :max="100" :step="5" />
          </el-form-item>
          <el-form-item :label="t('settings.aiSystemPrompt')">
            <el-input v-model="aiForm.system_prompt" type="textarea" :rows="4" :maxlength="500" show-word-limit />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" @click="saveAiSettings" :loading="aiSaving">
              {{ aiSaving ? t('settings.aiSaving') : t('settings.aiSave') }}
            </el-button>
          </el-form-item>
        </el-form>
      </div>

      <!-- 行情同步与网络代理设置 -->
      <div class="panel-container" style="margin-top: 24px;">
        <div class="panel-header">
          <h3>行情同步与网络代理</h3>
        </div>
        <p class="export-hint">配置外部行情源的网络代理（如访问海外美股、加密货币源时可选配置），以及查看行情调度状态。</p>
        <el-form label-width="120px" class="premium-form" style="margin-top: 16px;">
          <el-form-item label="HTTP 代理">
            <el-input v-model="marketForm.market_proxy" placeholder="如: http://127.0.0.1:7890 或 socks5://127.0.0.1:1080 (留空为直连)" />
          </el-form-item>
          <el-form-item label="自动同步模式">
            <el-radio-group v-model="marketForm.market_sync_mode">
              <el-radio value="trading_hours">智能开盘时段 (开盘期刷新 + 夜间基金清算，推荐)</el-radio>
              <el-radio value="interval">全天固定间隔</el-radio>
              <el-radio value="manual">仅手动同步</el-radio>
            </el-radio-group>
          </el-form-item>
          <el-form-item v-if="marketForm.market_sync_mode !== 'manual'" label="同步周期">
            <el-select v-model="marketForm.market_sync_interval_min" style="width: 200px;">
              <el-option :value="15" label="每 15 分钟" />
              <el-option :value="30" label="每 30 分钟" />
              <el-option :value="60" label="每 1 小时" />
              <el-option :value="120" label="每 2 小时" />
              <el-option :value="1440" label="每天一次" />
            </el-select>
          </el-form-item>
          <el-form-item>
            <div style="display: flex; gap: 12px; align-items: center;">
              <el-button type="primary" class="action-btn" :loading="savingMarket" @click="saveMarketSettings">
                保存行情配置
              </el-button>
              <el-button :loading="testingMarketProxy" @click="testMarketProxy">
                测试行情连通性
              </el-button>
              <el-tag v-if="marketTestResult" :type="marketTestResult.success ? 'success' : 'danger'">
                {{ marketTestResult.message }} ({{ marketTestResult.latency_ms }}ms)
              </el-tag>
            </div>
          </el-form-item>
        </el-form>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, computed } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useAuthStore } from '@/stores/auth'
import { useLedgerStore } from '@/stores/ledger'
import { transactionsApi } from '@/api/transactions'
import { getSettings, updateSettings, testAiConnection, fetchAiModels } from '@/api/ai'
import type { AiSettings, AiTestConnectionResult } from '@/api/ai'
import { marketApi } from '@/api/market'
import type { MarketSettings, TestProxyResult } from '@/types'
import { authApi } from '@/api/auth'
import { importRulesApi } from '@/api/importRules'
import type { ImportRule, ImportRuleCreatePayload } from '@/api/importRules'
import { useCategoryStore } from '@/stores/category'
import QRCode from 'qrcode'
import { encryptText } from '@/utils/crypto'
import { Icon } from '@iconify/vue'
import { zhCN } from '@/locales/zh-CN'
import { formatDate } from '@/utils/format'
import type { FormInstance, FormRules } from 'element-plus'

const t = (key: string): string => {
  const keys = key.split('.')
  let obj: unknown = zhCN
  for (const k of keys) {
    if (obj && typeof obj === 'object' && k in obj) {
      obj = (obj as Record<string, unknown>)[k]
    } else {
      return key
    }
  }
  return typeof obj === 'string' ? obj : key
}

const auth = useAuthStore()
const ledgerStore = useLedgerStore()
const loading = ref(false)
const exporting = ref(false)
const formRef = ref<FormInstance>()
const aiSaving = ref(false)
const editIndex = ref(-1)

// Import Rules state
const categoryStore = useCategoryStore()
const importRules = ref<ImportRule[]>([])
const rulesLoading = ref(false)
const ruleDialogVisible = ref(false)
const ruleSaving = ref(false)
const editingRule = ref<ImportRule | null>(null)
const ruleForm = reactive({
  keyword: '',
  match_field: 'all' as string,
  match_type: 'contains' as string,
  category_id: undefined as number | undefined,
  target_type: 'expense' as string,
  priority: 100,
  is_active: true,
})

const flatCategories = computed(() => {
  const result: { id: number; label: string }[] = []
  const tree = categoryStore.buildTree(categoryStore.allNodes)
  for (const c of tree) {
    result.push({ id: c.id, label: c.name })
    if (c.children) {
      for (const child of c.children) {
        result.push({ id: child.id, label: `${c.name} / ${child.name}` })
      }
    }
  }
  return result
})

async function copyUserId() {
  if (!auth.user?.id) return
  await navigator.clipboard.writeText(String(auth.user.id))
  ElMessage.success('账号 ID 已复制到剪贴板')
}

interface ProviderItem {
  id: string
  name: string
  base_url: string
  api_key?: string
  has_api_key?: boolean
  modelsStr: string
  testResult?: AiTestConnectionResult
}
const providerList = ref<ProviderItem[]>([])
const testingIndex = ref<number | null>(null)
const fetchingModelsIndex = ref<number | null>(null)
const aiForm = reactive({
  default_provider: '',
  default_model: '',
  context_size: 20,
  system_prompt: '',
})

const form = reactive({
  old_password: '',
  new_password: '',
  confirmPassword: '',
})

const validateConfirm = (rule: any, value: string, callback: any) => {
  if (value !== form.new_password) {
    callback(new Error(t('settings.passwordMismatch')))
  } else {
    callback()
  }
}

const rules: FormRules = {
  old_password: [{ required: true, message: t('settings.oldPassword'), trigger: 'blur' }],
  new_password: [
    { required: true, message: t('settings.newPassword'), trigger: 'blur' },
    { min: 6, message: t('settings.passwordMin'), trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: t('settings.confirmPassword'), trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' },
  ],
}

const allModels = computed(() => {
  const models = new Set<string>()
  providerList.value.forEach(p => {
    p.modelsStr.split(',').map(s => s.trim()).filter(Boolean).forEach(m => models.add(m))
  })
  return Array.from(models)
})

async function loadAiSettings() {
  try {
    const raw = await getSettings()
    const settings = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: AiSettings }).data : raw) as AiSettings
    if (settings) {
      aiForm.default_provider = settings.default_provider || ''
      aiForm.default_model = settings.default_model || ''
      aiForm.context_size = settings.context_size || 20
      aiForm.system_prompt = settings.system_prompt || ''
      
      providerList.value = (settings.providers ?? []).map((p) => ({
        id: p.id || '',
        name: p.name || p.id || '',
        base_url: p.base_url || '',
        api_key: '',
        has_api_key: !!p.has_api_key,
        modelsStr: Array.isArray(p.models) ? p.models.join(', ') : '',
      }))
    }
  } catch {
    // Load failed silently
  }
}

function toggleEdit(index: number) {
  if (editIndex.value === index) {
    editIndex.value = -1
  } else {
    editIndex.value = index
  }
}

function cancelEdit() {
  editIndex.value = -1
}

function addProvider() {
  providerList.value.push({
    id: '',
    name: '',
    base_url: '',
    api_key: '',
    modelsStr: '',
  })
  editIndex.value = providerList.value.length - 1
}

function saveProvider(index: number) {
  const provider = providerList.value[index]
  if (!provider || !provider.id) {
    ElMessage.error('供应商 ID 不能为空')
    return
  }
  if (provider.api_key && provider.api_key.trim()) {
    provider.has_api_key = true
  }
  editIndex.value = -1
  ElMessage.success('供应商配置已暂存，请点击下方「保存配置」按钮提交生效')
}


async function handleTestConnection(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning('请先填写供应商 ID')
    return
  }
  testingIndex.value = index
  try {
    let api_key_enc: string | undefined = undefined
    if (p.api_key && p.api_key.trim()) {
      api_key_enc = await encryptText(p.api_key.trim())
    }
    const res = await testAiConnection({
      id: p.id,
      base_url: p.base_url,
      api_key_enc,
      model: p.modelsStr.split(',')[0]?.trim() || undefined,
    })
    p.testResult = res
    if (res.success) {
      if (p.api_key && p.api_key.trim()) {
        p.has_api_key = true
      }
      ElMessage.success(`连接测试成功 (耗时 ${res.latency_ms}ms)`)
    } else {
      ElMessage.error(`连接测试失败: ${res.message}`)
    }
  } catch {
    p.testResult = { success: false, latency_ms: 0, message: '请求失败' }
    ElMessage.error('连接测试异常')
  } finally {
    testingIndex.value = null
  }
}

async function handleFetchModels(index: number) {
  const p = providerList.value[index]
  if (!p || !p.id) {
    ElMessage.warning('请先填写供应商 ID')
    return
  }
  fetchingModelsIndex.value = index
  try {
    let api_key_enc: string | undefined = undefined
    if (p.api_key && p.api_key.trim()) {
      api_key_enc = await encryptText(p.api_key.trim())
    }
    const models = await fetchAiModels({
      id: p.id,
      base_url: p.base_url,
      api_key_enc,
    })
    if (models && models.length > 0) {
      p.modelsStr = models.join(', ')
      if (p.api_key && p.api_key.trim()) {
        p.has_api_key = true
      }
      ElMessage.success(`成功拉取 ${models.length} 个可用模型`)
    } else {
      ElMessage.warning('未获取到模型列表，请手动输入')
    }
  } catch {
    ElMessage.error('获取模型列表失败')
  } finally {
    fetchingModelsIndex.value = null
  }
}
async function removeProvider(index: number) {
  try {
    await ElMessageBox.confirm(t('settings.aiConfirmDelete'), '提示', { type: 'warning' })
    providerList.value.splice(index, 1)
    ElMessage.success(t('settings.aiProviderDeleted'))
  } catch {
    // Cancelled
  }
}

async function saveAiSettings() {
  aiSaving.value = true
  try {
    const providers = await Promise.all(
      providerList.value.map(async (p) => {
        let api_key_enc: string | undefined = undefined
        if (p.api_key && p.api_key.trim()) {
          api_key_enc = await encryptText(p.api_key.trim())
        }
        return {
          id: p.id,
          name: p.name,
          base_url: p.base_url,
          api_key_enc,
          api_key: api_key_enc,
          models: p.modelsStr.split(',').map(s => s.trim()).filter(Boolean),
        }
      })
    )
    
    await updateSettings({
      ...aiForm,
      providers,
    })
    ElMessage.success(t('settings.aiSaved'))
    await loadAiSettings()
  } catch {
    ElMessage.error(t('settings.aiSaveFailed') || '保存失败')
  } finally {
    aiSaving.value = false
  }
}

async function submit() {
  if (!formRef.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid) return
    try {
      loading.value = true
      await auth.changePassword(form.old_password, form.new_password)
      ElMessage.success(t('settings.passwordSuccess'))
      form.old_password = ''
      form.new_password = ''
      form.confirmPassword = ''
    } catch {
      // error handled by http interceptor
    } finally {
      loading.value = false
    }
  })
}

async function handleExport() {
  exporting.value = true
  try {
    const blob = await transactionsApi.exportCsv()
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `minefolio_transactions_${new Date().toISOString().slice(0, 10)}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
    ElMessage.success('导出成功')
  } catch {
    ElMessage.error('导出失败')
  } finally {
    exporting.value = false
  }
}

const marketForm = reactive<MarketSettings>({
  market_proxy: '',
  market_auto_sync: true,
  market_sync_interval_min: 30,
  market_sync_mode: 'trading_hours'
})
const savingMarket = ref(false)
const testingMarketProxy = ref(false)
const marketTestResult = ref<TestProxyResult | null>(null)

async function loadMarketSettings() {
  try {
    const res = await marketApi.getSettings()
    if (res) {
      marketForm.market_proxy = res.market_proxy || ''
      marketForm.market_auto_sync = res.market_auto_sync ?? true
      marketForm.market_sync_interval_min = res.market_sync_interval_min || 30
      marketForm.market_sync_mode = res.market_sync_mode || 'trading_hours'
    }
  } catch (err) {
    console.error('[Settings] loadMarketSettings failed:', err)
  }
}

async function saveMarketSettings() {
  savingMarket.value = true
  try {
    await marketApi.updateSettings({
      market_proxy: marketForm.market_proxy,
      market_auto_sync: marketForm.market_sync_mode !== 'manual',
      market_sync_interval_min: marketForm.market_sync_interval_min,
      market_sync_mode: marketForm.market_sync_mode
    })
    ElMessage.success('行情设置保存成功')
  } catch (err: any) {
    ElMessage.error(err?.message || '保存失败')
  } finally {
    savingMarket.value = false
  }
}

async function testMarketProxy() {
  testingMarketProxy.value = true
  marketTestResult.value = null
  try {
    const res = await marketApi.testProxy({ market_proxy: marketForm.market_proxy })
    marketTestResult.value = res
  } catch (err: any) {
    marketTestResult.value = {
      success: false,
      message: err?.message || '测试失败',
      latency_ms: 0
    }
  } finally {
    testingMarketProxy.value = false
  }
}

const twoFactorEnabled = ref(false)
const twoFactorLoading = ref(false)
const twoFactorSetupVisible = ref(false)
const twoFactorSecret = ref('')
const twoFactorOtpauthUrl = ref('')
const twoFactorVerifyCode = ref('')
const qrCanvasRef = ref<HTMLCanvasElement | null>(null)
const backupCodesVisible = ref(false)
const backupCodes = ref<string[]>([])

async function loadTwoFactorStatus() {
  try {
    const raw = await authApi.get2FaStatus()
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { enabled: boolean } }).data : raw) as { enabled: boolean }
    twoFactorEnabled.value = !!res?.enabled
  } catch {
    // ignore
  }
}

async function openTwoFactorSetup() {
  twoFactorLoading.value = true
  try {
    const raw = await authApi.setup2Fa()
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { secret: string; otpauth_url: string } }).data : raw) as { secret: string; otpauth_url: string }
    twoFactorSecret.value = res.secret
    twoFactorOtpauthUrl.value = res.otpauth_url
    twoFactorVerifyCode.value = ''
    twoFactorSetupVisible.value = true
    setTimeout(async () => {
      if (qrCanvasRef.value) {
        await QRCode.toCanvas(qrCanvasRef.value, res.otpauth_url, {
          width: 180,
          margin: 2,
          color: {
            dark: '#000000',
            light: '#ffffff'
          }
        })
      }
    }, 100)
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '获取两步验证密钥失败')
  } finally {
    twoFactorLoading.value = false
  }
}

async function copySecret() {
  if (!twoFactorSecret.value) return
  await navigator.clipboard.writeText(twoFactorSecret.value)
  ElMessage.success('密钥已复制到剪贴板')
}

async function confirmEnableTwoFactor() {
  if (!twoFactorVerifyCode.value.trim() || twoFactorVerifyCode.value.trim().length !== 6) {
    ElMessage.warning('请输入 6 位动态验证码')
    return
  }
  twoFactorLoading.value = true
  try {
    const raw = await authApi.enable2Fa(twoFactorVerifyCode.value.trim())
    const res = (raw && typeof raw === 'object' && 'data' in raw ? (raw as { data: { backup_codes: string[] } }).data : raw) as { backup_codes: string[] }
    twoFactorEnabled.value = true
    twoFactorSetupVisible.value = false
    backupCodes.value = res.backup_codes || []
    backupCodesVisible.value = true
    ElMessage.success('两步验证已成功开启！请妥善保存应急备用码')
  } catch (e: any) {
    ElMessage.error(e?.response?.data?.message || '动态验证码错误，激活失败')
  } finally {
    twoFactorLoading.value = false
  }
}

async function handleDisableTwoFactor() {
  try {
    await ElMessageBox.confirm('确定要关闭两步验证吗？关闭后账户将降低为仅密码保护级别。', '关闭两步验证', {
      confirmButtonText: '确认关闭',
      cancelButtonText: '取消',
      type: 'warning'
    })
    twoFactorLoading.value = true
    await authApi.disable2Fa()
    twoFactorEnabled.value = false
    ElMessage.success('两步验证已成功关闭')
  } catch (e: any) {
    if (e !== 'cancel') {
      ElMessage.error(e?.response?.data?.message || '关闭两步验证失败')
    }
  } finally {
    twoFactorLoading.value = false
  }
}

async function copyBackupCodes() {
  if (!backupCodes.value.length) return
  const text = `Minefolio 2FA 应急备用码 (每个仅限使用一次):\n\n` + backupCodes.value.map((c, i) => `${i + 1}. ${c}`).join('\n')
  await navigator.clipboard.writeText(text)
  ElMessage.success('备用码已复制到剪贴板')
}

function downloadBackupCodes() {
  if (!backupCodes.value.length) return
  const text = `Minefolio 2FA 应急备用码 (每个仅限使用一次):\n\n` + backupCodes.value.map((c, i) => `${i + 1}. ${c}`).join('\n')
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `minefolio-2fa-backup-codes-${auth.user?.username || 'user'}.txt`
  a.click()
  URL.revokeObjectURL(url)
}

// Import Rules methods
async function loadImportRules() {
  rulesLoading.value = true
  try {
    importRules.value = await importRulesApi.list() as unknown as ImportRule[]
  } catch { /* ignore */ } finally {
    rulesLoading.value = false
  }
}

function openRuleDialog(rule: ImportRule | null) {
  editingRule.value = rule
  if (rule) {
    ruleForm.keyword = rule.keyword
    ruleForm.match_field = rule.match_field || 'all'
    ruleForm.match_type = rule.match_type || 'contains'
    ruleForm.category_id = rule.category_id || undefined
    ruleForm.target_type = rule.target_type || 'expense'
    ruleForm.priority = rule.priority || 100
    ruleForm.is_active = !!rule.is_active
  } else {
    ruleForm.keyword = ''
    ruleForm.match_field = 'all'
    ruleForm.match_type = 'contains'
    ruleForm.category_id = undefined
    ruleForm.target_type = 'expense'
    ruleForm.priority = 100
    ruleForm.is_active = true
  }
  ruleDialogVisible.value = true
}

async function saveRule() {
  if (!ruleForm.keyword.trim()) {
    ElMessage.warning('请输入匹配关键词')
    return
  }
  ruleSaving.value = true
  try {
    const payload: ImportRuleCreatePayload = {
      keyword: ruleForm.keyword.trim(),
      match_field: ruleForm.match_field,
      match_type: ruleForm.match_type,
      category_id: ruleForm.category_id,
      target_type: ruleForm.target_type,
      priority: ruleForm.priority,
      is_active: ruleForm.is_active,
    }
    if (editingRule.value?.id) {
      await importRulesApi.update(editingRule.value.id, payload)
      ElMessage.success('规则已更新')
    } else {
      await importRulesApi.create(payload)
      ElMessage.success('规则已创建')
    }
    ruleDialogVisible.value = false
    await loadImportRules()
  } catch { ElMessage.error('保存失败') } finally {
    ruleSaving.value = false
  }
}

async function handleDeleteRule(id: number) {
  try {
    await ElMessageBox.confirm('确定要删除该规则吗？', '确认', { type: 'warning' })
    await importRulesApi.delete(id)
    ElMessage.success('已删除')
    await loadImportRules()
  } catch { /* cancelled */ }
}

async function handleResetRules() {
  try {
    await ElMessageBox.confirm('这将删除所有自定义规则并恢复出厂默认规则，确定继续吗？', '重置确认', { type: 'warning' })
    rulesLoading.value = true
    await importRulesApi.resetDefaults()
    ElMessage.success('已恢复默认规则')
    await loadImportRules()
  } catch { /* cancelled */ } finally {
    rulesLoading.value = false
  }
}

onMounted(() => {
  loadAiSettings()
  loadMarketSettings()
  loadTwoFactorStatus()
  loadImportRules()
  categoryStore.loadCategories()
})
</script>

<style scoped>
.settings-page {
  background-color: var(--mf-background);
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-primary);
  flex-shrink: 0;
}

.header-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.title-accent {
  width: 4px;
  height: 24px;
  background: linear-gradient(180deg, #6366f1, #8b5cf6);
  border-radius: 2px;
}

.header-title h2 {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: var(--text-primary);
}

.settings-content {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.info-cards {
  margin-bottom: 0;
}

.panel-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
}

.panel-header {
  margin-bottom: 20px;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
}

/* User Hero Card Styling */
.user-hero-card {
  background: var(--mf-surface);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-xl);
  padding: 24px 28px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  box-shadow: var(--mf-shadow-md);
  position: relative;
  overflow: hidden;
}

.user-hero-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, #00d4ff 0%, #7c3aed 50%, #10b981 100%);
}

.user-hero-main {
  display: flex;
  align-items: center;
  gap: 20px;
}

.user-avatar-wrap {
  position: relative;
  flex-shrink: 0;
}

.user-avatar-large {
  width: 64px;
  height: 64px;
  border-radius: 50%;
  background: linear-gradient(135deg, #00d4ff 0%, #7c3aed 100%);
  color: #fff;
  font-size: 26px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 0 20px rgba(0, 212, 255, 0.4);
  border: 2px solid rgba(255, 255, 255, 0.2);
}

.online-indicator {
  position: absolute;
  bottom: 2px;
  right: 2px;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #10b981;
  border: 2px solid var(--mf-surface);
  box-shadow: 0 0 8px #10b981;
}

.user-info-text {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.user-name-row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.user-display-name {
  font-size: 22px;
  font-weight: 700;
  color: var(--mf-text-main);
  letter-spacing: 0.5px;
}

.role-badge {
  border-radius: 6px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: rgba(16, 185, 129, 0.15) !important;
  border: 1px solid rgba(16, 185, 129, 0.4) !important;
  color: #10b981 !important;
  font-weight: 600;
}

.self-host-badge {
  border-radius: 6px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: rgba(0, 212, 255, 0.08) !important;
  border: 1px solid rgba(0, 212, 255, 0.25) !important;
  color: var(--mf-primary) !important;
  font-weight: 500;
}

.user-meta-sub {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 13px;
  color: var(--mf-text-secondary);
  flex-wrap: wrap;
}

.uid-tag {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid var(--mf-border);
  padding: 2px 8px;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  font-family: monospace;
  font-size: 12px;
}

.uid-tag:hover {
  background: rgba(0, 212, 255, 0.08);
  border-color: rgba(0, 212, 255, 0.3);
  color: var(--mf-primary);
}

.copy-icon {
  opacity: 0.6;
}

.divider-dot {
  color: var(--mf-text-muted);
}

.meta-item {
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.user-stats-strip {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 14px;
  padding-top: 16px;
  border-top: 1px solid var(--mf-border);
}

.stat-pill {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: rgba(15, 23, 42, 0.45);
  border: 1px solid var(--mf-border);
  border-radius: var(--mf-radius-lg);
  transition: all 0.2s ease;
}

.stat-pill:hover {
  border-color: rgba(0, 212, 255, 0.3);
  background: rgba(0, 212, 255, 0.04);
}

.stat-icon-box {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.stat-icon-box.cyan {
  background: rgba(0, 212, 255, 0.1);
  color: #00d4ff;
  border: 1px solid rgba(0, 212, 255, 0.25);
}

.stat-icon-box.purple {
  background: rgba(124, 58, 237, 0.1);
  color: #a78bfa;
  border: 1px solid rgba(124, 58, 237, 0.25);
}

.stat-icon-box.green {
  background: rgba(16, 185, 129, 0.1);
  color: #34d399;
  border: 1px solid rgba(16, 185, 129, 0.25);
}

.stat-content {
  display: flex;
  flex-direction: column;
  gap: 3px;
  min-width: 0;
}

.stat-label {
  font-size: 11px;
  color: var(--mf-text-secondary);
}

.stat-val {
  font-size: 13.5px;
  font-weight: 600;
  color: var(--mf-text-main);
  display: flex;
  align-items: center;
  gap: 8px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.stat-val.mono {
  font-family: monospace;
}

.ledger-name-text {
  overflow: hidden;
  text-overflow: ellipsis;
}

.ledger-role-pill {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 4px;
  background: rgba(0, 212, 255, 0.15);
  color: #00d4ff;
  font-weight: 600;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: rgba(15, 23, 42, 0.6) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.action-btn {
  width: 160px;
}

.export-hint {
  font-size: 13px;
  color: var(--mf-text-muted);
  margin-bottom: 16px;
  line-height: 1.6;
}

.provider-list {
  margin-bottom: 24px;
}

.provider-item {
  border: 1px solid var(--mf-border);
  border-radius: 8px;
  padding: 12px;
  margin-bottom: 12px;
  background: var(--mf-surface);
}

.provider-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.provider-name {
  font-weight: 500;
  color: var(--mf-text-main);
  font-size: 14px;
}

.provider-actions {
  display: flex;
  gap: 4px;
}

.delete-btn:hover {
  color: var(--color-danger);
}

.provider-details {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.meta-tag {
  font-size: 12px;
}

.provider-form {
  margin-top: 8px;
}

.add-provider-btn {
  width: 100%;
  margin-top: 8px;
}

.provider-title-wrap {
  display: flex;
  align-items: center;
  gap: 8px;
}

.models-input-wrap {
  width: 100%;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.models-actions {
  display: flex;
  justify-content: flex-end;
}

.form-actions-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
}

.card-test-btn {
  font-size: 12px;
  color: var(--mf-primary);
  margin-left: auto;
}

:deep(.el-divider) {
  margin: 24px 0;
}

/* 2FA Setup & Backup Dialogs */
.two-factor-setup-content {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.setup-step {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  width: 100%;
  margin-bottom: 8px;
}

.step-num {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: var(--mf-primary);
  color: #fff;
  font-size: 12px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.step-desc {
  font-size: 13px;
  color: var(--mf-text-primary);
  line-height: 1.5;
}

.qr-canvas-wrapper {
  background: #ffffff;
  padding: 12px;
  border-radius: 8px;
  margin: 8px 0 16px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
}

.qr-canvas {
  display: block;
}

.secret-copy-box {
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: var(--mf-bg-secondary);
  border: 1px solid var(--border-color);
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 12px;
}

.secret-text {
  font-family: 'JetBrains Mono', ui-monospace, monospace;
  font-size: 13px;
  font-weight: 600;
  color: var(--mf-primary);
  letter-spacing: 1px;
}

.backup-codes-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
  background: var(--mf-bg-secondary);
  border: 1px solid var(--border-color);
  padding: 16px;
  border-radius: 8px;
}

.backup-code-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: 'JetBrains Mono', ui-monospace, monospace;
  font-size: 14px;
}

.code-idx {
  color: var(--mf-text-muted);
  width: 20px;
}

.code-val {
  font-weight: 600;
  color: var(--mf-text-primary);
  background: rgba(0, 212, 255, 0.08);
  padding: 2px 6px;
  border-radius: 4px;
}
</style>
