// sql.js (WASM SQLite) wrapper, persisted to localStorage with Web Crypto (AES-256-GCM) encryption.
import initSqlJs, { type Database, type SqlJsStatic } from 'sql.js'
import { LOCAL_SCHEMA } from './schema'
import type { DbResult, SqlValue } from '@/types/mobile'
import { SQL_WASM_BASE64 } from './generated/sql-wasm-base64'
import { encryptPayload, decryptPayload } from '@/utils/crypto-storage'

const STORAGE_KEY = 'minefolio_local_db'
const LOCKED_BACKUP_KEY = 'minefolio_local_db_locked_backup'
let db: Database | null = null
let sqlJs: SqlJsStatic | null = null

export interface LocalDbStatus {
  decryptFailed: boolean
  quotaExceeded: boolean
  lastError: string | null
  hasLockedBackup: boolean
}

const localDbStatus: LocalDbStatus = {
  decryptFailed: false,
  quotaExceeded: false,
  lastError: null,
  hasLockedBackup: false,
}

declare global {
  // eslint-disable-next-line no-var
  var __minefolio_persist_queue__: Promise<void> | undefined
  // eslint-disable-next-line no-var
  var __minefolio_persist_epoch__: number | undefined
}

function getPersistEpoch(): number {
  if (globalThis.__minefolio_persist_epoch__ === undefined) {
    globalThis.__minefolio_persist_epoch__ = 0
  }
  return globalThis.__minefolio_persist_epoch__
}

export function getLocalDbStatus(): LocalDbStatus {
  const hasBackup =
    localDbStatus.hasLockedBackup ||
    (typeof localStorage !== 'undefined' && Boolean(localStorage.getItem(LOCKED_BACKUP_KEY)))
  return { ...localDbStatus, hasLockedBackup: hasBackup }
}

export async function flushPersist(): Promise<void> {
  if (globalThis.__minefolio_persist_queue__) {
    await globalThis.__minefolio_persist_queue__
  }
}

// base64 -> Uint8Array，把 wasm 内嵌进构建产物，运行时无任何网络请求
function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return bytes
}

function bytesToBase64(bytes: Uint8Array): string {
  let bin = ''
  const len = bytes.byteLength
  for (let i = 0; i < len; i += 8192) {
    bin += String.fromCharCode(...bytes.subarray(i, Math.min(i + 8192, len)))
  }
  return btoa(bin)
}

async function ensureSqlJs(): Promise<SqlJsStatic> {
  if (sqlJs) return sqlJs
  // 直接用内嵌的 wasm 二进制（经 manualChunks 拆入 vendor-sql），浏览器/Capacitor/测试均无网络请求
  sqlJs = await initSqlJs({ wasmBinary: base64ToBytes(SQL_WASM_BASE64) })
  return sqlJs
}

export function nowIso(): string {
  return new Date().toISOString()
}

export async function initLocalDb(): Promise<Database> {
  await flushPersist()
  if (db) return db
  const SQL = await ensureSqlJs()
  const saved = typeof localStorage !== 'undefined' ? localStorage.getItem(STORAGE_KEY) : null
  if (saved) {
    try {
      const bytes = await decryptPayload(saved)
      db = new SQL.Database(bytes)
      localDbStatus.decryptFailed = false
      localDbStatus.lastError = null
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      console.error('[localDb] Failed to restore database from localStorage, backing up locked payload before initializing fresh:', err)
      localDbStatus.decryptFailed = true
      localDbStatus.lastError = msg
      if (typeof localStorage !== 'undefined') {
        try {
          if (!localStorage.getItem(LOCKED_BACKUP_KEY)) {
            localStorage.setItem(LOCKED_BACKUP_KEY, saved)
          }
          localDbStatus.hasLockedBackup = true
        } catch {
          // 忽略备份写入配额不足
        }
      }
      db = new SQL.Database()
    }
  } else {
    db = new SQL.Database()
  }
  // 幂等迁移：新建表 + 旧库补列（CREATE/ALTER 均带 IF NOT EXISTS 语义）
  db.run(LOCAL_SCHEMA)
  migrateColumns(db)
  return db
}

/**
 * 旧本地库补列迁移（列已存在时 ALTER 会抛错，捕获后忽略）
 */
