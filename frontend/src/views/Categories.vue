<template>
  <div class="categories-page">
    <div class="page-header">
      <div class="header-title">
        <div class="title-accent"></div>
        <h2>分类管理</h2>
      </div>
      <el-button type="primary" class="action-btn" @click="openDialog(null)">
        <el-icon><Plus /></el-icon> 新增分类
      </el-button>
    </div>

    <!-- Category Type Tabs -->
    <div class="tab-filter-container">
      <el-radio-group v-model="activeTab" class="category-tabs" @change="onTabChange">
        <el-radio-button value="all">全部分类</el-radio-button>
        <el-radio-button value="asset">资产分类</el-radio-button>
        <el-radio-button value="expense">支出分类</el-radio-button>
        <el-radio-button value="income">收入分类</el-radio-button>
        <el-radio-button value="transaction">交易分类</el-radio-button>
      </el-radio-group>
    </div>

    <div class="panels-scroll-wrapper">
      <el-row :gutter="24">
        <el-col :xs="24" :md="9" :lg="8">
          <div class="panel-container tree-panel-container">
            <div class="panel-header panel-header-with-search">
              <h3>分类结构</h3>
              <el-input
                v-model="keyword"
                placeholder="搜索分类名称"
                prefix-icon="Search"
                clearable
                class="search-input"
                size="small"
              />
            </div>
            <div class="panel-body-scroll">
              <div v-if="debouncedKeyword.trim() && filteredFlatCategories.length === 0" class="tree-empty">
                暂无匹配分类
              </div>
              <el-tree
                v-show="!(debouncedKeyword.trim() && filteredFlatCategories.length === 0)"
                ref="treeRef"
                :data="tabTreeData"
                :props="{ label: 'name', children: 'children' }"
                :filter-node-method="(filterTreeNode as any)"
                node-key="id"
                class="premium-tree"
                :expand-on-click-node="false"
                @node-click="onNodeClick"
              >
                <template #default="{ node, data }">
                  <div class="tree-node-wrapper">
                    <div class="tree-node-content">
                      <span class="node-icon">{{ (data as any).icon || defaultIcon(data as Category) }}</span>
                      <span class="node-label">{{ node.label }}</span>
                      <span :class="['mini-type-pill', `pill-${data.type || 'asset'}`]">
                        {{ categoryTypeLabel(data.type) }}
                      </span>
                    </div>
                    <div class="tree-actions">
                      <el-button link size="small" type="primary" @click.stop="openDialog(data)">编辑</el-button>
                      <el-button link size="small" type="danger" @click.stop="handleDelete(data)">删除</el-button>
                    </div>
                  </div>
                </template>
              </el-tree>
            </div>
          </div>
        </el-col>
        <el-col :xs="24" :md="15" :lg="16">
          <div class="panel-container">
            <div class="panel-header">
              <h3>分类列表</h3>
            </div>
            <div class="panel-body-scroll">
              <el-table :data="paginatedFlatCategories" row-key="id" class="premium-table" header-cell-class-name="premium-header"
                :row-class-name="rowClassName" @row-click="onRowClick">
                <el-table-column prop="name" label="名称" min-width="140">
                  <template #default="{ row }">
                    <span class="cat-name-cell">
                      <span class="cat-icon">{{ (row as any).icon || defaultIcon(row as Category) }}</span>
                      {{ row.name }}
                    </span>
                  </template>
                </el-table-column>
                <el-table-column prop="type" label="分类大类" width="110">
                  <template #default="{ row }">
                    <span :class="['type-pill', `pill-${row.type || 'asset'}`]">
                      {{ categoryTypeLabel(row.type) }}
                    </span>
                  </template>
                </el-table-column>
                <el-table-column label="上级分类" min-width="120">
                  <template #default="{ row }">
                    <span class="text-muted">{{ getParentName(row.parent_id) }}</span>
                  </template>
                </el-table-column>
                <el-table-column prop="asset_type" label="资产细分" width="140">
                  <template #default="{ row }">
                    <div class="type-cell" v-if="row.type === 'asset'">
                      <span :class="['status-dot', row.asset_type]"></span>
                      {{ assetTypeLabel(row.asset_type) }}
                    </div>
                    <span v-else class="text-muted">-</span>
                  </template>
                </el-table-column>
                <el-table-column prop="currency" label="币种" width="90">
                  <template #default="{ row }">
                    <span class="currency-pill">{{ row.currency }}</span>
                  </template>
                </el-table-column>
                <el-table-column label="操作" width="120" align="center">
                  <template #default="{ row }">
                    <div class="action-buttons">
                      <el-button link type="primary" @click="openDialog(row)">编辑</el-button>
                      <el-button link type="danger" @click="handleDelete(row)">删除</el-button>
                    </div>
                  </template>
                </el-table-column>
              </el-table>
            </div>
            <div v-if="filteredFlatCategories.length > 0" class="table-pagination">
              <el-pagination
                v-model:current-page="currentPage"
                v-model:page-size="pageSize"
                :total="filteredFlatCategories.length"
                :page-sizes="[20, 50, 100]"
                layout="total, sizes, prev, pager, next"
                background
                small
              />
            </div>
          </div>
        </el-col>
      </el-row>
    </div>

    <el-dialog v-model="dialogVisible" :title="editingId ? '编辑分类' : '新增分类'" width="480px" class="premium-dialog" :show-close="false" destroy-on-close>
      <el-form ref="formRef" :model="form" :rules="rules" label-width="90px" class="premium-form">
        <el-form-item label="分类名称" prop="name">
          <el-input v-model="form.name" placeholder="输入分类名称" />
        </el-form-item>

        <el-form-item label="图标">
          <el-select v-model="form.icon" clearable style="width: 100%" placeholder="选择图标">
            <el-option v-for="ic in iconOptions" :key="ic.value" :label="ic.value" :value="ic.value" />
          </el-select>
        </el-form-item>

        <el-form-item label="分类大类" prop="type">
          <el-select v-model="form.type" style="width: 100%" @change="onFormTypeChange">
            <el-option label="资产分类" value="asset" />
            <el-option label="支出分类" value="expense" />
            <el-option label="收入分类" value="income" />
            <el-option label="交易分类" value="transaction" />
          </el-select>
        </el-form-item>

        <el-form-item label="上级分类">
          <el-cascader v-model="form._parentPath" :options="(parentOptions as any)" :props="{ value: 'id', label: 'name', checkStrictly: true }" placeholder="无（一级分类）" style="width: 100%" @change="onParentChange" />
        </el-form-item>

        <el-form-item v-if="form.type === 'asset'" label="资产类型" prop="asset_type">
          <el-select v-model="form.asset_type" style="width: 100%">
            <el-option v-for="t in assetTypes" :key="t.value" :label="t.label" :value="t.value" />
          </el-select>
        </el-form-item>

        <el-form-item label="币种">
          <el-select v-model="form.currency" style="width: 100%">
            <el-option label="CNY" value="CNY" /><el-option label="USD" value="USD" /><el-option label="EUR" value="EUR" />
          </el-select>
        </el-form-item>
        <el-form-item label="排序">
          <el-input-number v-model="form.sort_order" :min="0" style="width: 100%" :controls="false" />
        </el-form-item>
      </el-form>
      <template #footer>
        <div class="dialog-footer">
          <el-button class="cancel-btn" @click="dialogVisible = false">取消</el-button>
          <el-button type="primary" class="save-btn" :loading="saving" @click="handleSubmit">保存分类</el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, watch, nextTick, onMounted, onActivated, onBeforeUnmount } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { categoriesApi } from '@/api/categories'
