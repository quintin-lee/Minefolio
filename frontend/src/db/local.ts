// sql.js (WASM SQLite) wrapper, persisted to localStorage as Uint8Array JSON.
import initSqlJs, { type Database, type SqlJsStatic } from 'sql.js'
import { LOCAL_SCHEMA } from './schema'
import type { DbResult, SqlValue } from '@/types/mobile'
import { SQL_WASM_BASE64 } from './generated/sql-wasm-base64'

const STORAGE_KEY = 'minefolio_local_db'
let db: Database | null = null
let sqlJs: SqlJsStatic | null = null

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

function parseSavedBytes(saved: string): Uint8Array {
  if (saved.startsWith('b64:')) {
    return base64ToBytes(saved.slice(4))
  }
  if (saved.startsWith('[')) {
    return new Uint8Array(JSON.parse(saved))
  }
  try {
    return base64ToBytes(saved)
  } catch {
    return new Uint8Array(JSON.parse(saved))
  }
}

async function ensureSqlJs(): Promise<SqlJsStatic> {
  if (sqlJs) return sqlJs
  // 直接用内嵌的 wasm 二进制，在浏览器、Capacitor 或测试(Node/jsdom)环境均无须文件系统路径或网络请求
  sqlJs = await initSqlJs({ wasmBinary: base64ToBytes(SQL_WASM_BASE64) })
  return sqlJs
}

export function nowIso(): string {
  return new Date().toISOString()
}

export async function initLocalDb(): Promise<Database> {
  if (db) return db
  const SQL = await ensureSqlJs()
  const saved = typeof localStorage !== 'undefined' ? localStorage.getItem(STORAGE_KEY) : null
  if (saved) {
    try {
      const bytes = parseSavedBytes(saved)
      db = new SQL.Database(bytes)
    } catch (err) {
      console.error('[localDb] Failed to restore database from localStorage, initializing fresh:', err)
      db = new SQL.Database()
      db.run(LOCAL_SCHEMA)
    }
  } else {
    db = new SQL.Database()
    db.run(LOCAL_SCHEMA)
  }
  return db
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

export function persist(): void {
  if (!db) return
  if (typeof localStorage === 'undefined') return
  try {
    const data = db.export()
    const encoded = 'b64:' + bytesToBase64(data)
    localStorage.setItem(STORAGE_KEY, encoded)
  } catch (err) {
    console.error('[localDb] Failed to persist SQLite state to localStorage:', err)
  }
}

export function resetLocalDb(): void {
  if (db) {
    db.close()
    db = null
  }
  if (typeof localStorage !== 'undefined') localStorage.removeItem(STORAGE_KEY)
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
