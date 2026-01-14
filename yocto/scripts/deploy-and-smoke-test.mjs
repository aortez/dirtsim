#!/usr/bin/env node
/**
 * Deploy and Smoke Test - Full yolo deploy followed by StatusGet verification.
 *
 * This script:
 * 1. Builds and deploys a full rootfs image to the Pi (yolo style)
 * 2. Waits for the Pi to reboot and come back online
 * 3. Runs StatusGet on both server and UI via SSH
 * 4. Verifies both services are healthy
 *
 * Usage:
 *   npm run smoke-test                           # Full deploy + verify
 *   npm run smoke-test -- --target dirtsim2.local  # Different host
 *   npm run smoke-test -- --skip-deploy          # Just run verification
 *   npm run smoke-test -- --help                 # Show help
 */

import { execSync } from 'child_process';
import { existsSync, statSync, readdirSync, readFileSync } from 'fs';
import { join, dirname, basename } from 'path';
import { fileURLToPath } from 'url';
import { createConsola } from 'consola';

// Shared utilities from pi-base.
import {
  colors,
  log,
  info,
  success,
  warn,
  error,
  formatBytes,
  setupConsolaLogging,
  banner,
  loadConfig,
  run,
  createCleanupManager,
  checkRemoteReachable,
  getRemoteTmpSpace,
  getRemoteBootDevice,
  getRemoteBootTime,
  waitForReboot,
  transferImage,
  verifyRemoteChecksum,
  calculateChecksum,
  remoteFlashWithKey,
} from '../pi-base/scripts/lib/index.mjs';

// Path setup.
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const IMAGE_DIR = join(YOCTO_DIR, 'build/tmp/deploy/images/raspberrypi-dirtsim');
const CONFIG_FILE = join(YOCTO_DIR, '.flash-config.json');

// Remote target configuration.
const DEFAULT_HOST = 'dirtsim.local';
const REMOTE_USER = 'dirtsim';
const REMOTE_TMP = '/tmp';

// Set up logging.
const timestampReporter = setupConsolaLogging();
const consola = createConsola({ reporters: [timestampReporter] });

// Set up cleanup manager.
const cleanup = createCleanupManager();
cleanup.installSignalHandlers();

// ============================================================================
// Build Phase (copied from yolo-update.mjs)
// ============================================================================

async function build() {
  banner('Building dirtsim-image...', consola);
  await run('kas', ['build', 'kas-dirtsim.yml'], { cwd: YOCTO_DIR });
  success('Build complete!');
}

// ============================================================================
// Image Discovery (copied from yolo-update.mjs)
// ============================================================================

function findLatestRootfs() {
  if (!existsSync(IMAGE_DIR)) {
    return null;
  }

  const files = readdirSync(IMAGE_DIR)
    .filter(f => f.endsWith('.ext4.gz') && !f.includes('->'))
    .map(f => ({
      name: f,
      path: join(IMAGE_DIR, f),
      stat: statSync(join(IMAGE_DIR, f)),
    }))
    .sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);

  return files[0] || null;
}

// ============================================================================
// Deploy Phase (adapted from yolo-update.mjs)
// ============================================================================

