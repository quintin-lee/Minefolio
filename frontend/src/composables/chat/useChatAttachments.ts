import { ref } from 'vue'
import { ElMessage } from 'element-plus'

export interface AttachedImageInfo {
  file: File
  dataUrl: string
  name: string
  sizeText: string
}

export interface ReceiptScanPayload {
  amount?: number | string
  counterparty?: string
  date?: string
  currency?: string
  category_name?: string
  description?: string
  type?: string
}

export interface UseChatAttachmentsOptions {
  scanReceipt: (payload: { image: string }) => Promise<ReceiptScanPayload | null | undefined>
}

export function formatAttachmentSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

/**
 * Manages receipt screenshot attachments (paste/drop/file-select) and AI OCR prompt assembly.
 */
export function useChatAttachments(options: UseChatAttachmentsOptions) {
  const attachedImage = ref<AttachedImageInfo | null>(null)
  const isDragOver = ref(false)
  const isScanningReceipt = ref(false)

  function processImageFile(file: File) {
    if (!file.type.startsWith('image/')) {
      ElMessage.warning('仅支持上传图片格式（如 JPG, PNG, WebP）')
      return
    }
    if (file.size > 10 * 1024 * 1024) {
      ElMessage.warning('图片大小不能超过 10MB')
      return
    }
    const reader = new FileReader()
    reader.onload = (e) => {
      const dataUrl = e.target?.result as string
      if (dataUrl) {
        attachedImage.value = {
          file,
          dataUrl,
          name: file.name || `screenshot_${Date.now()}.png`,
          sizeText: formatAttachmentSize(file.size),
        }
        ElMessage.success('已添加票据截图，发送时将自动调用 AI 多模态识别')
      }
    }
    reader.readAsDataURL(file)
  }

  function handlePaste(e: ClipboardEvent) {
    if (!e.clipboardData?.items) return
    for (let i = 0; i < e.clipboardData.items.length; i++) {
      const item = e.clipboardData.items[i]!
      if (item.type.startsWith('image/')) {
        const file = item.getAsFile()
        if (file) {
          e.preventDefault()
          processImageFile(file)
          return
        }
      }
    }
  }

  function handleDrop(e: DragEvent) {
    isDragOver.value = false
    if (!e.dataTransfer?.files?.length) return
    const file = e.dataTransfer.files[0]!
    if (file.type.startsWith('image/')) {
      processImageFile(file)
    }
  }

  function handleFileSelect(e: Event) {
    const target = e.target as HTMLInputElement
    if (target.files && target.files.length > 0) {
      processImageFile(target.files[0]!)
      target.value = ''
    }
  }

  function removeAttachment() {
    attachedImage.value = null
  }

  async function scanReceiptAttachment(img: AttachedImageInfo, rawText: string): Promise<string> {
    isScanningReceipt.value = true
    try {
      ElMessage.info('正在调用 AI 视觉识别发票与票据明细...')
      const res = await options.scanReceipt({ image: img.dataUrl })
      if (res && res.amount !== undefined) {
        ElMessage.success('票据识别成功，已组装智能记账分析')
        return `【消费票据识别结果】
- 商户/交易对手: ${res.counterparty || '未知'}
- 交易日期: ${res.date}
- 交易金额: ¥${res.amount} (${res.currency || 'CNY'})
- 推荐分类: ${res.category_name || '日常消费'}
- 消费摘要: ${res.description || '无'}
- 交易类型: ${res.type === 'income' ? '收入' : '支出'}

${rawText || '请核对上述识别结果，并生成记账确认卡片协助入账。'}`
      }
      return `[已上传消费票据截图: ${img.name}]\n${rawText || '请根据此消费票据协助记账。'}`
    } catch (err) {
      console.warn('[chat] receipt scan fallback', err)
      return `[已上传消费票据截图: ${img.name}]\n${rawText || '请根据此消费票据协助记账。'}`
    } finally {
      isScanningReceipt.value = false
    }
  }

  return {
    attachedImage,
    isDragOver,
    isScanningReceipt,
    processImageFile,
    handlePaste,
    handleDrop,
    handleFileSelect,
    removeAttachment,
    scanReceiptAttachment,
  }
}
