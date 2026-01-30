import path from "node:path";
import fs from "node:fs/promises";
import { spawn } from "node:child_process";
import { screens } from "./real_screens.mjs";

const rootDir = process.cwd();
const outputDir = path.join(rootDir, "screenshots", "real");

const shouldPrintHelp = process.argv.includes("--help") || process.argv.includes("-h");
if (shouldPrintHelp) {
  console.log(`Usage: npm run shots:real

Capture real UI screenshots from a remote dirtsim target.

Environment:
  DIRTSIM_SSH_HOST           SSH host (default: dirtsim2.local)
  DIRTSIM_SSH_USER           SSH user (optional)
  DIRTSIM_REMOTE_TMP         Remote temp dir (default: /tmp/dirtsim-ui-docs)
  DOCS_SCREENSHOT_ONLY       Comma-separated screen ids (e.g. start-menu,training)
  DOCS_SCREENSHOT_ACTIVITY   0/1 to disable/enable activity nudges (default: 1)
  DOCS_SCREENSHOT_RESET_WAIT_MS   Reset wait override (ms)
  DOCS_SCREENSHOT_MIN_BYTES       Minimum screenshot size (bytes)
  DOCS_SCREENSHOT_MAX_RETRIES     Retry count for small screenshots
`);
  process.exit(0);
}

const sshHost = process.env.DIRTSIM_SSH_HOST ?? "dirtsim2.local";
const sshUser = process.env.DIRTSIM_SSH_USER;
const sshTarget = sshUser ? `${sshUser}@${sshHost}` : sshHost;
const remoteTmpDir = process.env.DIRTSIM_REMOTE_TMP ?? "/tmp/dirtsim-ui-docs";

const activityEnabled = (process.env.DOCS_SCREENSHOT_ACTIVITY ?? "1") !== "0";
const resetWaitMs = Number(process.env.DOCS_SCREENSHOT_RESET_WAIT_MS ?? 1500);
const minScreenshotBytes = Number(
  process.env.DOCS_SCREENSHOT_MIN_BYTES ?? 2048
);
const maxScreenshotRetries = Number(
  process.env.DOCS_SCREENSHOT_MAX_RETRIES ?? 2
);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const quoteForShell = (value) => {
  if (value === undefined || value === null) {
    return "''";
  }
  const text = String(value);
  if (/^[A-Za-z0-9_./:-]+$/.test(text)) {
    return text;
  }
  return `'${text.replace(/'/g, `'\"'\"'`)}'`;
};

const runRemoteCli = async (args) => {
  const command = ["dirtsim-cli", ...args].map(quoteForShell).join(" ");
  await new Promise((resolve, reject) => {
    const child = spawn("ssh", [sshTarget, command], { stdio: "inherit" });
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });
};

const runRemoteCapture = async (args) =>
  new Promise((resolve, reject) => {
    const command = ["dirtsim-cli", ...args].map(quoteForShell).join(" ");
    const child = spawn("ssh", [sshTarget, command], {
      stdio: ["ignore", "pipe", "pipe"]
    });
    let output = "";
    let errorOutput = "";
    child.stdout.on("data", (chunk) => {
      output += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      errorOutput += chunk.toString();
    });
    child.on("close", (code) => {
      const result = { output, errorOutput, code };
      if (code === 0) {
        resolve(result);
        return;
      }
      const error = new Error(`SSH command failed (${code}) for: ${command}`);
      error.result = result;
      reject(error);
    });
  });

const runSsh = async (command) => {
  await new Promise((resolve, reject) => {
    const child = spawn("ssh", [sshTarget, command], { stdio: "inherit" });
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });
};

const runCli = async (args) => runRemoteCli(args);

const runCliCapture = async (args) => runRemoteCapture(args);

const parseJsonLine = (text) => {
  const lines = text.split(/\r?\n/).map((line) => line.trim());
  for (const line of lines) {
    if (!line.startsWith("{")) {
      continue;
    }
    try {
      return JSON.parse(line);
    } catch {
      continue;
    }
  }
  return null;
};

