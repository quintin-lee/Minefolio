import http from '@/utils/http'
import type {
  ScheduledJob,
  JobRunLog,
  SchedulerDashboard,
  UserPushChannel
} from '@/types'

export interface JobPaginationResult {
  items: ScheduledJob[]
  total: number
  page: number
  page_size: number
}

export interface LogPaginationResult {
  items: JobRunLog[]
  total: number
  page: number
  page_size: number
}

export const schedulerApi = {
  getDashboard: () => http.get<SchedulerDashboard>('/scheduler/dashboard'),

  listJobs: (page = 1, pageSize = 20) =>
    http.get<JobPaginationResult>('/scheduler/jobs', {
      params: { page, page_size: pageSize }
    }),

  getJob: (id: number) => http.get<ScheduledJob>(`/scheduler/jobs/${id}`),

  createJob: (data: {
    name: string
    description?: string
    job_type: string
    trigger_type: string
    trigger_config?: string
    payload_config?: string
    delivery_channel?: string
    delivery_config?: string
  }) => http.post<ScheduledJob>('/scheduler/jobs', data),

  updateJob: (
    id: number,
    data: {
      name?: string
      description?: string
      trigger_type?: string
      trigger_config?: string
      payload_config?: string
      delivery_channel?: string
      delivery_config?: string
    }
  ) => http.put<{ id: number }>(`/scheduler/jobs/${id}`, data),

  deleteJob: (id: number) => http.delete<{ id: number }>(`/scheduler/jobs/${id}`),

  toggleJob: (id: number, enabled: boolean) =>
    http.post<{ id: number; enabled: boolean }>(`/scheduler/jobs/${id}/toggle`, {
      enabled
    }),

  runJob: (id: number) =>
    http.post<{ message: string }>(`/scheduler/jobs/${id}/run`),

  listLogs: (jobId = 0, page = 1, pageSize = 20) =>
    http.get<LogPaginationResult>(`/scheduler/jobs/${jobId}/logs`, {
      params: { page, page_size: pageSize }
    }),

  getChannel: () => http.get<UserPushChannel>('/scheduler/channel'),

  saveChannel: (data: {
    channel_type: string
    channel_name?: string
    webhook_url?: string
    secret_key?: string
  }) => http.put<{ message: string }>('/scheduler/channel', data),

  testChannel: () => http.post<{ message: string }>('/scheduler/channel/test')
}