import { useCategoryStore } from '@/stores/category'
import { findCategoryPath } from '@/utils/category-tree'
import type { Category, CategoryType } from '@/types'

defineOptions({ name: 'Categories' })

const categoryStore = useCategoryStore()

const activeTab = ref<'all' | 'asset' | 'expense' | 'income' | 'transaction'>('all')
const categories = computed(() => categoryStore.allNodes)
const selectedId = ref<number | null>(null)
const keyword = ref('')
const debouncedKeyword = ref('')
const treeRef = ref<any>(null)
const currentPage = ref(1)
const pageSize = ref(20)
let searchTimer: ReturnType<typeof setTimeout> | null = null

function filterTree(nodes: Category[], kw: string): Category[] {
  const out: Category[] = []
  for (const node of nodes) {
    const nameHit = node.name.toLowerCase().includes(kw)
    const children = node.children ? filterTree(node.children, kw) : []
    if (nameHit || children.length > 0) {
      out.push({ ...node, children: nameHit ? (node.children ?? []) : children })
    }
  }
  return out
}

function filterTreeNode(value: string, data: Category): boolean {
  if (!value) return true
  return (data.name || '').toLowerCase().includes(value)
}

const fullTree = computed(() => {
  const list = categories.value
  if (!list.length) return []
  const hasNested = list.some(n => Array.isArray(n.children) && n.children.length > 0)
  return hasNested ? list : categoryStore.buildTree(list)
})

