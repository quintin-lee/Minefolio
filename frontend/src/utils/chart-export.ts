import type { ECharts } from '@/utils/echarts'

/**
 * 导出 ECharts 画布为高清 PNG 图片并触发下载
 * @param chart ECharts 实例
 * @param fileName 文件名前缀 (不含或含 .png 扩展名)
 * @param pixelRatio 像素倍率 (默认 2，清晰度翻倍)
 * @param backgroundColor 背景颜色 (默认白色)
 */
export function exportChartPng(
  chart: ECharts | { getDataURL: (opts: any) => string },
  fileName: string,
  pixelRatio = 2,
  backgroundColor = '#ffffff'
) {
  if (!chart || typeof chart.getDataURL !== 'function') return

  const dataUrl = chart.getDataURL({
    type: 'png',
    pixelRatio,
    backgroundColor,
    excludeComponents: ['toolbox'],
  })

  const finalName = fileName.endsWith('.png') ? fileName : `${fileName}.png`
  const link = document.createElement('a')
  link.download = finalName
  link.href = dataUrl
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
}

/**
 * 导出结构化数据为 CSV 文件 (带 UTF-8 BOM，防止 Excel 打开乱码)
 * @param fileName 文件名 (不含或含 .csv 扩展名)
 * @param headers 表头数组
 * @param rows 数据行数组
 */
export function exportChartCsv(
  fileName: string,
  headers: string[],
  rows: (string | number | boolean | null | undefined)[][]
) {
  const escapeCell = (val: unknown) => {
    if (val === null || val === undefined) return '""'
    const str = String(val).replace(/"/g, '""')
    return `"${str}"`
  }

  const csvContent = [
    headers.map(escapeCell).join(','),
    ...rows.map((row) => row.map(escapeCell).join(',')),
  ].join('\r\n')

  // \uFEFF 是 UTF-8 的 BOM 标记，保证 Windows Excel 默认以 UTF-8 正确解析中文字符
  const blob = new Blob(['\uFEFF' + csvContent], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const finalName = fileName.endsWith('.csv') ? fileName : `${fileName}.csv`

  const link = document.createElement('a')
  link.href = url
  link.download = finalName
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  URL.revokeObjectURL(url)
}
