// Vitest 环境：jsdom 的 localStorage getter 在某些环境下返回 undefined，
// 且 jsdom 默认缺少 IndexedDB；用内存实现保证 sql.js persist 与非可导出 CryptoKey 往返测试可用。
class MemoryStorage implements Storage {
  private store = new Map<string, string>()

  get length(): number {
    return this.store.size
  }

  clear(): void {
    this.store.clear()
  }

  getItem(key: string): string | null {
    return this.store.has(key) ? (this.store.get(key) as string) : null
  }

  key(index: number): string | null {
    return Array.from(this.store.keys())[index] ?? null
  }

  removeItem(key: string): void {
    this.store.delete(key)
  }

  setItem(key: string, value: string): void {
    this.store.set(key, String(value))
  }
}

if (typeof localStorage === 'undefined' || (Object.getOwnPropertyDescriptor(globalThis, 'localStorage')?.get)) {
  const storage = new MemoryStorage()
  Object.defineProperty(globalThis, 'localStorage', {
    value: storage,
    writable: true,
    configurable: true,
  })
}

if (typeof globalThis.indexedDB === 'undefined') {
  const idbDatabases = new Map<string, Map<string, Map<unknown, unknown>>>()

  const makeRequest = <T = unknown>() => {
    const req: {
      result: T
      error: unknown
      onsuccess: ((ev?: unknown) => void) | null
      onerror: ((ev?: unknown) => void) | null
      onupgradeneeded: ((ev?: unknown) => void) | null
    } = {
      result: undefined as unknown as T,
      error: null,
      onsuccess: null,
      onerror: null,
      onupgradeneeded: null,
    }
    return req
  }

  const fakeIndexedDB = {
    open(name: string) {
      const req = makeRequest<any>()
      queueMicrotask(() => {
        let stores = idbDatabases.get(name)
        const isNew = !stores
        if (!stores) {
          stores = new Map()
          idbDatabases.set(name, stores)
        }
        const db = {
          objectStoreNames: {
            contains: (sName: string) => stores!.has(sName),
          },
          createObjectStore(sName: string) {
            if (!stores!.has(sName)) stores!.set(sName, new Map())
            return {}
          },
          transaction(sName: string) {
            if (!stores!.has(sName)) stores!.set(sName, new Map())
            const storeMap = stores!.get(sName)!
            const tx: { oncomplete: (() => void) | null; onerror: (() => void) | null; objectStore: (n: string) => unknown } = {
              oncomplete: null,
              onerror: null,
              objectStore() {
                return {
                  get(key: unknown) {
                    const getReq = makeRequest<unknown>()
                    getReq.result = storeMap.get(key)
                    queueMicrotask(() => getReq.onsuccess?.())
                    return getReq
                  },
                  put(val: unknown, key: unknown) {
                    storeMap.set(key, val)
                    const putReq = makeRequest<unknown>()
                    putReq.result = key
                    queueMicrotask(() => {
                      putReq.onsuccess?.()
                      tx.oncomplete?.()
                    })
                    return putReq
                  },
                }
              },
            }
            return tx
          },
        }
        req.result = db
        if (isNew) req.onupgradeneeded?.()
        req.onsuccess?.()
      })
      return req
    },
  }

  Object.defineProperty(globalThis, 'indexedDB', {
    value: fakeIndexedDB,
    writable: true,
    configurable: true,
  })
}