const tabTreeData = computed(() =>
  activeTab.value === 'all'
    ? fullTree.value
    : fullTree.value.filter(c => c.type === activeTab.value)
)

const tabFlatCategories = computed(() => flatten(tabTreeData.value))

const filteredFlatCategories = computed(() => {
  const kw = debouncedKeyword.value.trim().toLowerCase()
  if (!kw) return tabFlatCategories.value
  return flatten(filterTree(tabTreeData.value, kw))
})

const paginatedFlatCategories = computed(() => {
  const start = (currentPage.value - 1) * pageSize.value
  return filteredFlatCategories.value.slice(start, start + pageSize.value)
})

watch(keyword, (val) => {
  if (searchTimer) clearTimeout(searchTimer)
  searchTimer = setTimeout(() => {
    debouncedKeyword.value = val
    treeRef.value?.filter(val.trim().toLowerCase())
  }, 150)
})

watch([activeTab, debouncedKeyword], () => {
  currentPage.value = 1
})

watch([activeTab, tabTreeData], () => {
  const kw = debouncedKeyword.value.trim().toLowerCase()
  if (kw) {
    nextTick(() => {
      treeRef.value?.filter(kw)
    })
  }
})

watch(
  () => filteredFlatCategories.value.length,
  (total) => {
    const maxPage = Math.max(1, Math.ceil(total / pageSize.value))
    if (currentPage.value > maxPage) {
      currentPage.value = maxPage
    }
  }
)

onBeforeUnmount(() => {
  if (searchTimer) clearTimeout(searchTimer)
})

const dialogVisible = ref(false)
const editingId = ref<number | null>(null)
const saving = ref(false)
const formRef = ref()

const assetTypes = [
  { label: '现金', value: 'cash' }, { label: '股票', value: 'stock' },
  { label: '基金', value: 'fund' }, { label: '债券', value: 'bond' },
  { label: '加密货币', value: 'crypto' }, { label: '房产', value: 'real_estate' },
  { label: '车辆', value: 'vehicle' }, { label: '其他资产', value: 'other_asset' },
  { label: '贷款', value: 'loan' }, { label: '信用卡', value: 'credit_card' },
  { label: '其他负债', value: 'other_liability' },
]

const ASSET_TYPE_LABEL_MAP: Record<string, string> = Object.fromEntries(
  assetTypes.map(t => [t.value, t.label])
)

const iconOptions = [
  { label: '💰 钱包', value: '💰' },
  { label: '🏦 银行', value: '🏦' },
  { label: '💵 现金', value: '💵' },
  { label: '📱 支付宝', value: '📱' },
  { label: '💬 微信', value: '💬' },
  { label: '💳 信用卡', value: '💳' },
  { label: '📈 股票', value: '📈' },
  { label: '📊 基金', value: '📊' },
  { label: '🪙 加密货币', value: '🪙' },
  { label: '💎 投资', value: '💎' },
  { label: '🏠 房产', value: '🏠' },
  { label: '🚗 车辆', value: '🚗' },
  { label: '💸 贷款', value: '💸' },
  { label: '🛒 购物', value: '🛒' },
  { label: '🍔 餐饮', value: '🍔' },
  { label: '🚌 交通', value: '🚌' },
  { label: '🎮 娱乐', value: '🎮' },
  { label: '🏥 医疗', value: '🏥' },
  { label: '📚 教育', value: '📚' },
  { label: '🎁 人情', value: '🎁' },
  { label: '💼 工作', value: '💼' },
  { label: '📋 交易', value: '📋' },
  { label: '⚡ 公用事业', value: '⚡' },
  { label: '🏛 资产', value: '🏛' },
  { label: '💹 理财', value: '💹' },
  { label: '🔑 其他', value: '🔑' },
]

