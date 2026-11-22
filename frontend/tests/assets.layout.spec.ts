import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'

const assetsSource = readFileSync(resolve(process.cwd(), 'src/views/Assets.vue'), 'utf8')
const assetsMobileSource = readFileSync(resolve(process.cwd(), 'src/views-mobile/AssetsMobile.vue'), 'utf8')

describe('Assets layout scrolling', () => {
  it('prevents flex children inside scrollable table-container from shrinking and clipping assets', () => {
    expect(assetsSource).toMatch(/\.assets-page\s*{[^}]*flex:\s*1;[^}]*min-height:\s*0;/s)
    expect(assetsSource).toMatch(/\.table-container\s*{[^}]*overflow-y:\s*auto;[^}]*flex:\s*1;[^}]*min-height:\s*0;/s)
    expect(assetsSource).toMatch(/\.account-group\s*{[^}]*flex-shrink:\s*0;/s)
    expect(assetsSource).toMatch(/\.add-account-cta\s*{[^}]*flex-shrink:\s*0;/s)
    expect(assetsSource).toMatch(/\.empty-state\s*{[^}]*flex-shrink:\s*0;/s)
  })

  it('gives each account asset list its own vertical scrollbar when it grows large', () => {
    expect(assetsSource).toMatch(/\.account-group-body\s*{[^}]*max-height:\s*[^;]+;[^}]*overflow-y:\s*auto;/s)
    expect(assetsMobileSource).toMatch(/\.account-body\s*{[^}]*max-height:\s*[^;]+;[^}]*overflow-y:\s*auto;/s)
    expect(assetsMobileSource).toMatch(/\.asset-card\s*{[^}]*flex-shrink:\s*0;/s)
  })

  it('switches to page-level scroll below 768px so stacked summary cards do not starve the list', () => {
    expect(assetsSource).toMatch(/@media\s*\(max-width:\s*768px\)\s*{[\s\S]*\.assets-page\s*{[^}]*overflow:\s*visible;/s)
    expect(assetsSource).toMatch(/@media\s*\(max-width:\s*768px\)\s*{[\s\S]*\.table-container\s*{[^}]*flex:\s*none;[^}]*overflow:\s*visible;/s)
  })
})
