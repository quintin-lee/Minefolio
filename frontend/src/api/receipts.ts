/**
 * @file 小票与发票 OCR 智能识别 API 接口模块
 * @description 提供消费小票、购物发票及账单截图的 AI 多模态视觉解析识别接口
 */

// frontend/src/api/receipts.ts
import http from '@/utils/http'

/**
 * 小票/发票 OCR 解析识别结果
 */
export interface ReceiptScanResult {
  /** 识别出的交易日期 (YYYY-MM-DD) */
  date: string
  /** 识别出的消费总金额 */
  amount: number
  /** 判断的交易类型 ('expense' 支出 | 'income' 收入) */
  type: 'expense' | 'income'
  /** 推荐匹配的分类 ID */
  category_id: number
  /** 推荐匹配的分类名称 */
  category_name: string
  /** 识别出的交易对手/商户名称 (如 '山姆会员店', '海底捞') */
  counterparty: string
  /** 消费明细或商品描述摘要 */
  description: string
  /** 结算币种代码 (如 'CNY', 'USD') */
  currency: string
  /** AI 模型对该解析结果的置信度评分 (0.0 - 1.0) */
  confidence: number
}

/**
 * 小票扫描识别请求载荷
 */
export interface ReceiptScanPayload {
  /** 图片 Base64 编码字符串或 Data URL */
  image: string
  /** 指定识别使用的多模态大模型 (可选) */
  model?: string
  /** 指定识别使用的模型提供商 (可选) */
  provider?: string
}

/**
 * 小票识别 API 服务对象
 */
export const receiptsApi = {
  /**
   * 上传小票图片并调用 AI 视觉模型进行结构化字段识别
   * @route POST /api/receipts/scan
   * @param data 扫描请求载荷 (包含图片 Base64 与可选模型配置)
   * @returns 结构化解析识别结果
   */
  scan: (data: ReceiptScanPayload) =>
    http.post<ReceiptScanResult, any, ReceiptScanPayload>('/receipts/scan', data),
}