const form = reactive({
  name: '',
  type: 'asset' as CategoryType,
  asset_type: 'cash',
  currency: 'CNY',
  sort_order: 0,
  parent_id: null as number | null,
  _parentPath: [] as number[],
  _hasChildren: false,
  icon: '' as string,
})

const rules = {
  name: [{ required: true, message: '请输入分类名称' }],
  type: [{ required: true, message: '请选择分类大类' }],
  asset_type: [{ required: true, message: '请选择资产类型' }]
}

function flatten(list: Category[]): Category[] {
  const out: Category[] = []
  const walk = (nodes: Category[]) => {
    for (const node of nodes) {
      out.push(node)
      if (node.children?.length) walk(node.children)
    }
  }
  walk(list)
  return out
}

function onNodeClick(data: any) {
  selectedId.value = data?.id ?? null
}

function rowClassName({ row }: { row: any }) {
  return row.id === selectedId.value ? 'premium-row row-selected' : 'premium-row'
}

function onRowClick(row: any) {
  selectedId.value = row.id
}

const categoryNameMap = computed(() => {
  const map = new Map<number, string>()
  const walk = (nodes: Category[]) => {
    for (const c of nodes) {
      if (c.id) map.set(Number(c.id), c.name)
      if (c.children?.length) walk(c.children)
    }
  }
  walk(fullTree.value)
  return map
})

function getParentName(parentId: any): string {
  if (!parentId) return '—'
  return categoryNameMap.value.get(Number(parentId)) || '—'
}

function categoryTypeLabel(t: CategoryType) {
  if (t === 'asset') return '资产'
  if (t === 'expense') return '支出'
  if (t === 'income') return '收入'
  if (t === 'transaction') return '交易'
  return t || '资产'
}

function assetTypeLabel(t: string) { return ASSET_TYPE_LABEL_MAP[t] || t }

const ASSET_ICONS: Record<string, string> = {
  cash: '💵', stock: '📈', fund: '📊', bond: '💎', crypto: '🪙',
  real_estate: '🏠', vehicle: '🚗', other_asset: '📦',
  loan: '💸', credit_card: '💳', other_liability: '⚖️',
}

