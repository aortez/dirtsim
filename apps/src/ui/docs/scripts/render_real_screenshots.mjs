import path from "node:path";
import fs from "node:fs/promises";
import { spawn } from "node:child_process";
import { screens } from "./real_screens.mjs";

const rootDir = process.cwd();
const outputDir = path.join(rootDir, "screenshots", "real");

const sshHost = process.env.DIRTSIM_SSH_HOST ?? "dirtsim3.local";
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

const runRemoteCli = async (args, { allowFailure = false } = {}) => {
  const command = ["dirtsim-cli", ...args].map(quoteForShell).join(" ");
  await new Promise((resolve, reject) => {
    const child = spawn("ssh", [sshTarget, command], { stdio: "inherit" });
    child.on("close", (code) => {
      if (code === 0 || allowFailure) {
        resolve();
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });
};

const runRemoteCapture = async (args, { allowFailure = false } = {}) =>
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
      if (code === 0 || allowFailure) {
        resolve({ output, errorOutput, code });
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });

const runSsh = async (command, { allowFailure = false } = {}) => {
  await new Promise((resolve, reject) => {
    const child = spawn("ssh", [sshTarget, command], { stdio: "inherit" });
    child.on("close", (code) => {
      if (code === 0 || allowFailure) {
        resolve();
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });
};

const runCli = async (args, options) => runRemoteCli(args, options);

const runCliCapture = async (args, options) => runRemoteCapture(args, options);

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
    await sleep(400);
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
    await sleep(400);
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
    await sleep(400);
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

const runCliStep = async (step, screenId) => {
  if (!step.args || step.args.length === 0) {
    throw new Error(`Missing args for step in ${screenId}`);
  }

  const { output, errorOutput, code } = await runCliCapture(step.args, { allowFailure: true });
  const commandError = extractCommandError(output) || extractCommandError(errorOutput);
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

  if (!isIconSelectCommand(step.args)) {
    if (code !== 0 && !step.allowFailure) {
      throw new Error(
        `Command failed (${code}) during ${screenId}` +
          (commandError ? `: ${commandError}` : "")
      );
    }
    if (commandError && !step.allowFailure) {
      throw new Error(`Command error during ${screenId}: ${commandError}`);
    }
    if (commandError && step.allowFailure) {
      console.error(`CLI warning (${screenId}): ${commandError}`);
    }
    return;
  }

  if (commandError && commandError.includes("IconRail unavailable")) {
    await logStatusSnapshot(`${screenId} iconrail-unavailable`);
    throw new Error(`IconRail unavailable during ${screenId}: ${commandError}`);
  }
  if (code !== 0 && !step.allowFailure) {
    await logStatusSnapshot(`${screenId} iconselect-exit-${code}`);
    throw new Error(
      `IconSelect failed (${code}) during ${screenId}` +
        (commandError ? `: ${commandError}` : "")
    );
  }
  if (commandError && !step.allowFailure) {
    await logStatusSnapshot(`${screenId} iconselect-error`);
    throw new Error(`IconSelect error during ${screenId}: ${commandError}`);
  }
};

const clearTrainingState = async () => {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    const serverState = await getServerState();
    if (serverState === "UnsavedTrainingResult") {
      await runCli(["server", "TrainingResultDiscard"], { allowFailure: true });
    }
    const state = await getUiState();
    if (!state) {
      return;
    }
    if (state === "UnsavedTrainingResult") {
      await runCli(["ui", "TrainingResultDiscard"], { allowFailure: true });
      await sleep(700);
      continue;
    }
    if (state === "Training") {
      await runCli(["ui", "TrainingResultDiscard"], { allowFailure: true });
      await runCli(["ui", "SimStop"], { allowFailure: true });
      await sleep(700);
      continue;
    }
    return;
  }
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

  for (const screen of selectedScreens) {
    if (screen.resetUi) {
      await runCli(["os-manager", "StartServer"], { allowFailure: true });
      await runCli(["os-manager", "RestartUi"], { allowFailure: true });
      await runCli(["os-manager", "StartUi"], { allowFailure: true });
      await sleep(screen.resetWaitMs ?? resetWaitMs);
      if (!screen.skipClearTraining) {
        await clearTrainingState();
      }
      await waitForUiConnected();
      await requireUiState(["StartMenu", "SimRunning", "Paused", "Training"], 8000, "after reset");
    }

    const steps = screen.steps ?? [];
    for (const step of steps) {
      if (step.waitForState && (!step.args || step.args.length === 0)) {
        await requireUiState(
          Array.isArray(step.waitForState) ? step.waitForState : [step.waitForState],
          step.waitTimeoutMs,
          `${screen.id} step waitForState`
        );
        continue;
      }
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
          {
            args: ["ui", "IconSelect", JSON.stringify({ id: screen.expect.selectedIcon })],
            allowFailure: true
          },
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
      await runCli(["ui", "MouseMove", "{\"pixelX\":15,\"pixelY\":80}"], {
        allowFailure: true
      });
      await runCli(["ui", "MouseDown", "{\"pixelX\":15,\"pixelY\":80}"], {
        allowFailure: true
      });
      await runCli(["ui", "MouseUp", "{\"pixelX\":15,\"pixelY\":80}"], {
        allowFailure: true
      });
      await runCli(["ui", "MouseMove", "{\"pixelX\":20,\"pixelY\":20}"], {
        allowFailure: true
      });
      await runCli(["ui", "MouseMove", "{\"pixelX\":60,\"pixelY\":60}"], {
        allowFailure: true
      });
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
      await sleep(800);
      if (activityEnabled) {
        await runCli(["ui", "MouseMove", "{\"pixelX\":20,\"pixelY\":20}"], {
          allowFailure: true
        });
      }
    }
    await runSsh(`rm -f ${quoteForShell(remotePath)}`, {
      allowFailure: true
    });

    if (screen.afterMs) {
      await sleep(screen.afterMs);
    }
  }
};

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
