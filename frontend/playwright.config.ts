import { defineConfig, devices } from '@playwright/test'

export default defineConfig({
  testDir: './e2e',
  // e2e-test.spec.ts drives the real setup/login flow against a live backend;
  // keep it as a manual suite until CI provisions one.
  testIgnore: 'e2e-test.spec.ts',
  timeout: 60_000,
  use: {
    baseURL: 'http://localhost:5173',
    trace: 'retain-on-failure',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: {
    command: 'npm run dev:mobile -- --port 5173 --strictPort',
    url: 'http://localhost:5173',
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
})
