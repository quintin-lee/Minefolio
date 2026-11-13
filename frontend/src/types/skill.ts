/**
 * Agent Skills 类型定义
 * 符合 Anthropic Agent Skills 规范并支持 L1 元数据、L2 Prompt 指令及 L3 本地沙箱进程配置。
 */

export type SkillCategory = 'finance' | 'investment' | 'expense' | 'tax' | 'risk' | 'other'

export interface AiSkill {
  id: number
  user_id?: number
  name: string
  description: string
  license?: string
  compatibility?: string
  category: string
  tags?: string
  enabled: number
  instructions_l2?: string
  backend?: string
  exec_root?: string
  exec_path?: string
  exec_args?: string
  exec_env?: string
  timeout_ms?: number
  max_output_bytes?: number
  created_at?: string
  updated_at?: string
}

export interface SkillCreatePayload {
  name: string
  description: string
  license?: string
  compatibility?: string
  category?: string
  tags?: string
  enabled?: number
  instructions_l2?: string
  backend?: string
  exec_root?: string
  exec_path?: string
  exec_args?: string
  exec_env?: string
  timeout_ms?: number
  max_output_bytes?: number
}

export interface SkillUpdatePayload {
  description: string
  license?: string
  compatibility?: string
  category?: string
  tags?: string
  instructions_l2?: string
  backend?: string
  exec_root?: string
  exec_path?: string
  exec_args?: string
  exec_env?: string
  timeout_ms?: number
  max_output_bytes?: number
}

export interface SkillTestResult {
  status: 'success' | 'failed'
  mode: 'l2' | 'l3'
  skill_name?: string
  valid?: boolean
  char_count?: number
  token_estimate?: number
  instructions?: string
  instructions_preview?: string
  rc?: number
  output?: string
  exec_result?: unknown
}