function migrateColumns(database: Database): void {
  const stmts = [
    'ALTER TABLE assets ADD COLUMN account_id INTEGER',
    'ALTER TABLE assets ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE transactions ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE daily_expenses ADD COLUMN ledger_id INTEGER DEFAULT 0',
    'ALTER TABLE sync_queue ADD COLUMN ledger_id INTEGER DEFAULT 0',
  ]
  for (const sql of stmts) {
    try {
      database.run(sql)
    } catch {
      // 列已存在 → 忽略
    }
  }
}

export function getDb(): Database {
  if (!db) throw new Error('localDb not initialized; call initLocalDb() first')
  return db
}

export function query(sql: string, params: SqlValue[] = []): DbResult[] {
  return getDb().exec(sql, params) as DbResult[]
}

export function run(sql: string, params: SqlValue[] = []): void {
  getDb().run(sql, params)
}

function isQuotaExceededError(err: unknown): boolean {
  return Boolean(
    err &&
      typeof err === 'object' &&
      ('name' in err && (err as { name: string }).name === 'QuotaExceededError')
  )
}

async function writeEncryptedSnapshot(data: Uint8Array, epoch: number): Promise<void> {
  if (typeof localStorage === 'undefined') return
  if (epoch !== getPersistEpoch()) return
  try {
    const canEncrypt = typeof window !== 'undefined' && Boolean(window.crypto?.subtle)
    const encoded = canEncrypt ? await encryptPayload(data) : 'b64:' + bytesToBase64(data)
    if (epoch !== getPersistEpoch()) return
    localStorage.setItem(STORAGE_KEY, encoded)
    localDbStatus.quotaExceeded = false
  } catch (err) {
    if (epoch !== getPersistEpoch()) return
    const msg = err instanceof Error ? err.message : String(err)
    localDbStatus.lastError = msg
    if (isQuotaExceededError(err)) {
      localDbStatus.quotaExceeded = true
      console.error('[localDb] localStorage quota exceeded while persisting offline database:', err)
      if (typeof window !== 'undefined') {
        window.dispatchEvent(new CustomEvent('minefolio:localdb-quota-exceeded', { detail: { message: msg } }))
      }
    } else {
      console.error('[localDb] Failed to persist SQLite state to localStorage:', err)
    }
  }
}

function enqueuePersist(data: Uint8Array): Promise<void> {
  const epoch = getPersistEpoch()
  const prev = globalThis.__minefolio_persist_queue__ ?? Promise.resolve()
  const next = prev.catch(() => undefined).then(() => writeEncryptedSnapshot(data, epoch))
  globalThis.__minefolio_persist_queue__ = next
  return next
}

export function persist(): void {
  if (!db) return
  if (typeof localStorage === 'undefined') return
  try {
    const data = db.export()
    // 有 WebCrypto 时绝不先明文写入 b64:，统一通过串行异步加密队列落盘
    void enqueuePersist(data)
  } catch (err) {
    console.error('[localDb] Failed to export SQLite state:', err)
  }
}

export async function persistAsync(): Promise<void> {
  if (!db) return
  if (typeof localStorage === 'undefined') return
  try {
    const data = db.export()
    await enqueuePersist(data)
  } catch (err) {
    console.error('[localDb] Failed to export SQLite state:', err)
  }
}

export function resetLocalDb(): void {
  globalThis.__minefolio_persist_epoch__ = getPersistEpoch() + 1
  if (db) {
    db.close()
    db = null
  }
  localDbStatus.decryptFailed = false
  localDbStatus.quotaExceeded = false
  localDbStatus.lastError = null
  localDbStatus.hasLockedBackup = false
  if (typeof localStorage !== 'undefined') {
    localStorage.removeItem(STORAGE_KEY)
    localStorage.removeItem(LOCKED_BACKUP_KEY)
  }
}

// 将 exec 结果转为对象数组（过滤软删记录由调用方决定）
export function rowsFrom(result: DbResult[]): Record<string, unknown>[] {
  const first = result[0]
  if (!first) return []
  const { columns, values } = first
  return values.map((row) => {
    const obj: Record<string, unknown> = {}
    columns.forEach((c, i) => {
      obj[c] = row[i]
    })
    return obj
  })
}
