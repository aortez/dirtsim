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

const runRemoteCapture = async (args) =>
  new Promise((resolve, reject) => {
    const command = ["dirtsim-cli", ...args].map(quoteForShell).join(" ");
    const child = spawn("ssh", [sshTarget, command], {
      stdio: ["ignore", "pipe", "pipe"]
    });
    let output = "";
    child.stdout.on("data", (chunk) => {
      output += chunk.toString();
    });
    child.on("close", (code) => {
      if (code === 0) {
        resolve(output);
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

const getUiState = async () => {
  try {
    const output = await runCliCapture(["ui", "StatusGet"]);
    const parsed = parseJsonLine(output);
    return parsed?.value?.state ?? null;
  } catch {
    return null;
  }
};

const isUiConnected = async () => {
  try {
    const output = await runCliCapture(["ui", "StatusGet"]);
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

const clearTrainingState = async () => {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    await runCli(["server", "TrainingResultDiscard"], { allowFailure: true });
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
      await waitForUiState(["StartMenu", "SimRunning", "Paused", "Training"]);
    }

    const steps = screen.steps ?? [];
    for (const step of steps) {
      if (step.waitForState) {
        await waitForUiState(
          Array.isArray(step.waitForState) ? step.waitForState : [step.waitForState],
          step.waitTimeoutMs
        );
        continue;
      }
      try {
        await runCli(step.args, { allowFailure: step.allowFailure });
      } catch (error) {
        if (step.retryOnTrainingResult) {
          await clearTrainingState();
          await waitForUiState(["StartMenu", "Paused", "SimRunning"]);
          await runCli(step.args, { allowFailure: step.allowFailure });
        } else {
          throw error;
        }
      }
      if (step.waitMs) {
        await sleep(step.waitMs);
      }
      if (step.waitForState) {
        await waitForUiState(
          Array.isArray(step.waitForState) ? step.waitForState : [step.waitForState],
          step.waitTimeoutMs
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
