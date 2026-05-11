/* ============================================================================
 * page.steps.ts — Gherkin step definitions for the qforge web demo
 *
 * Conventions:
 *  • Accessible locators only (getByRole / getByLabel / getByText).
 *  • Never reference internal DOM classes except for xterm's `.xterm-rows`,
 *    which is the documented way to read its rendered buffer.
 *  • Step phrases are reusable across feature files — if two scenarios click
 *    the same button, they share the step.
 * ============================================================================ */

import { expect, type Page, type Locator } from '@playwright/test';
import { createBdd } from 'playwright-bdd';

const { Given, When, Then } = createBdd();

/* ---- Helpers ------------------------------------------------------------ */

/* xterm renders each line into a <div> inside .xterm-rows. Reading the whole
 * region's text content gives us a stable substring search target. */
const terminalContent = (page: Page): Locator => page.locator('.terminal .xterm-rows');

/* Scope to the runner panel — both the asciinema tabs and the run buttons
 * contain demo names ("XOR (smoke test)" tab vs. "▶ XOR (~<1s)" run button),
 * so an unscoped getByRole call would ambiguously hit the tab. */
const runnerPanel = (page: Page): Locator => page.locator('.runner-controls');

const runButton = (page: Page, demo: string): Locator =>
  runnerPanel(page).getByRole('button', { name: new RegExp(demo, 'i') });

/* Find a tweak-panel input by visible label and demo section heading. */
async function tweakInput(page: Page, demoHeading: string, labelText: string): Promise<Locator> {
  const section = page.locator('.tweak-demo').filter({
    has: page.getByRole('heading', { level: 4, name: new RegExp(demoHeading, 'i') }),
  });
  return section.getByRole('spinbutton').and(
    section.locator(`label:has-text("${labelText}") input`)
  );
}

/* ---- Background --------------------------------------------------------- */

Given('I am on the qforge home page', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'qforge', level: 1 })).toBeVisible();
});

Given('I open the tweak hyperparameters panel', async ({ page }) => {
  /* <details>/<summary> — click summary to expand, then assert open state */
  const summary = page.locator('#tweak-panel summary');
  const panel = page.locator('#tweak-panel');
  if (await panel.evaluate((el) => !(el as HTMLDetailsElement).open)) {
    await summary.click();
  }
  await expect(panel).toHaveAttribute('open', '');
});

/* ---- Visibility / smoke ------------------------------------------------- */

Then('I see the heading {string}', async ({ page }, text: string) => {
  await expect(page.getByRole('heading', { name: text }).first()).toBeVisible();
});

Then('I see a link {string}', async ({ page }, text: string) => {
  await expect(page.getByRole('link', { name: text }).first()).toBeVisible();
});

Then('I see {string}', async ({ page }, text: string) => {
  await expect(page.getByText(text).first()).toBeVisible();
});

/* ---- Run buttons + terminal --------------------------------------------- */

When('I click the {string} run button', async ({ page }, demo: string) => {
  await runButton(page, demo).click();
});

When('I wait for the terminal to show {string}', async ({ page }, text: string) => {
  await expect(terminalContent(page)).toContainText(text, { timeout: 30_000 });
});

When('I click the stop button', async ({ page }) => {
  await runnerPanel(page).getByRole('button', { name: /stop/i }).click();
});

Then('the terminal eventually shows {string}', async ({ page }, text: string) => {
  await expect(terminalContent(page)).toContainText(text, { timeout: 45_000 });
});

Then('the terminal contains a prediction row for {string}', async ({ page }, input: string) => {
  /* Each XOR prediction line has the input pattern e.g. "[0, 1]" followed by
   * a probability between 0 and 1. The whole row appears on one line of the
   * xterm DOM, so a substring match is enough. */
  await expect(terminalContent(page)).toContainText(input, { timeout: 30_000 });
});

/* ---- Tweak panel -------------------------------------------------------- */

When(
  'I set the {string} input under {string} to {string}',
  async ({ page }, labelText: string, demoHeading: string, value: string) => {
    const input = await tweakInput(page, demoHeading, labelText);
    await input.fill(value);
    await expect(input).toHaveValue(value);
  }
);

When('I click {string}', async ({ page }, name: string) => {
  await page.getByRole('button', { name }).click();
});

Then(
  'the {string} input under {string} has value {string}',
  async ({ page }, labelText: string, demoHeading: string, expected: string) => {
    const input = await tweakInput(page, demoHeading, labelText);
    await expect(input).toHaveValue(expected);
  }
);