async function deploy(remoteHost, remoteTarget, dryRun) {
  // Pre-flight checks.
  if (!checkRemoteReachable(remoteHost, remoteTarget)) {
    error(`Cannot reach ${remoteHost}`);
    error('Make sure the Pi is running and accessible via SSH.');
    return false;
  }
  success(`${remoteHost} is reachable`);

  // Detect boot device.
  const bootDevice = getRemoteBootDevice(remoteTarget, '/dev/sda');
  info(`Boot device: ${bootDevice}`);

  // Build.
  await build();

  // Find rootfs image.
  const rootfs = findLatestRootfs();
  if (!rootfs) {
    error('No rootfs image found (*.ext4.gz).');
    error('Make sure IMAGE_FSTYPES includes "ext4.gz" and run "kas build kas-dirtsim.yml".');
    return false;
  }

  log('');
  info(`Rootfs: ${rootfs.name}`);
  info(`Size: ${formatBytes(rootfs.stat.size)}`);
  info(`Built: ${rootfs.stat.mtime.toLocaleString()}`);

  // Load SSH key config.
  const config = loadConfig(CONFIG_FILE);
  let sshKeyPath = null;

  if (!config) {
    warn('No SSH key configured. Run "npm run flash -- --reconfigure" first.');
    warn('Image will be flashed without SSH key - you may be locked out!');
    return false;
  } else {
    sshKeyPath = config.ssh_key_path;
    info(`SSH key: ${basename(sshKeyPath)}`);
  }

  // Check remote has enough space.
  const remoteSpace = getRemoteTmpSpace(remoteTarget, REMOTE_TMP);
  if (remoteSpace < rootfs.stat.size) {
    error(`Not enough space in ${REMOTE_TMP} on ${remoteHost}`);
    error(`Need: ${formatBytes(rootfs.stat.size)}, Have: ${formatBytes(remoteSpace)}`);
    return false;
  }
  success(`Remote has enough space (${formatBytes(remoteSpace)} available)`);

  // Calculate checksum.
  info('Calculating checksum...');
  const checksum = await calculateChecksum(rootfs.path);
  success(`Checksum: ${checksum.substring(0, 16)}...`);

  // Transfer rootfs.
  banner('Transferring rootfs to Pi...', consola);
  const { remoteImagePath, remoteChecksumPath } = await transferImage(
    rootfs.path, checksum, remoteTarget, REMOTE_TMP, dryRun
  );

  // Verify transfer.
  if (!dryRun) {
    if (!verifyRemoteChecksum(remoteImagePath, remoteChecksumPath, remoteTarget)) {
      error('Transfer corrupted! Aborting.');
      return false;
    }
  }

  // Transfer SSH key.
  let remoteKeyPath = null;
  if (sshKeyPath && !dryRun) {
    info('Transferring SSH key...');
    remoteKeyPath = `${REMOTE_TMP}/authorized_keys`;
    execSync(`scp -o BatchMode=yes "${sshKeyPath}" "${remoteTarget}:${remoteKeyPath}"`, { stdio: 'pipe' });
    success('SSH key transferred');
  }

  // Check if ab-update-with-key exists on remote.
  let remoteUpdateScript = 'ab-update-with-key';
  if (!dryRun) {
    try {
      execSync(`ssh -o BatchMode=yes ${remoteTarget} "which ab-update-with-key"`, { stdio: 'pipe' });
    } catch {
      info('ab-update-with-key not found on Pi, transferring...');
      const localScript = join(YOCTO_DIR, 'pi-base/yocto/meta-pi-base/recipes-support/ab-boot/files/ab-update-with-key');
      const remoteScriptPath = `${REMOTE_TMP}/ab-update-with-key`;
      execSync(`scp -o BatchMode=yes "${localScript}" "${remoteTarget}:${remoteScriptPath}"`, { stdio: 'pipe' });
      execSync(`ssh -o BatchMode=yes ${remoteTarget} "chmod +x ${remoteScriptPath}"`, { stdio: 'pipe' });
      remoteUpdateScript = remoteScriptPath;
      success('Update script transferred');
    }
  }

  // Get boot time before flash.
  const originalBootTime = getRemoteBootTime(remoteTarget);

  // Flash.
  banner('Flashing image on Pi...', consola);
  cleanup.enterCriticalSection();
  await remoteFlashWithKey(remoteImagePath, remoteKeyPath, REMOTE_USER, remoteTarget, dryRun, true, remoteUpdateScript);

  if (!dryRun) {
    // Wait for reboot.
    banner('Waiting for Pi to reboot...', consola);
    const online = await waitForReboot(remoteTarget, remoteHost, originalBootTime, 120);
    cleanup.exitCriticalSection();

    if (!online) {
      error('Pi did not come back online within timeout.');
      return false;
    }
    success('Pi is back online!');
  }

  return true;
}

// ============================================================================
// Smoke Test Phase
// ============================================================================

/**
 * Run a command on the remote Pi via SSH and return stdout.
 * Stderr (logs) is discarded; only stdout (JSON) is captured.
 */
function sshExec(remoteTarget, command) {
  try {
    const result = execSync(`ssh -o BatchMode=yes -o ConnectTimeout=10 ${remoteTarget} "${command}"`, {
      encoding: 'utf-8',
      stdio: ['pipe', 'pipe', 'inherit'],  // stdout captured, stderr to console.
    });
    return { success: true, output: result.trim() };
  } catch (err) {
    return { success: false, output: err.stderr || err.message };
  }
}

/**
 * Extract JSON object from output that may have log lines before it.
 */
function extractJson(output) {
  const jsonStart = output.indexOf('{');
  if (jsonStart === -1) {
    return null;
  }
  return output.substring(jsonStart);
}

/**
 * Run StatusGet on a service and parse the result.
 */
function runStatusGet(remoteTarget, service) {
  const command = `dirtsim-cli ${service} StatusGet`;

  info(`Running: ${command}`);
  const result = sshExec(remoteTarget, command);

  if (!result.success) {
    error(`Failed to run StatusGet on ${service}: ${result.output}`);
    return null;
  }

  // Extract JSON from output (CLI may have log lines before JSON on stdout).
  const jsonStr = extractJson(result.output);
  if (!jsonStr) {
    error(`No JSON found in ${service} response`);
    error(`Raw output: ${result.output}`);
    return null;
  }

  // Parse JSON response.
  try {
    const json = JSON.parse(jsonStr);
    // Response is wrapped in {"id":N,"value":{...}} - extract value.
    if (json.value !== undefined) {
      return json.value;
    }
    return json;
  } catch (err) {
    error(`Failed to parse StatusGet response from ${service}: ${err.message}`);
    error(`Raw output: ${result.output}`);
    return null;
  }
}