const CHILD_ICONS: Record<string, string> = {
  // expense children
  '早晚餐/正餐': '🍜', '水果零食': '🍎', '外卖聚餐': '🥡', '咖啡奶茶': '☕',
  '公共交通': '🚇', '打车网约车': '🚕', '加油停车': '⛽', '飞机高铁': '✈️', '高速/停车费': '🛣',
  '服饰鞋包': '👗', '日用百货': '🧴', '数码家电': '💻', '生鲜果蔬': '🥬', '家居清洁': '🧹',
  '房租房贷': '🏘', '水电燃气': '⚡', '网络话费': '📶', '物业费': '🏢', '维修家政': '🔧',
  '游戏影视': '🎬', '运动健身': '🏃', '旅游度假': '🏖', '会员订阅': '📺', '文娱演出': '🎭',
  '药品诊疗': '💊', '保健体检': '🩺', '住院手术': '🏥', '医疗保险': '🛡',
  '礼金红包': '🧧', '孝敬父母': '👨', '请客送礼': '🎁', '捐赠公益': '❤️',
  '学费培训': '📘', '书籍资料': '📖', '在线课程': '💡', '考证报名': '📝',
  '宠物食品': '🦴', '宠物医疗': '🐾', '宠物用品': '🧸', '宠物美容': '✂️',
  '保险费用': '🔒', '其他杂费': '📦',
  // income children
  '基本工资': '💼', '绩效奖金': '⭐', '兼职外包': '🔨', '年终奖': '🎄', '加班费': '⏰', '补贴津贴': '💵',
  '股票/基金收益': '💹', '存款利息': '🪙', '股息分红': '📈', '租金收入': '🏠', '外汇收益': '💱',
  '二手转让': '♻️', '政府补贴': '📢', '退款返现': '🏷', '奖学金/补助': '🎓',
  // transaction children
  '股票买卖': '📈', '基金申赎': '📊', '债券买卖': '🎫', '港股/美股交易': '🌏', '新股申购': '📋',
  '现货买卖': '↕️', '合约质押': '⛓', '交易所出入金': '🔄',
  '银证/出入金': '💸', '存现/取现': '➕', '交易手续费': '🧾', '资产转移': '🔀',
  '贵金属': '🥇', '收藏品': '🏛', '黄金积存': '🪙',
  // asset children
  '现金账户': '💰', '银行存款': '🏦', '支付宝': '📱', '微信零钱': '💬', '余额宝/零钱通': '🐷', '京东金融': '🛒',
  '股票证券': '📉', '基金理财': '📊', '加密货币': '⛓', '债券投资': '💎', '港美股账户': '🌏',
  '房产': '🏠', '车辆': '🚗',
  '信用卡': '💳', '花呗/白条': '📲',
  '房贷/车贷/贷款': '💸', '消费贷/网贷': '📱',
  '应收款项': '🪪', '预付卡/储值卡': '🎫',
}

function defaultIcon(cat: Category) {
  if (cat.name && CHILD_ICONS[cat.name]) return CHILD_ICONS[cat.name]
  if (cat.asset_type && ASSET_ICONS[cat.asset_type]) return ASSET_ICONS[cat.asset_type]
  return { asset: '🏦', expense: '🛒', income: '💰', transaction: '📋' }[cat.type] ?? '🔑'
}

const excludedParentIds = computed(() => {
  const excluded = new Set<number>()
  if (editingId.value == null) return excluded
  const targetId = Number(editingId.value)

  const collectSubtree = (node: Category) => {
    excluded.add(Number(node.id))
    if (node.children?.length) {
      for (const ch of node.children) {
        collectSubtree(ch)
      }
    }
  }

  const findAndCollect = (nodes: Category[]): boolean => {
    for (const node of nodes) {
      if (Number(node.id) === targetId) {
        collectSubtree(node)
        return true
      }
      if (node.children?.length && findAndCollect(node.children)) {
        return true
      }
    }
    return false
  }

  findAndCollect(fullTree.value)
  if (!excluded.has(targetId)) {
    excluded.add(targetId)
  }
  return excluded
})

const parentOptions = computed(() => {
  if (!dialogVisible.value) return []
  const prune = (nodes: Category[]): Category[] =>
    nodes
      .filter(n => !excludedParentIds.value.has(n.id))
      .map(n => ({ ...n, children: n.children?.length ? prune(n.children) : [] }))
  return prune(fullTree.value.filter(c => c.type === form.type))
})

function onTabChange() {
  // tab switch automatically updates computed lists
}

function onFormTypeChange() {
  // Clear parent selection if type changes
  form.parent_id = null
  form._parentPath = []
}

async function loadData(force = false) {
  try {
    await categoryStore.loadCategories(undefined, force)
  } catch (err) {
    console.error('[Categories] loadData failed:', err)
    ElMessage.error('加载分类失败')
  }
}