const extractCommandError = (output) => {
  const parsed = parseJsonLine(output);
  if (parsed?.error) {
    return String(parsed.error);
  }
  if (parsed?.value?.error) {
    return String(parsed.value.error);
  }
  return null;
};

const extractSelectedFlag = (output) => {
  const parsed = parseJsonLine(output);
  if (parsed?.value?.selected !== undefined) {
    return Boolean(parsed.value.selected);
  }
  if (parsed?.selected !== undefined) {
    return Boolean(parsed.selected);
  }
  return null;
};

const clipText = (text, maxLength = 800) => {
  if (!text) {
    return "";
  }
  const trimmed = text.trim();
  if (trimmed.length <= maxLength) {
    return trimmed;
  }
  return `${trimmed.slice(0, maxLength)}...`;
};

const logStatusSnapshot = async (context) => {
  try {
    const status = await getUiStatus();
    if (status) {
      console.error(`Status snapshot (${context}): ${JSON.stringify(status)}`);
      return;
    }
  } catch {
    // Ignore snapshot failures.
  }
  console.error(`Status snapshot (${context}): unavailable`);
};

const getUiStatus = async () => {
  try {
    const { output } = await runCliCapture(["ui", "StatusGet"]);
    const parsed = parseJsonLine(output);
    return parsed?.value ?? null;
  } catch {
    return null;
  }
};

const getServerStatus = async () => {
  try {
    const { output } = await runCliCapture(["server", "StatusGet"]);
    const parsed = parseJsonLine(output);
    return parsed?.value ?? null;
  } catch {
    return null;
  }
};

const getUiState = async () => {
  try {
    const { output } = await runCliCapture(["ui", "StatusGet"]);
    const parsed = parseJsonLine(output);
    return parsed?.value?.state ?? null;
  } catch {
    return null;
  }
};

const requireUiStateFromStateGet = async (context) => {
  let result;
  let capturedError = null;
  try {
    result = await runCliCapture(["ui", "StateGet"]);
  } catch (error) {
    capturedError = error;
    result = error?.result ?? { output: "", errorOutput: "", code: null };
  }

  const output = result.output ?? "";
  const errorOutput = result.errorOutput ?? "";
  const code = result.code ?? null;
  const commandError = extractCommandError(output) || extractCommandError(errorOutput);
  const stdoutSnippet = clipText(output);
  const stderrSnippet = clipText(errorOutput);
  const label = context ? ` (${context})` : "";

  if (capturedError || code !== 0 || commandError) {
    const detail = commandError ? `: ${commandError}` : "";
    throw new Error(
      `StateGet failed${label} (exit ${code ?? "unknown"})${detail}\n` +
        `stdout: ${stdoutSnippet}\nstderr: ${stderrSnippet}`.trim()
    );
  }

  const parsed = parseJsonLine(output) ?? parseJsonLine(errorOutput);
  const state = parsed?.value?.state ?? parsed?.state ?? null;
  if (!state) {
    throw new Error(
      `StateGet missing state${label}\n` +
        `stdout: ${stdoutSnippet}\nstderr: ${stderrSnippet}`.trim()
    );
  }
  return state;
};

const getServerState = async () => {
  const status = await getServerStatus();
  return status?.state ?? null;
};

const isUiConnected = async () => {
  try {
    const { output } = await runCliCapture(["ui", "StatusGet"]);
    const parsed = parseJsonLine(output);
    return Boolean(parsed?.value?.connected_to_server);
  } catch {
    return false;
  }
};

const waitForUiState = async (targets, timeoutMs = 8000) => {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const state = await getUiState();
    if (state && targets.includes(state)) {
      return state;
    }
    await sleep(100);
  }
  return null;
};

const requireUiState = async (targets, timeoutMs = 8000, label = "") => {
  const state = await waitForUiState(targets, timeoutMs);
  if (!state) {
    const targetLabel = Array.isArray(targets) ? targets.join(", ") : targets;
    const suffix = label ? ` (${label})` : "";
    throw new Error(`Timeout waiting for UI state: ${targetLabel}${suffix}`);
  }
  return state;
};

