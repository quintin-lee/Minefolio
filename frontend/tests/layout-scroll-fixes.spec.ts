import { describe, it, expect } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const read = (rel: string) => readFileSync(resolve(process.cwd(), rel), 'utf-8')

describe('layout and scrolling audit fixes', () => {
  it('compacts sidebar vertical rhythm in Layout.vue so 17 menu items fit cleanly in 900px', () => {
    const layout = read('src/views/Layout.vue')
    expect(layout).toMatch(/\.logo\s*\{[\s\S]*?height:\s*64px;/)
    expect(layout).toMatch(/\.sidebar-menu\s*\{[\s\S]*?padding:\s*8px 8px;/)
    expect(layout).toMatch(/\.nav-group-label\s*\{[\s\S]*?padding:\s*10px 12px 4px;/)
    expect(layout).toContain('.nav-group-label:first-child')
    expect(layout).toMatch(/\.sidebar-menu :deep\(\.el-menu-item\)\s*\{[\s\S]*?height:\s*36px;[\s\S]*?line-height:\s*36px;/)
  })

  it('makes main container scrollbars explicitly visible across Layout.vue and index.css', () => {
    const layout = read('src/views/Layout.vue')
    expect(layout).toMatch(/\.main\s*\{[\s\S]*?overflow-y:\s*auto;[\s\S]*?scrollbar-gutter:\s*stable;[\s\S]*?scrollbar-width:\s*thin;/)

    const css = read('src/styles/index.css')
    expect(css).toMatch(/::-webkit-scrollbar\s*\{[\s\S]*?width:\s*8px;/)
    expect(css).toMatch(/::-webkit-scrollbar-thumb\s*\{[\s\S]*?0\.42/)
  })

  it('eliminates double nested scrolling and duplicate tree-row expansion in Categories.vue', () => {
    const cat = read('src/views/Categories.vue')
    expect(cat).toContain("children: '__no_tree_children__'")
    expect(cat).toContain('height="100%"')
    expect(cat).toContain('class="panel-body-scroll is-table-body"')
    expect(cat).toContain('filteredFlatCategories.length')
    expect(cat).toContain('totalRoots')
    expect(cat).toMatch(/\.panels-scroll-wrapper\s*\{[\s\S]*?overflow:\s*hidden;/)
    expect(cat).toMatch(/\.panel-body-scroll\.is-table-body\s*\{[\s\S]*?overflow:\s*hidden;/)
  })

  it('unlocks natural table flow on Scheduler.vue while pinning pagination on Skills.vue and Mcp.vue', () => {
    const scheduler = read('src/views/Scheduler.vue')
    expect(scheduler).toMatch(/\.scheduler-page\s*\{[\s\S]*?min-height:\s*100%;/)
    expect(scheduler).not.toMatch(/\.scheduler-page\s*\{[^}]*height:\s*100%;[^}]*overflow:\s*hidden;/s)
    expect(scheduler).toMatch(/\.table-pagination\s*\{[\s\S]*?flex-shrink:\s*0;/)

    for (const file of ['src/views/Skills.vue', 'src/views/Mcp.vue']) {
      const src = read(file)
      expect(src).toContain('class="grid-scroll-area"')
      expect(src).toContain('class="pagination-bar"')
      expect(src).toMatch(/\.grid-scroll-area\s*\{[\s\S]*?overflow-y:\s*auto;/)
      expect(src).toMatch(/\.pagination-bar\s*\{[\s\S]*?flex-shrink:\s*0;/)
      expect(src).toMatch(/@media \(max-width:\s*768px\)/)
    }
  })

  it('keeps Dashboard.vue as a single scroll level inside Layout .main', () => {
    const dashboard = read('src/views/Dashboard.vue')
    expect(dashboard).not.toMatch(/\.dashboard\s*\{[\s\S]*?overflow:\s*auto;/)
  })
})