function openDialog(cat?: any) {
  editingId.value = cat?.id ?? null
  const defaultType = activeTab.value === 'all' ? 'asset' : activeTab.value
  const parentPath = cat?.parent_id ? findCategoryPath(categories.value, Number(cat.parent_id)) : []
  Object.assign(form, cat ? {
    name: cat.name,
    type: cat.type || 'asset',
    asset_type: cat.asset_type || 'cash',
    currency: cat.currency,
    sort_order: cat.sort_order,
    parent_id: cat.parent_id,
    _parentPath: parentPath.length ? parentPath : (cat.parent_id ? [Number(cat.parent_id)] : []),
    _hasChildren: !!(cat.children && cat.children.length > 0),
    icon: cat.icon || ''
  } : {
    name: '',
    type: defaultType,
    asset_type: 'cash',
    currency: 'CNY',
    sort_order: 0,
    parent_id: null,
    _parentPath: [],
    _hasChildren: false,
    icon: ''
  })
  dialogVisible.value = true
}

function onParentChange(val: any) { form.parent_id = (val as number[])?.[(val as number[]).length - 1] ?? null }

async function handleSubmit() {
  if (saving.value) return
  await formRef.value.validate(async (valid: boolean) => {
    if (!valid || saving.value) return
    saving.value = true
    try {
      const data = {
        name: form.name,
        type: form.type,
        asset_type: form.type === 'asset' ? form.asset_type : 'cash',
        currency: form.currency,
        sort_order: form.sort_order,
        parent_id: form.parent_id,
        icon: form.icon || null
      }
      if (editingId.value) await categoriesApi.update(editingId.value, data)
      else await categoriesApi.create(data)
      ElMessage.success('保存成功')
      dialogVisible.value = false
      categoryStore.invalidate()
      await loadData(true)
    } finally { saving.value = false }
  })
}

async function handleDelete(cat: any) {
  try {
    await ElMessageBox.confirm('确定删除该分类吗？', '提示', { type: 'warning' })
  } catch {
    return
  }
  try {
    await categoriesApi.delete(cat.id)
    ElMessage.success('删除成功')
    categoryStore.invalidate()
    await loadData(true)
  } catch (e: any) {
    if (e?.response?.data?.code === 2001) ElMessage.warning('分类下有子分类，请先删除子分类')
    else ElMessage.error('删除失败')
  }
}

onMounted(() => {
  loadData()
})

onActivated(() => {
  if (!categoryStore.loaded) {
    loadData()
  }
})
</script>

<style scoped>
.categories-page {

  padding-bottom: 0;
  background-color: var(--mf-background);
  height: 100%;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.categories-page > .page-header {
  flex-shrink: 0;
}

.categories-page > .tab-filter-container {
  flex-shrink: 0;
}

.panels-scroll-wrapper {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  margin: 0 -12px;
  padding: 0 12px;
}

/* 两栏面板必须固定为滚动容器的高度,否则 el-col 随内容增高,面板被撑成整页长条 */
.panels-scroll-wrapper > .el-row,
.panels-scroll-wrapper .el-col {
  height: 100%;
}

.panel-container {
  background: var(--mf-surface);
  border-radius: var(--mf-radius-lg);
  padding: 20px;
  box-shadow: var(--mf-shadow-sm);
  border: 1px solid var(--mf-border);
  box-sizing: border-box;
  height: 100%;
  min-height: 0;
  display: flex;
  flex-direction: column;
}

.tree-panel-container {
  min-height: 0;
}

@media (max-width: 991px) {
  .categories-page {
    height: auto;
    min-height: 100%;
    overflow: visible;
  }
  .panels-scroll-wrapper {
    flex: none;
    overflow: visible;
  }
  .panels-scroll-wrapper > .el-row,
  .panels-scroll-wrapper .el-col {
    height: auto;
  }
  .panel-container {
    height: auto;
  }
  .tree-panel-container {
    margin-bottom: 20px;
  }
  .panel-body-scroll :deep(.el-table) {
    height: auto;
  }
}

.action-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
  padding: 10px 20px;
  box-shadow: 0 4px 6px -1px rgba(59, 130, 246, 0.2);
  transition: all 0.2s ease;
}

.action-btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 8px -1px rgba(59, 130, 246, 0.3);
}

.tab-filter-container {
  margin-bottom: 20px;
}

.category-tabs {
  display: inline-flex;
  flex-wrap: wrap;
  gap: 8px;
}

