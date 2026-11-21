import http from '@/utils/http'

export type AsyncTaskStatus = 'pending' | 'running' | 'completed' | 'failed'

export interface AsyncTask<T = unknown> {
  id: number
  user_id: number
  ledger_id: number
  task_type: string
  status: AsyncTaskStatus
  progress: number
  message: string
  error_message: string
  result: T | null
  created_at: string
  started_at: string
  completed_at: string
  updated_at: string
}

export interface AsyncTaskSubmission {
  task_id: number
  task_type?: string
  status: AsyncTaskStatus
  deduplicated?: boolean
}

export function isAsyncTaskSubmission(val: unknown): val is AsyncTaskSubmission {
  return Boolean(
    val &&
    typeof val === 'object' &&
    typeof (val as AsyncTaskSubmission).task_id === 'number' &&
    (val as AsyncTaskSubmission).task_id > 0
  )
}

export interface PollTaskOptions {
  intervalMs?: number
  timeoutMs?: number
  onProgress?: (task: AsyncTask<unknown>) => void
}

export const tasksApi = {
  get: <T = unknown>(id: number) => http.get<AsyncTask<T>>(`/tasks/${id}`),
  list: (params?: { status?: string; task_type?: string; limit?: number }) =>
    http.get<AsyncTask[]>('/tasks', { params }),
}

/**
 * Poll GET /api/tasks/:id until completion or failure.
 */
export async function pollTask<T = unknown>(
  taskId: number,
  options: PollTaskOptions = {},
): Promise<T> {
  const intervalMs = options.intervalMs ?? 300
  const timeoutMs = options.timeoutMs ?? 60000
  const deadline = Date.now() + timeoutMs

  while (true) {
    const task = await tasksApi.get<T>(taskId)
    options.onProgress?.(task)

    if (task.status === 'completed') {
      return task.result as T
    }
    if (task.status === 'failed') {
      throw new Error(task.error_message || task.message || '后台任务执行失败')
    }
    if (Date.now() >= deadline) {
      throw new Error('任务处理超时，请稍后在任务列表中查看结果')
    }

    await new Promise((resolve) => setTimeout(resolve, intervalMs))
  }
}

export async function resolveMaybeAsync<T>(
  response: T | AsyncTaskSubmission,
  options?: PollTaskOptions,
): Promise<T> {
  if (isAsyncTaskSubmission(response)) {
    return pollTask<T>(response.task_id, options)
  }
  return response
}