/**
 * Verify server status is healthy.
 */
function verifyServerStatus(status) {
  if (!status) {
    return { success: false, reason: 'No response from server' };
  }

  // Check for error state.
  if (status.state === 'Error') {
    return { success: false, reason: `Server in error state: ${status.error_message || 'unknown'}` };
  }

  // Check state exists and is valid.
  if (!status.state) {
    return { success: false, reason: 'Server status missing state field' };
  }

  return { success: true, reason: `Server state: ${status.state}` };
}

/**
 * Verify UI status is healthy.
 */
function verifyUiStatus(status) {
  if (!status) {
    return { success: false, reason: 'No response from UI' };
  }

  // Check state exists.
  if (!status.state) {
    return { success: false, reason: 'UI status missing state field' };
  }

  // Check connected to server.
  if (!status.connected_to_server) {
    return { success: false, reason: `UI not connected to server (state: ${status.state})` };
  }

  return { success: true, reason: `UI state: ${status.state}, connected: true` };
}

/**
 * Run the smoke test (StatusGet on both services).
 */
async function smokeTest(remoteTarget) {
  banner('Running smoke tests...', consola);

  // Give services a moment to fully initialize after boot.
  info('Waiting 5 seconds for services to stabilize...');
  await new Promise(resolve => setTimeout(resolve, 5000));

  // Test server.
  info('Testing server...');
  const serverStatus = runStatusGet(remoteTarget, 'server');
  const serverResult = verifyServerStatus(serverStatus);

  if (serverResult.success) {
    success(`Server: ${serverResult.reason}`);
  } else {
    error(`Server: ${serverResult.reason}`);
  }

  // Test UI.
  info('Testing UI...');
  const uiStatus = runStatusGet(remoteTarget, 'ui');
  const uiResult = verifyUiStatus(uiStatus);

  if (uiResult.success) {
    success(`UI: ${uiResult.reason}`);
  } else {
    error(`UI: ${uiResult.reason}`);
  }

  // Summary.
  log('');
  if (serverResult.success && uiResult.success) {
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    success('Smoke test PASSED!');
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    return true;
  } else {
    log(`${colors.bold}${colors.red}════════════════════════════════════════════════════════════════${colors.reset}`);
    error('Smoke test FAILED!');
    if (!serverResult.success) error(`  Server: ${serverResult.reason}`);
    if (!uiResult.success) error(`  UI: ${uiResult.reason}`);
    log(`${colors.bold}${colors.red}════════════════════════════════════════════════════════════════${colors.reset}`);
    return false;
  }
}

// ============================================================================
// Main
// ============================================================================

function showHelp() {
  log('Usage: npm run smoke-test [options]');
  log('');
  log('Deploy to a Pi and verify services are healthy via StatusGet.');
  log('');
  log('Options:');
  log('  --target <host>    Target hostname or IP (default: dirtsim.local)');
  log('  --skip-deploy      Skip deployment, just run smoke tests');
  log('  --dry-run          Show what would happen without doing it');
  log('  -h, --help         Show this help');
  log('');
  log('Exit codes:');
  log('  0  All tests passed');
  log('  1  Test failed or error occurred');
}

async function main() {
  const args = process.argv.slice(2);

  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    process.exit(0);
  }

  const skipDeploy = args.includes('--skip-deploy');
  const dryRun = args.includes('--dry-run');

  // Parse --target argument.
  const targetIdx = args.indexOf('--target');
  const remoteHost = (targetIdx !== -1 && args[targetIdx + 1]) ? args[targetIdx + 1] : DEFAULT_HOST;
  const remoteTarget = `${REMOTE_USER}@${remoteHost}`;

  log('');
  log(`${colors.bold}${colors.cyan}DirtSim Deploy and Smoke Test${colors.reset}`);
  log(`${colors.dim}Target: ${remoteHost}${colors.reset}`);
  if (skipDeploy) {
    log(`${colors.dim}(skipping deploy)${colors.reset}`);
  }
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode)${colors.reset}`);
  }
  log('');

  // Deploy phase.
  if (!skipDeploy) {
    const deploySuccess = await deploy(remoteHost, remoteTarget, dryRun);
    if (!deploySuccess) {
      error('Deploy failed!');
      process.exit(1);
    }
  } else {
    // Just verify Pi is reachable.
    if (!checkRemoteReachable(remoteHost, remoteTarget)) {
      error(`Cannot reach ${remoteHost}`);
      process.exit(1);
    }
    success(`${remoteHost} is reachable`);
  }

  // Smoke test phase.
  if (!dryRun) {
    const testSuccess = await smokeTest(remoteTarget);
    process.exit(testSuccess ? 0 : 1);
  } else {
    info('Dry run - skipping smoke tests');
    process.exit(0);
  }
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