.category-tabs :deep(.el-radio-button__inner) {
  border-radius: var(--mf-radius-md) !important;
  border: 1px solid var(--mf-border);
  box-shadow: none !important;
  padding: 8px 18px;
  font-weight: 500;
}

.category-tabs :deep(.el-radio-button.is-active .el-radio-button__inner) {
  background: linear-gradient(135deg, var(--mf-primary), var(--mf-accent)) !important;
  border-color: var(--mf-primary) !important;
  color: #ffffff !important;
}

.type-pill,
.mini-type-pill,
.currency-pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  font-weight: 600;
  white-space: nowrap;
  border: 1px solid transparent;
}

.type-pill {
  font-size: 12px;
  line-height: 18px;
  padding: 2px 8px;
}

.mini-type-pill {
  border-radius: 4px;
  font-size: 10px;
  padding: 0 5px;
  height: 18px;
  line-height: 16px;
  margin-left: 4px;
}

.pill-asset {
  color: var(--el-color-info);
  background-color: var(--el-color-info-light-9);
  border-color: var(--el-color-info-light-8);
}

.pill-expense {
  color: var(--el-color-success);
  background-color: var(--el-color-success-light-9);
  border-color: var(--el-color-success-light-8);
}

.pill-income {
  color: var(--el-color-danger);
  background-color: var(--el-color-danger-light-9);
  border-color: var(--el-color-danger-light-8);
}

.pill-transaction {
  color: var(--el-color-warning);
  background-color: var(--el-color-warning-light-9);
  border-color: var(--el-color-warning-light-8);
}

.currency-pill {
  font-size: 12px;
  line-height: 18px;
  padding: 2px 8px;
  letter-spacing: 0.5px;
  color: var(--mf-text-muted);
  background-color: transparent;
  border-color: var(--mf-border);
}

.table-pagination {
  flex-shrink: 0;
  display: flex;
  justify-content: flex-end;
  padding-top: 14px;
  margin-top: 8px;
  border-top: 1px solid var(--mf-border);
}

.panel-header {
  margin-bottom: 20px;
  flex-shrink: 0;
}

.panel-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
  color: var(--mf-text-main);
  white-space: nowrap;
}

.panel-header-with-search {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.search-input {
  flex: 1 1 auto;
  width: 180px;
  min-width: 90px;
  max-width: 180px;
}

.tree-empty {
  padding: 40px 0;
  text-align: center;
  color: var(--mf-text-placeholder);
  font-size: 13px;
}

.panel-body-scroll {
  flex: 1;
  overflow-y: auto;
  min-height: 0;
}

.premium-tree {
  background: transparent;
  --el-tree-node-hover-bg-color: var(--mf-surface-muted);
}

[data-theme="light"] .premium-tree {
  color: var(--mf-text-main);
}
[data-theme="light"] .premium-tree .el-tree-node__content:hover {
  background: var(--mf-surface-muted);
}
[data-theme="light"] .premium-tree .tree-node-content,
[data-theme="light"] .premium-tree .tree-actions {
  color: var(--mf-text-main);
}
[data-theme="light"] .premium-tree .el-input__wrapper,
[data-theme="light"] .premium-tree .el-input__inner {
  color: var(--mf-text-main);
  background: var(--mf-surface-card);
}

.panel-body-scroll :deep(.el-tree) {
  padding-bottom: 8px;
}

:deep(.premium-tree .el-tree-node__content) {
  height: 40px;
  border-radius: var(--mf-radius-md);
  margin-bottom: 4px;
  transition: background-color 0.15s ease;
}

.tree-node-wrapper {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  padding-right: 8px;
}

.tree-node-content {
  display: flex;
  align-items: center;
  gap: 8px;
}

.node-icon {
  color: var(--mf-text-muted);
  font-size: 14px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 22px;
  flex-shrink: 0;
}

.node-icon.folder {
  font-size: 14px;
}

.node-label {
  font-weight: 500;
  color: var(--mf-text-main);
}

.cat-name-cell {
  display: flex;
  align-items: center;
  gap: 6px;
}

.cat-icon {
  font-size: 15px;
  flex-shrink: 0;
}

.tree-actions {
  opacity: 0;
  transition: opacity 0.2s ease;
  display: flex;
  gap: 4px;
}

:deep(.premium-tree .el-tree-node__content:hover) .tree-actions,
:deep(.premium-tree .el-tree-node__content:focus-within) .tree-actions {
  opacity: 1;
}

.premium-table {
  --el-table-border-color: transparent;
  --el-table-header-bg-color: var(--mf-primary-light);
}

.panel-body-scroll :deep(.el-table) {
  height: 100%;
}

.panel-body-scroll :deep(.el-table__body-wrapper) {
  overflow-y: auto !important;
}

:deep(.premium-header th) {
  background-color: var(--mf-primary-light) !important;
  color: var(--mf-text-muted) !important;
  font-weight: 600;
  text-transform: uppercase;
  font-size: 12px;
  letter-spacing: 0.5px;
  padding: 12px 0;
  border-bottom: 1px solid var(--mf-border) !important;
}

:deep(.premium-row) {
  transition: background-color 0.15s ease;
}

:deep(.premium-row td) {
  border-bottom: 1px solid var(--mf-border);
  padding: 16px 0;
  color: var(--mf-text-main);
}

:deep(.premium-row:hover > td) {
  background-color: var(--mf-primary-light) !important;
}

:deep(.el-table__row.row-selected td) {
  background-color: var(--mf-primary-light) !important;
}

.text-muted {
  color: #64748b;
}

.type-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background-color: var(--mf-primary-light);
}

