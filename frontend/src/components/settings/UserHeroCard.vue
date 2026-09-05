<template>
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
          <div class="stat-val mono">v{{ appVersion }}<span v-if="backendVersion"> / v{{ backendVersion }}</span></div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { Icon } from '@iconify/vue'
import { useAuthStore } from '@/stores/auth'
import { useLedgerStore } from '@/stores/ledger'
import { systemApi } from '@/api/system'
import { formatDate } from '@/utils/format'

const auth = useAuthStore()
const ledgerStore = useLedgerStore()
const appVersion = __APP_VERSION__
const backendVersion = ref('')

onMounted(async () => {
  try {
    const status = await systemApi.status()
    backendVersion.value = status.version
  } catch {
    // backend status unavailable (e.g. offline)
  }
})

async function copyUserId() {
  if (!auth.user?.id) return
  await navigator.clipboard.writeText(String(auth.user.id))
  ElMessage.success('账号 ID 已复制到剪贴板')
}
</script>

<style scoped>
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
</style>
