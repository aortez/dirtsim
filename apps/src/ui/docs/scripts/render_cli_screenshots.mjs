import path from "node:path";
import fs from "node:fs/promises";
import { spawn } from "node:child_process";

const rootDir = process.cwd();
const outputDir = path.join(rootDir, "screenshots", "real");

const shouldPrintHelp = process.argv.includes("--help") || process.argv.includes("-h");
if (shouldPrintHelp) {
  console.log(`Usage: npm run shots:real

Capture UI screenshots via the remote CLI runner and download them locally.

Environment:
  DIRTSIM_SSH_HOST           SSH host (default: dirtsim2.local)
  DIRTSIM_SSH_USER           SSH user (optional)
  DIRTSIM_REMOTE_TMP         Remote temp dir (default: /tmp/dirtsim-ui-docs)
  DIRTSIM_SSH_CONTROL_PATH   SSH control socket path (default: ~/.ssh/cm-%r@%h:%p)
  DOCS_SCREENSHOT_ONLY       Comma-separated screen ids (e.g. start-menu,training)
  DOCS_SCREENSHOT_MIN_BYTES  Minimum screenshot size (bytes)
`);
  process.exit(0);
}

const sshHost = process.env.DIRTSIM_SSH_HOST ?? "dirtsim2.local";
const sshUser = process.env.DIRTSIM_SSH_USER;
const sshTarget = sshUser ? `${sshUser}@${sshHost}` : sshHost;
const remoteTmpDir = process.env.DIRTSIM_REMOTE_TMP ?? "/tmp/dirtsim-ui-docs";
const sshControlPath =
  process.env.DIRTSIM_SSH_CONTROL_PATH
  ?? path.join(process.env.HOME ?? "", ".ssh", "cm-%r@%h:%p");
const sshControlArgs = [
  "-o",
  "ControlMaster=auto",
  "-o",
  "ControlPersist=60s",
  "-o",
  `ControlPath=${sshControlPath}`
];

const quoteForShell = (value) => {
  if (value === undefined || value === null) {
    return "''";
  }
  const text = String(value);
  if (/^[A-Za-z0-9_./:-]+$/.test(text)) {
    return text;
  }
  return `'${text.replace(/'/g, `'"'"'`)}'`;
};

const runSsh = async (command) => {
  await new Promise((resolve, reject) => {
    const child = spawn("ssh", [...sshControlArgs, sshTarget, command], {
      stdio: "inherit"
    });
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`SSH command failed (${code}) for: ${command}`));
      }
    });
  });
};

const runSshCapture = async (command) =>
  new Promise((resolve, reject) => {
    const child = spawn("ssh", [...sshControlArgs, sshTarget, command], {
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
      if (code === 0) {
        resolve({ output, errorOutput });
        return;
      }
      const error = new Error(`SSH command failed (${code}) for: ${command}`);
      error.output = output;
      error.errorOutput = errorOutput;
      reject(error);
    });
  });

const runScp = async (remotePath, localPath) => {
  await new Promise((resolve, reject) => {
    const child = spawn("scp", [...sshControlArgs, remotePath, localPath], {
      stdio: "inherit"
    });
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`scp failed (${code}) for: ${remotePath}`));
      }
    });
  });
};

const run = async () => {
  await fs.mkdir(outputDir, { recursive: true });
  await fs.mkdir(path.dirname(sshControlPath), { recursive: true });

  const existingFiles = await fs.readdir(outputDir).catch(() => []);
  await Promise.all(
    existingFiles
      .filter((file) => file.endsWith(".png"))
      .map((file) => fs.unlink(path.join(outputDir, file)).catch(() => undefined))
  );

  const envParts = [];
  if (process.env.DOCS_SCREENSHOT_ONLY) {
    envParts.push(`DOCS_SCREENSHOT_ONLY=${quoteForShell(process.env.DOCS_SCREENSHOT_ONLY)}`);
  }
  if (process.env.DOCS_SCREENSHOT_MIN_BYTES) {
    envParts.push(
      `DOCS_SCREENSHOT_MIN_BYTES=${quoteForShell(process.env.DOCS_SCREENSHOT_MIN_BYTES)}`
    );
  }

  const remoteCommand = [
    ...envParts,
    "dirtsim-cli",
    "docs-screenshots",
    quoteForShell(remoteTmpDir)
  ].join(" ");

  const remoteTmpQuoted = quoteForShell(remoteTmpDir);
  await runSsh(`mkdir -p ${remoteTmpQuoted} && rm -f ${remoteTmpQuoted}/*.png`);

  console.log(`Running remote CLI capture on ${sshTarget}...`);
  await runSsh(remoteCommand);

  const listCommand = `ls -1 ${quoteForShell(remoteTmpDir)}/*.png`;
  const { output } = await runSshCapture(listCommand);
  const files = output
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((filePath) => path.posix.basename(filePath));

  if (files.length === 0) {
    console.warn(`No screenshots found in ${remoteTmpDir} on ${sshTarget}.`);
    return;
  }

  for (const file of files) {
    const remotePath = `${sshTarget}:${path.posix.join(remoteTmpDir, file)}`;
    const localPath = path.join(outputDir, file);
    await runScp(remotePath, localPath);
    console.log(`Downloaded ${file} -> ${path.relative(rootDir, localPath)}`);
  }
};

run().catch((error) => {
  console.error(error);
  process.exit(1);
});
