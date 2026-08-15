// Vitest 环境：jsdom 的 localStorage getter 在某些环境下返回 undefined，
// 用内存实现保证 sql.js persist 往返测试可用（与真实浏览器行为一致）。
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