const waitForUiConnected = async (timeoutMs = 8000) => {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const connected = await isUiConnected();
    if (connected) {
      return true;
    }
    await sleep(100);
  }
  return false;
};

const waitForUiStatus = async (predicate, timeoutMs = 8000) => {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const status = await getUiStatus();
    if (status && predicate(status)) {
      return status;
    }
    await sleep(100);
  }
  return null;
};

const matchesExpectedStatus = (status, expected) => {
  if (!status) {
    return false;
  }
  if (expected.state) {
    const states = Array.isArray(expected.state) ? expected.state : [expected.state];
    if (!states.includes(status.state)) {
      return false;
    }
  }
  if (expected.selectedIcon !== undefined && status.selected_icon !== expected.selectedIcon) {
    return false;
  }
  if (expected.panelVisible !== undefined && status.panel_visible !== expected.panelVisible) {
    return false;
  }
  return true;
};

const isIconSelectCommand = (args) =>
  Array.isArray(args) && args[0] === "ui" && args[1] === "IconSelect";

const isMouseClickCommand = (args) =>
  Array.isArray(args)
  && args[0] === "ui"
  && (args[1] === "MouseDown" || args[1] === "MouseUp");

const ensureIconRailExpanded = async (screenId) => {
  await runCliStep({ args: ["ui", "IconRailShowIcons"] }, screenId);
  await sleep(200);
};