.status-dot.cash  { background-color: var(--mf-success); }
.status-dot.stock { background-color: var(--mf-primary); }
.status-dot.fund  { background-color: var(--mf-accent); }
.status-dot.crypto { background-color: var(--mf-warning); }
.status-dot.loan  { background-color: var(--mf-danger); }
.status-dot.credit_card { background-color: #fb7185; }

[data-theme="dark"] .status-dot.cash  { box-shadow: 0 0 6px rgba(16,185,129,0.5); }
[data-theme="dark"] .status-dot.stock { box-shadow: 0 0 6px var(--mf-primary-light); }
[data-theme="dark"] .status-dot.fund  { box-shadow: 0 0 6px var(--mf-accent-light); }
[data-theme="dark"] .status-dot.crypto { box-shadow: 0 0 6px rgba(251,191,36,0.5); }
[data-theme="dark"] .status-dot.loan  { box-shadow: 0 0 6px rgba(248,113,113,0.5); }
[data-theme="dark"] .status-dot.credit_card { box-shadow: 0 0 6px rgba(251,113,133,0.5); }

.currency-tag {
  border-radius: 6px;
  font-weight: 600;
  letter-spacing: 0.5px;
}

.action-buttons {
  display: flex;
  gap: 12px;
  justify-content: center;
}

:deep(.premium-dialog) {
  border-radius: var(--mf-radius-xl);
  overflow: hidden;
  box-shadow: var(--mf-shadow-lg);
}

:deep(.premium-dialog .el-dialog__header) {
  margin: 0;

  border-bottom: 1px solid var(--mf-border);
  background: var(--mf-surface);
}

:deep(.premium-dialog .el-dialog__title) {
  font-weight: 600;
  font-size: 18px;
  color: var(--mf-text-main);
}

:deep(.premium-dialog .el-dialog__body) {
  padding: 32px 24px;
  background: var(--mf-background);
}

:deep(.premium-dialog .el-dialog__footer) {
  padding: 16px 24px;
  border-top: 1px solid var(--mf-border);
  background: var(--mf-surface);
  margin: 0;
}

.premium-form .el-form-item {
  margin-bottom: 24px;
}

.premium-form :deep(.el-input__wrapper) {
  background-color: var(--mf-surface-muted) !important;
  box-shadow: 0 0 0 1px var(--mf-border) inset !important;
  border-radius: var(--mf-radius-md);
  padding: 6px 12px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}

.cancel-btn, .save-btn {
  border-radius: var(--mf-radius-md);
  font-weight: 500;
}

.save-btn {
  padding: 8px 24px;
}
</style>
