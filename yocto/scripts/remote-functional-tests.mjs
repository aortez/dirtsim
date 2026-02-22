#!/usr/bin/env node
/**
 * Run DirtSim CLI functional tests on a remote device via SSH.
 */

import { spawnSync } from 'child_process';
import { existsSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import {
  checkRemoteReachable,
  colors,
  error,
  info,
  log,
  success,
  warn,
} from '../pi-base/scripts/lib/index.mjs';

const DEFAULT_HOST = 'dirtsim.local';
const DEFAULT_USER = 'dirtsim';
const DEFAULT_TEST_TIMEOUT_MS = 20000;
const DEFAULT_VERIFY_TIMEOUT_MS = 300000;
const DEFAULT_SSH_TIMEOUT_SEC = 5;
const DEFAULT_WAIT_SEC = 180;
const NES_FIXTURE_FILE = 'Flappy.Paratroopa.World.Unl.nes';
const NES_FIXTURE_REMOTE_DIR = '/data/dirtsim/testdata/roms';
const NES_FIXTURE_REMOTE_PATH = `${NES_FIXTURE_REMOTE_DIR}/${NES_FIXTURE_FILE}`;
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const APPS_DIR = join(__dirname, '..', '..', 'apps');
const NES_FIXTURE_LOCAL_PATH = join(APPS_DIR, 'testdata', 'roms', NES_FIXTURE_FILE);
const NES_FETCH_SCRIPT = join(APPS_DIR, 'scripts', 'fetch_nes_test_rom.sh');

const ALL_TESTS = [
  'canExit',
  'canTrain',
  'canSetGenerationsAndTrain',
  'canPlantTreeSeed',
  'canControlNesScenario',
  'canLoadGenomeFromBrowser',
  'canOpenTrainingConfigPanel',
  'verifyTraining',
];

function showHelp() {
  log('Usage: npm run remote-functional-tests -- [options]');
  log('');
  log('Run DirtSim functional tests on a remote unit via SSH.');
  log('');
  log('Options:');
  log(`  --target <host>        Target hostname (default: ${DEFAULT_HOST})`);
  log(`  --user <user>          SSH user (default: ${DEFAULT_USER})`);
  log('  --tests <a,b,c>        Comma-separated test list');
  log('  --all                  Run all functional tests');
  log(`  --timeout <ms>         Timeout per test (default: ${DEFAULT_TEST_TIMEOUT_MS})`);
  log(`  --verify-timeout <ms>  Timeout for verifyTraining (default: ${DEFAULT_VERIFY_TIMEOUT_MS})`);
  log(`  --ssh-timeout <sec>    SSH connect timeout (default: ${DEFAULT_SSH_TIMEOUT_SEC})`);
  log(`  --wait <sec>           Wait for SSH after deploy (default: ${DEFAULT_WAIT_SEC})`);
  log('  -h, --help             Show this help');
  log('');
  log(`Valid tests: ${ALL_TESTS.join(', ')}`);
}

function fail(message) {
  error(message);
  process.exit(1);
}

function parseIntArg(value, name, minValue) {
  const parsed = Number.parseInt(value, 10);
  if (Number.isNaN(parsed) || parsed < minValue) {
    fail(`${name} must be an integer >= ${minValue}`);
  }
  return parsed;
}

function parseTestsArg(value) {
  return value
    .split(',')
    .map(item => item.trim())
    .filter(Boolean);
}

function resolveTests({ tests, useAll }) {
  const selected = tests.length > 0 ? tests : (useAll ? ALL_TESTS : ALL_TESTS);
  const invalid = selected.filter(name => !ALL_TESTS.includes(name));
  if (invalid.length > 0) {
    fail(`Unknown functional test(s): ${invalid.join(', ')}`);
  }
  return selected;
}

function shouldDisableStrictHostKeyChecking() {
  if (process.env.DIRTSIM_SSH_STRICT === 'true') {
    return false;
  }
  if (process.env.DIRTSIM_SSH_STRICT === 'false') {
    return true;
  }
  if (process.env.DIRTSIM_SSH_NO_STRICT === '1') {
    return true;
  }
  return process.env.CI === 'true' || process.env.GITHUB_ACTIONS === 'true';
}

function buildSshArgs(timeoutSec, identityPath) {
  const args = [
    '-o', `ConnectTimeout=${timeoutSec}`,
    '-o', 'BatchMode=yes',
  ];
  if (identityPath) {
    args.push('-i', identityPath);
  }
  if (shouldDisableStrictHostKeyChecking()) {
    args.push('-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null');
  }
  return args;
}

function runSsh(remoteTarget, command, timeoutSec, identityPath) {
  const args = [
    ...buildSshArgs(timeoutSec, identityPath),
    remoteTarget,
    command,
  ];
  const result = spawnSync('ssh', args, { encoding: 'utf-8' });
  const stdout = result.stdout || '';
  const stderr = result.stderr || '';
  const status = typeof result.status === 'number' ? result.status : 1;
  return { status, stdout, stderr, output: `${stdout}${stderr}` };
}

function runScp(localPath, remoteTarget, remotePath, timeoutSec, identityPath) {
  const args = [
    ...buildSshArgs(timeoutSec, identityPath),
    localPath,
    `${remoteTarget}:${remotePath}`,
  ];
  const result = spawnSync('scp', args, { encoding: 'utf-8' });
  const stdout = result.stdout || '';
  const stderr = result.stderr || '';
  const status = typeof result.status === 'number' ? result.status : 1;
  return { status, stdout, stderr, output: `${stdout}${stderr}` };
}

function ensureLocalNesFixture() {
  if (existsSync(NES_FIXTURE_LOCAL_PATH)) {
    return NES_FIXTURE_LOCAL_PATH;
  }

  info(`NES fixture missing locally; running ${NES_FETCH_SCRIPT}`);
  const fetchResult = spawnSync(NES_FETCH_SCRIPT, { encoding: 'utf-8' });
  if (fetchResult.status !== 0) {
    const output = `${fetchResult.stdout || ''}${fetchResult.stderr || ''}`.trim();
    fail(`Failed to fetch NES fixture ROM:\n${output}`);
  }

  if (!existsSync(NES_FIXTURE_LOCAL_PATH)) {
    fail(`NES fixture ROM not found after fetch: ${NES_FIXTURE_LOCAL_PATH}`);
  }

  return NES_FIXTURE_LOCAL_PATH;
}

function ensureRemoteNesFixture(remoteTarget, timeoutSec, identityPath) {
  const fixtureCheck = runSsh(
    remoteTarget,
    `test -f "${NES_FIXTURE_REMOTE_PATH}"`,
    timeoutSec,
    identityPath
  );
  if (fixtureCheck.status === 0) {
    info(`NES fixture ready: ${NES_FIXTURE_REMOTE_PATH}`);
    return;
  }

  const localFixturePath = ensureLocalNesFixture();
  info(`Installing NES fixture on target: ${NES_FIXTURE_REMOTE_PATH}`);

  const mkdirResult = runSsh(
    remoteTarget,
    `sudo mkdir -p "${NES_FIXTURE_REMOTE_DIR}"`,
    timeoutSec,
    identityPath
  );
  if (mkdirResult.status !== 0) {
    fail(`Failed to create remote NES fixture directory:\n${mkdirResult.output.trim()}`);
  }

  const stagedRemotePath = `/tmp/${NES_FIXTURE_FILE}.${Date.now()}.${process.pid}`;
  const scpResult = runScp(
    localFixturePath,
    remoteTarget,
    stagedRemotePath,
    timeoutSec,
    identityPath
  );
  if (scpResult.status !== 0) {
    fail(`Failed to upload NES fixture ROM:\n${scpResult.output.trim()}`);
  }

  const installResult = runSsh(
    remoteTarget,
    `sudo cp "${stagedRemotePath}" "${NES_FIXTURE_REMOTE_PATH}" && ` +
      `sudo chown dirtsim:dirtsim "${NES_FIXTURE_REMOTE_PATH}" && ` +
      `sudo chmod 0644 "${NES_FIXTURE_REMOTE_PATH}" && ` +
      `rm -f "${stagedRemotePath}"`,
    timeoutSec,
    identityPath
  );
  if (installResult.status !== 0) {
    fail(`Failed to install NES fixture ROM:\n${installResult.output.trim()}`);
  }

  success(`NES fixture installed: ${NES_FIXTURE_REMOTE_PATH}`);
}

async function waitForSsh(remoteTarget, timeoutSec, waitSec, identityPath) {
  const startTime = Date.now();
  const timeoutMs = waitSec * 1000;
  while (Date.now() - startTime < timeoutMs) {
    const result = runSsh(remoteTarget, 'echo ok', timeoutSec, identityPath);
    if (result.status === 0 && result.stdout.trim() === 'ok') {
      return true;
    }
    await new Promise(resolve => setTimeout(resolve, 2000));
  }
  return false;
}

function extractJson(output) {
  const lines = output
    .split(/\r?\n/)
    .map(line => line.trim())
    .filter(Boolean);
  for (let i = lines.length - 1; i >= 0; i--) {
    const line = lines[i];
    if (!line.startsWith('{') || !line.endsWith('}')) {
      continue;
    }
    try {
      return JSON.parse(line);
    } catch {
      continue;
    }
  }
  return null;
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    return;
  }

  let tests = [];
  let useAll = false;
  let timeoutMs = Number(process.env.DIRTSIM_FUNCTIONAL_TIMEOUT_MS) || DEFAULT_TEST_TIMEOUT_MS;
  let verifyTimeoutMs =
    Number(process.env.DIRTSIM_VERIFY_TRAINING_TIMEOUT_MS) || DEFAULT_VERIFY_TIMEOUT_MS;
  let remoteHost = process.env.DIRTSIM_REMOTE_HOST || DEFAULT_HOST;
  let remoteUser = process.env.DIRTSIM_REMOTE_USER || DEFAULT_USER;
  let sshTimeoutSec = Number(process.env.DIRTSIM_SSH_TIMEOUT_SEC) || DEFAULT_SSH_TIMEOUT_SEC;
  let waitSec = Number(process.env.DIRTSIM_REMOTE_WAIT_SEC) || DEFAULT_WAIT_SEC;
  const sshIdentityPath = process.env.DIRTSIM_SSH_IDENTITY
    || process.env.DIRTSIM_SSH_PRIVATE_KEY_PATH
    || null;

  if (sshIdentityPath && !sshIdentityPath.trim()) {
    fail('DIRTSIM_SSH_PRIVATE_KEY_PATH is set but empty');
  }

  const uiAddress = process.env.DIRTSIM_UI_ADDRESS || 'ws://localhost:7070';
  const serverAddress = process.env.DIRTSIM_SERVER_ADDRESS || 'ws://localhost:8080';
  const osManagerAddress = process.env.DIRTSIM_OS_MANAGER_ADDRESS || 'ws://localhost:9090';

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    switch (arg) {
      case '--target':
        if (i + 1 >= args.length) {
          fail('--target requires a hostname');
        }
        remoteHost = args[++i];
        break;
      case '--user':
        if (i + 1 >= args.length) {
          fail('--user requires a username');
        }
        remoteUser = args[++i];
        break;
      case '--tests':
        if (i + 1 >= args.length) {
          fail('--tests requires a value');
        }
        tests = tests.concat(parseTestsArg(args[++i]));
        break;
      case '--all':
        useAll = true;
        break;
      case '--timeout':
        if (i + 1 >= args.length) {
          fail('--timeout requires a value');
        }
        timeoutMs = parseIntArg(args[++i], '--timeout', 1);
        break;
      case '--verify-timeout':
        if (i + 1 >= args.length) {
          fail('--verify-timeout requires a value');
        }
        verifyTimeoutMs = parseIntArg(args[++i], '--verify-timeout', 1);
        break;
      case '--ssh-timeout':
        if (i + 1 >= args.length) {
          fail('--ssh-timeout requires a value');
        }
        sshTimeoutSec = parseIntArg(args[++i], '--ssh-timeout', 1);
        break;
      case '--wait':
        if (i + 1 >= args.length) {
          fail('--wait requires a value');
        }
        waitSec = parseIntArg(args[++i], '--wait', 1);
        break;
      default:
        fail(`Unknown argument: ${arg}`);
    }
  }

  const selectedTests = resolveTests({ tests, useAll });
  const remoteTarget = `${remoteUser}@${remoteHost}`;

  log(`${colors.bold}${colors.cyan}DirtSim Remote Functional Tests${colors.reset}`);
  info(`Target: ${remoteTarget}`);

  if (!checkRemoteReachable(remoteHost, remoteTarget)) {
    warn('Initial reachability check failed. Waiting for SSH...');
  }

  const sshReady = await waitForSsh(remoteTarget, sshTimeoutSec, waitSec, sshIdentityPath);
  if (!sshReady) {
    fail(`Timed out waiting for SSH on ${remoteTarget}`);
  }

  if (selectedTests.includes('canControlNesScenario')) {
    ensureRemoteNesFixture(remoteTarget, sshTimeoutSec, sshIdentityPath);
  }

  let testResults = '';
  let failed = false;

  for (const testName of selectedTests) {
    const currentTimeout =
      testName === 'verifyTraining' ? verifyTimeoutMs : timeoutMs;
    info(`Running functional test: ${testName}`);

    const remoteCommand =
      `dirtsim-cli functional-test ${testName} ` +
      `--timeout ${currentTimeout} ` +
      `--ui-address ${uiAddress} ` +
      `--server-address ${serverAddress} ` +
      `--os-manager-address ${osManagerAddress} 2>&1`;

    const { status, output } = runSsh(remoteTarget, remoteCommand, sshTimeoutSec, sshIdentityPath);
    const parsed = extractJson(output);
    const durationMs = parsed?.duration_ms ?? 0;
    const durationSec = (durationMs / 1000).toFixed(1);

    if (status === 0) {
      success(`${testName}: PASSED (${durationSec}s)`);
      testResults += `| ${testName} | ✅ Pass | ${durationSec}s |\n`;
    } else {
      failed = true;
      error(`${testName}: FAILED (${durationSec}s)`);
      if (parsed?.result?.error) {
        warn(`Error: ${parsed.result.error}`);
      } else if (output) {
        warn(output.trim());
      }
      testResults += `| ${testName} | ❌ Fail | ${durationSec}s |\n`;
    }
  }

  log('');
  log('### Remote Functional Tests');
  log('');
  log('| Test | Status | Duration |');
  log('|------|--------|----------|');
  log(testResults.trimEnd());
  log('');

  if (failed) {
    log('remote functional tests FAILED');
    process.exit(1);
  }

  log('remote functional tests PASSED');
}

main().catch(err => {
  fail(err.message);
});