const navigateToScreen = async (step, screenId) => {
  const target = step.target;
  if (target !== "StartMenu" && target !== "Training") {
    throw new Error(`Unsupported NavigateToScreen target (${screenId}): ${target}`);
  }

  const state = await requireUiStateFromStateGet(`${screenId} NavigateToScreen`);
  if (state === target) {
    await ensureIconRailExpanded(screenId);
    return;
  }

  if (target === "StartMenu") {
    if (state === "SimRunning" || state === "Paused") {
      await runCliStep({ args: ["ui", "SimStop"] }, screenId);
      await requireUiState(["StartMenu"], 8000, `${screenId} NavigateToScreen`);
      await ensureIconRailExpanded(screenId);
      return;
    }
    if (state === "Training") {
      await runCliStep({ args: ["ui", "TrainingQuit"] }, screenId);
      await requireUiState(["StartMenu"], 8000, `${screenId} NavigateToScreen`);
      await ensureIconRailExpanded(screenId);
      return;
    }
    throw new Error(`NavigateToScreen(${target}) unsupported from state ${state} (${screenId})`);
  }

  if (target === "Training") {
    if (state === "StartMenu") {
      await ensureIconRailExpanded(screenId);
      await runCliStep({ args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"] }, screenId);
      await requireUiState(["Training"], 8000, `${screenId} NavigateToScreen`);
      await ensureIconRailExpanded(screenId);
      return;
    }
    if (state === "SimRunning" || state === "Paused") {
      await runCliStep({ args: ["ui", "SimStop"] }, screenId);
      await requireUiState(["StartMenu"], 8000, `${screenId} NavigateToScreen`);
      await ensureIconRailExpanded(screenId);
      await runCliStep({ args: ["ui", "IconSelect", "{\"id\":\"EVOLUTION\"}"] }, screenId);
      await requireUiState(["Training"], 8000, `${screenId} NavigateToScreen`);
      await ensureIconRailExpanded(screenId);
      return;
    }
    throw new Error(`NavigateToScreen(${target}) unsupported from state ${state} (${screenId})`);
  }
};

const runCliStep = async (step, screenId) => {
  if (!step.args || step.args.length === 0) {
    throw new Error(`Missing args for step in ${screenId}`);
  }

  const allowDeselected = Boolean(step.allowDeselected);
  if (isMouseClickCommand(step.args)) {
    throw new Error(`MouseDown/MouseUp not allowed in docs DSL (${screenId}).`);
  }

  let output = "";
  let errorOutput = "";
  let code = null;
  let capturedError = null;
  try {
    const result = await runCliCapture(step.args);
    output = result.output;
    errorOutput = result.errorOutput;
    code = result.code;
  } catch (error) {
    capturedError = error;
    const result = error?.result ?? {};
    output = result.output ?? "";
    errorOutput = result.errorOutput ?? "";
    code = result.code ?? null;
  }
  const commandError = extractCommandError(output) || extractCommandError(errorOutput);
  const selectedFlag = extractSelectedFlag(output);
  const stdoutSnippet = clipText(output);
  const stderrSnippet = clipText(errorOutput);
  console.error(
    `CLI step (${screenId}): ${step.args.join(" ")} (exit ${code ?? "unknown"})`
  );
  if (stdoutSnippet) {
    console.error(`CLI stdout (${screenId}): ${stdoutSnippet}`);
  }
  if (stderrSnippet) {
    console.error(`CLI stderr (${screenId}): ${stderrSnippet}`);
  }

  if (capturedError) {
    if (isIconSelectCommand(step.args)) {
      await logStatusSnapshot(`${screenId} iconselect-exit-${code ?? "unknown"}`);
    }
    throw new Error(
      `Command failed (${code ?? "unknown"}) during ${screenId}` +
        (commandError ? `: ${commandError}` : "")
    );
  }

  if (!isIconSelectCommand(step.args)) {
    if (code !== 0) {
      throw new Error(
        `Command failed (${code}) during ${screenId}` +
          (commandError ? `: ${commandError}` : "")
      );
    }
    if (commandError) {
      throw new Error(`Command error during ${screenId}: ${commandError}`);
    }
    return;
  }

  if (commandError && commandError.includes("IconRail unavailable")) {
    await logStatusSnapshot(`${screenId} iconrail-unavailable`);
    throw new Error(`IconRail unavailable during ${screenId}: ${commandError}`);
  }
  if (code !== 0) {
    await logStatusSnapshot(`${screenId} iconselect-exit-${code}`);
    throw new Error(
      `IconSelect failed (${code}) during ${screenId}` +
        (commandError ? `: ${commandError}` : "")
    );
  }
  if (commandError) {
    await logStatusSnapshot(`${screenId} iconselect-error`);
    throw new Error(`IconSelect error during ${screenId}: ${commandError}`);
  }
  if (selectedFlag !== false) {
    return;
  }

  if (!allowDeselected) {
    await logStatusSnapshot(`${screenId} iconselect-unselected`);
    throw new Error(`IconSelect returned selected=false during ${screenId}`);
  }
  return;
};

const clearTrainingState = async () => {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    const serverState = await getServerState();
    if (serverState === "UnsavedTrainingResult") {
      await runCli(["server", "TrainingResultDiscard"]);
    }
    const state = await getUiState();
    if (!state) {
      return;
    }
    if (state === "UnsavedTrainingResult") {
      await runCli(["ui", "TrainingResultDiscard"]);
      await sleep(700);
      continue;
    }
    if (state === "Training") {
      await runCli(["ui", "TrainingResultDiscard"]);
      await runCli(["ui", "SimStop"]);
      await sleep(700);
      continue;
    }
    return;
  }
};

const resetSystem = async () => {
  await runCli(["os-manager", "RestartServer"]);
  await runCli(["os-manager", "RestartUi"]);
  await waitForUiConnected();
  await requireUiState(
    ["StartMenu", "SimRunning", "Paused", "Training"],
    2000,
    "after reset"
  );
};

const run = async () => {
  await fs.mkdir(outputDir, { recursive: true });

  try {
    const state = await getUiState();
    if (!state) {
      throw new Error("UI status not available.");
    }
  } catch (error) {
    throw new Error(
      `UI is not reachable. Ensure dirtsim UI is running on ${sshHost}.`,
      { cause: error }
    );
  }

  const only = process.env.DOCS_SCREENSHOT_ONLY
    ? process.env.DOCS_SCREENSHOT_ONLY.split(",").map((item) => item.trim())
    : null;

  const selectedScreens = only
    ? screens.filter((screen) => only.includes(screen.id))
    : screens;

  if (selectedScreens.length === 0) {
    console.warn("No screens selected. Check DOCS_SCREENSHOT_ONLY or real_screens.mjs.");
    return;
  }

  let activeFlowId = null;
  for (const screen of selectedScreens) {
    console.info(`Screenshot scenario: ${screen.id}`);
    const flowId = screen.flowId ?? screen.id;
    if (flowId !== activeFlowId) {
      if (screen.resetSystem !== false) {
        await resetSystem();
        if (!screen.skipClearTraining) {
          await clearTrainingState();
        }
      }
      activeFlowId = flowId;
    }

    const steps = screen.steps ?? [];
    for (const step of steps) {
      if (step.kind === "NavigateToScreen") {
        await navigateToScreen(step, screen.id);
      }
      else if (step.waitForState && (!step.args || step.args.length === 0)) {
        await requireUiState(
          Array.isArray(step.waitForState) ? step.waitForState : [step.waitForState],
          step.waitTimeoutMs,
          `${screen.id} step waitForState`
        );
        continue;
      }
      else {
        try {
          await runCliStep(step, screen.id);
        } catch (error) {
          if (step.retryOnTrainingResult) {
            await clearTrainingState();
            await requireUiState(
              ["StartMenu", "Paused", "SimRunning"],
              8000,
              `${screen.id} retryOnTrainingResult`
            );
            await runCliStep(step, screen.id);
          } else {
            throw error;
          }
        }
      }
      if (step.waitMs) {
        await sleep(step.waitMs);
      }
      if (step.waitForState) {
        await requireUiState(
          Array.isArray(step.waitForState) ? step.waitForState : [step.waitForState],
          step.waitTimeoutMs,
          `${screen.id} step waitForState`
        );
      }
    }

    if (screen.expect) {
      const expectTimeoutMs = screen.expectTimeoutMs ?? 8000;
      const status = await waitForUiStatus(
        (current) => matchesExpectedStatus(current, screen.expect),
        expectTimeoutMs
      );
      if (!status && screen.expect.selectedIcon) {
        await runCliStep(
          { args: ["ui", "IconSelect", JSON.stringify({ id: screen.expect.selectedIcon })] },
          screen.id
        );
      }
      const finalStatus = await waitForUiStatus(
        (current) => matchesExpectedStatus(current, screen.expect),
        expectTimeoutMs
      );
      if (!finalStatus) {
        throw new Error(
          `UI did not reach expected status for ${screen.id}: ` +
            JSON.stringify(screen.expect)
        );
      }
    }

    const shouldEnableActivity =
      screen.activityEnabled ?? activityEnabled;
    if (shouldEnableActivity) {
      if (!screen.skipClearTraining) {
        await clearTrainingState();
      }
      await runCli(["ui", "MouseMove", "{\"pixelX\":15,\"pixelY\":80}"]);
      await runCli(["ui", "MouseMove", "{\"pixelX\":20,\"pixelY\":20}"]);
      await runCli(["ui", "MouseMove", "{\"pixelX\":60,\"pixelY\":60}"]);
      await sleep(400);
    }

    const outputPath = path.join(outputDir, `${screen.id}.png`);
    await runSsh(`mkdir -p ${quoteForShell(remoteTmpDir)}`);
    const remotePath = path.posix.join(remoteTmpDir, `${screen.id}.png`);
    let attempt = 0;
    while (attempt <= maxScreenshotRetries) {
      await runRemoteCli(["screenshot", remotePath]);
      await new Promise((resolve, reject) => {
        const child = spawn("scp", [`${sshTarget}:${remotePath}`, outputPath], {
          stdio: "inherit"
        });
        child.on("close", (code) => {
          if (code === 0) {
            resolve();
          } else {
            reject(
              new Error(
                `scp failed (${code}) for: ${sshTarget}:${remotePath}`
              )
            );
          }
        });
      });

      const stats = await fs.stat(outputPath);
      if (stats.size >= minScreenshotBytes || attempt >= maxScreenshotRetries) {
        break;
      }
      attempt += 1;
      await sleep(100);
      if (activityEnabled) {
        await runCli(["ui", "MouseMove", "{\"pixelX\":20,\"pixelY\":20}"]);
      }
    }
    await runSsh(`rm -f ${quoteForShell(remotePath)}`);

    if (screen.afterMs) {
      await sleep(screen.afterMs);
    }
  }
};

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
