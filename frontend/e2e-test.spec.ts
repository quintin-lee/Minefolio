/**
 * Minefolio Frontend E2E Test Suite
 * Run: npx playwright test frontend-test.spec.ts --headed
 */
import { test, expect } from '@playwright/test';

const BASE = 'http://localhost:5173';

async function setupSystem(page) {
  await page.goto(`${BASE}/setup`);
  await expect(page).toHaveTitle(/Minefolio/);
  
  await page.fill('input[placeholder*="用户名"]', 'testuser');
  await page.fill('input[placeholder*="密码"]', 'testpass123');
  await page.fill('input[placeholder*="再次输入密码"]', 'testpass123');
  await page.click('button:has-text("完成初始化")');
  
  // Wait for redirect to login/dashboard
  await page.waitForURL(/\/login|\/dashboard/);
}

async function login(page) {
  await page.goto(`${BASE}/login`);
  await page.fill('input[placeholder*="用户名"]', 'testuser');
  await page.fill('input[placeholder*="密码"]', 'testpass123');
  await page.click('button:has-text("登录系统")');
  await page.waitForURL(/\/dashboard/);
}

test.describe('Minefolio Frontend E2E Tests', () => {
  test.beforeAll(async ({ browser }) => {
    const context = await browser.newContext();
    const page = await context.newPage();
    await setupSystem(page);
    await login(page);
    await context.close();
  });

  test('Dashboard loads', async ({ page }) => {
    await page.goto(`${BASE}/dashboard`);
    await expect(page).toHaveTitle(/Minefolio/);
    
    // Check key elements exist
    await expect(page.locator('.page-header')).toBeVisible();
    await expect(page.locator('text=仪表盘')).toBeVisible();
  });

  test('Assets page loads and displays list', async ({ page }) => {
    await page.goto(`${BASE}/assets`);
    await expect(page.locator('text=资产列表')).toBeVisible();
    
    // Should have add button
    await expect(page.locator('button:has-text("新增")')).toBeVisible();
  });

  test('Transactions page loads', async ({ page }) => {
    await page.goto(`${BASE}/transactions`);
    await expect(page.locator('text=交易记录')).toBeVisible();
    await expect(page.locator('button:has-text("新增")')).toBeVisible();
  });

  test('Daily Expenses page loads', async ({ page }) => {
    await page.goto(`${BASE}/daily-expenses`);
    await expect(page.locator('text=日常收支')).toBeVisible();
  });

  test('Categories page loads', async ({ page }) => {
    await page.goto(`${BASE}/categories`);
    await expect(page.locator('text=分类管理')).toBeVisible();
  });

  test('Holdings page loads', async ({ page }) => {
    await page.goto(`${BASE}/holdings`);
    await expect(page.locator('text=持仓')).toBeVisible();
  });

  test('Reports page loads', async ({ page }) => {
    await page.goto(`${BASE}/reports`);
    await expect(page.locator('text=报表')).toBeVisible();
  });

  test('Settings page loads', async ({ page }) => {
    await page.goto(`${BASE}/settings`);
    await expect(page.locator('text=设置')).toBeVisible();
  });

  test('Audit Logs page loads', async ({ page }) => {
    await page.goto(`${BASE}/audit-logs`);
    await expect(page.locator('text=审计日志')).toBeVisible();
  });
});
