import { ref, onBeforeUnmount, getCurrentInstance } from 'vue'
import { TaskCancelledError, type AsyncTask, type PollTaskOptions } from '@/api/tasks'

/**
 * 为视图/弹窗提供可取消的异步任务实时进度跟踪状态，并在组件卸载时自动中止轮询
 */
export function useAsyncTaskProgress() {
  const running = ref(false)
  const progress = ref(0)
  const message = ref('')
  let activeController: AbortController | null = null

  function cancel() {
    if (activeController) {
      activeController.abort()
      activeController = null
    }
    running.value = false
  }

  async function run<T>(
    executor: (opts: Pick<PollTaskOptions, 'onProgress' | 'signal'>) => Promise<T>,
    initialMessage = '正在提交任务...'
  ): Promise<T> {
    cancel()
    const controller = new AbortController()
    activeController = controller
    running.value = true
    progress.value = 0
    message.value = initialMessage

    try {
      return await executor({
        signal: controller.signal,
        onProgress: (task: AsyncTask<unknown>) => {
          const pct = Number(task.progress)
          if (Number.isFinite(pct)) {
            progress.value = Math.max(0, Math.min(100, Math.round(pct)))
          }
          if (task.message) {
            message.value = task.message
          }
        },
      })
    } finally {
      if (activeController === controller) {
        activeController = null
        running.value = false
      }
    }
  }

  if (getCurrentInstance()) {
    onBeforeUnmount(() => {
      cancel()
    })
  }

  return {
    running,
    progress,
    message,
    run,
    cancel,
    isCancelledError: (err: unknown) => err instanceof TaskCancelledError,
  }
}
