export type JobType = 'ai_prompt' | 'structured_report' | 'system_builtin'
export type TriggerType = 'cron' | 'market_event'
export type DeliveryChannel = 'user_default' | 'custom_webhook' | 'silent_db'
export type ChannelType =
  | 'serverchan'
  | 'pushdeer'
  | 'wechat_work'
  | 'webhook'
  | 'wechat_ilink'
  | 'none'

export interface IlinkChannelStatus {
  has_instance: boolean
  instance_id?: number
  status?: string
  enabled?: boolean
  ilink_bot_id?: string | null
  last_seen_at?: string | null
  last_inbound_at?: string | null
}

export interface ScheduledJob {
  id: number
  user_id: number
  name: string
  description?: string
  job_type: JobType
  trigger_type: TriggerType
  trigger_config: string
  payload_config: string
  delivery_channel: DeliveryChannel
  delivery_config?: string
  enabled: number | boolean
  is_builtin?: number | boolean
  last_run_at?: string
  last_run_status?: string
  last_run_duration_ms?: number
  next_run_at?: string
  created_at: string
  updated_at: string
}

export interface JobRunLog {
  id: number
  job_id: number
  user_id: number
  started_at: string
  completed_at?: string
  status: 'running' | 'success' | 'failed' | 'timeout'
  duration_ms: number
  trigger_reason: string
  output_summary?: string
  error_message?: string
  notification_status?: 'queued' | 'sent' | 'partial' | 'failed' | 'none'
}

export interface UserPushChannel {
  channel_type: ChannelType
  channel_name: string
  webhook_url?: string
  secret_key?: string
  secret_key_masked?: string
  is_active: number
  ilink_status?: IlinkChannelStatus
}

export interface SchedulerDashboard {
  active_jobs: number
  today_runs: number
  next_run_job: {
    name: string
    next_run_at: string
  }
  push_channel: {
    channel_type: ChannelType
    channel_name: string
    is_active: number
    ilink_status?: IlinkChannelStatus
  }
}
