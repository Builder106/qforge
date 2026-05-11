/* ============================================================================
 * playwright.config.ts — QA test configuration for the qforge web demo
 *
 * BDD-driven: feature files in e2e/features/ are compiled to playwright
 * specs by playwright-bdd at runtime. Step definitions live in e2e/steps/.
 *
 * The demo is a pure-static site, so the webServer just runs Python's
 * built-in http.server pointed at ../web. CI installs Python by default
 * on every runner, so no extra deps.
 * ============================================================================ */

import { defineConfig, devices } from '@playwright/test';
import { defineBddConfig } from 'playwright-bdd';

const testDir = defineBddConfig({
  features: 'features/**/*.feature',
  steps:    'steps/**/*.ts',
});

const PORT = 4173;

export default defineConfig({
  testDir,
  timeout: 60_000,
  expect: { timeout: 10_000 },
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: process.env.CI ? 2 : undefined,
  reporter: process.env.CI ? [['html'], ['list']] : 'list',

  use: {
    baseURL: `http://localhost:${PORT}`,
    trace: 'retain-on-failure',
    video: 'retain-on-failure',
    screenshot: 'only-on-failure',
    viewport: { width: 1280, height: 800 },
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 800 } },
    },
  ],

  /* Serve ../web as a static site. Reuse an existing server during local
   * development; CI always starts a fresh one. */
  webServer: {
    command: `python3 -m http.server ${PORT} --directory ../web`,
    url: `http://localhost:${PORT}/`,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
