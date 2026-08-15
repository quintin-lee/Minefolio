// sql.js (WASM SQLite) wrapper, persisted to localStorage as Uint8Array JSON.
import initSqlJs, { type Database, type SqlJsStatic } from 'sql.js'
import { LOCAL_SCHEMA } from './schema'
import type { DbResult, SqlValue } from '@/types/mobile'
import { SQL_WASM_BASE64 } from './generated/sql-wasm-base64'

const STORAGE_KEY = 'minefolio_local_db'
let db: Database | null = null
let sqlJs: SqlJsStatic | null = null

const isNode = typeof process !== 'undefined' && !!process.versions?.node

// base64 -> Uint8Array，把 wasm 内嵌进构建产物，运行时无任何网络请求
function base64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return bytes
}

function locateFile(file: string): string {
  if (!isNode) return file
  // 测试(jsdom)环境从本地 node_modules 取 wasm；eval('require') 避免浏览器打包解析 node:module
  const { createRequire } = eval('require("node:module")') as typeof import('node:module')
  const requireFromCwd = createRequire(typeof __filename !== 'undefined' ? __filename : process.cwd() + '/index.js')
  return requireFromCwd.resolve(`sql.js/dist/${file}`)
}

async function ensureSqlJs(): Promise<SqlJsStatic> {
  if (sqlJs) return sqlJs
  if (isNode) {
    sqlJs = await initSqlJs({ locateFile })
  } else {
    // 浏览器/Capacitor：直接用内嵌的 wasm 二进制，避免 WebView 里 fetch/XHR 拉取失败
    sqlJs = await initSqlJs({ wasmBinary: base64ToBytes(SQL_WASM_BASE64) })
  }
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
    const bytes = new Uint8Array(JSON.parse(saved))
    db = new SQL.Database(bytes)
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
  const data = db.export()
  localStorage.setItem(STORAGE_KEY, JSON.stringify(Array.from(data)))
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
