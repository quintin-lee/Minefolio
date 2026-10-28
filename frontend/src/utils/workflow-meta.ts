/**
 * @file 工作流前端共享常量
 * @description 无需配置参数即可直接执行的工作流 ID 集合 —— 单一来源。
 *              消费方：Chat.vue（胶囊点击）、WorkflowBar.vue（侧栏）、
 *              WorkflowSlashMenu.vue（斜杠菜单）。修改直跑集合时只改这里。
 */

/** 免参数直跑的工作流 ID（其余工作流需先填写配置卡）。字符串键静态查找表。 */
export const DIRECT_RUN_WORKFLOWS: Record<string, true> = {
  wf_portfolio_rebalance: true,
  wf_goal_tracker: true,
  wf_bill_calendar: true,
  wf_health_score: true,
}

/** 判断工作流是否可免参数直跑 */
export function isDirectRunWorkflow(id: string): boolean {
  return DIRECT_RUN_WORKFLOWS[id] === true
}
