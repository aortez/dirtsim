#!/usr/bin/env node
/**
 * YOLO remote update - push image over network and flash via A/B update.
 *
 * This is the "hold my mead" approach: we scp the rootfs image to the Pi,
 * verify the checksum, then flash it to the inactive partition.
 * If it works, great! If not, the previous slot still works.
 *
 * No local sudo required! All privileged operations happen on the Pi.
 *
 * Usage:
 *   npm run yolo                              # Build + push + flash + reboot
 *   npm run yolo -- --target 192.168.1.50    # Target a specific host
 *   npm run yolo -- --clean                   # Force rebuild (cleans image sstate)
 *   npm run yolo -- --clean-all               # Force full rebuild (cleans server + image)
 *   npm run yolo -- --skip-build              # Push existing image (skip kas build)
 *   npm run yolo -- --dry-run                 # Show what would happen
 *   npm run yolo -- --help                    # Show help
 */

import { execSync } from 'child_process';
import { existsSync, statSync, readdirSync, readFileSync, writeFileSync, unlinkSync } from 'fs';
import { join, dirname, basename } from 'path';
import { fileURLToPath } from 'url';
import { tmpdir } from 'os';
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
  skull,
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

// Remote target configuration (defaults).
const DEFAULT_HOST = 'dirtsim.local';
const REMOTE_USER = 'dirtsim';
const REMOTE_DEVICE = '/dev/sda';
const REMOTE_TMP = '/tmp';

// Set up consola with timestamp reporter.
const timestampReporter = setupConsolaLogging();
const consola = createConsola({ reporters: [timestampReporter] });

// Set up cleanup manager for signal handling.
const cleanup = createCleanupManager();
cleanup.installSignalHandlers();

// ============================================================================
// Build Phase (Project-Specific)
// ============================================================================

/**
 * Clean the image sstate to force a rebuild.
 */
async function cleanImage() {
  info('Cleaning dirtsim-image sstate to force rebuild...');
  await run('kas', ['shell', 'kas-dirtsim.yml', '-c', 'bitbake -c cleansstate dirtsim-image'], { cwd: YOCTO_DIR });
  success('Clean complete!');
}

/**
 * Clean both server and image sstate for a full rebuild.
 */
async function cleanAll() {
  info('Cleaning dirtsim-server and dirtsim-image sstate...');
  await run('kas', ['shell', 'kas-dirtsim.yml', '-c', 'bitbake -c cleansstate dirtsim-server dirtsim-image'], { cwd: YOCTO_DIR });
  success('Clean complete!');
}

/**
 * Run the Yocto build.
 */
async function build(forceClean = false, forceCleanAll = false) {
  banner('Building dirtsim-image...', consola);

  if (forceCleanAll) {
    await cleanAll();
  } else if (forceClean) {
    await cleanImage();
  }

  await run('kas', ['build', 'kas-dirtsim.yml'], { cwd: YOCTO_DIR });
  success('Build complete!');
}

// ============================================================================
// Image Discovery (Project-Specific)
// ============================================================================

/**
 * Find the latest .ext4.gz rootfs image file.
 * This is the standalone rootfs that can be flashed without local sudo.
 */
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
// Main Entry Point
// ============================================================================

async function main() {
  const args = process.argv.slice(2);

  const skipBuild = args.includes('--skip-build');
  const forceClean = args.includes('--clean');
  const forceCleanAll = args.includes('--clean-all');
  const dryRun = args.includes('--dry-run');
  const holdMyMead = args.includes('--hold-my-mead');

  // Parse --target <hostname> argument.
  const targetIdx = args.indexOf('--target');
  const remoteHost = (targetIdx !== -1 && args[targetIdx + 1]) ? args[targetIdx + 1] : DEFAULT_HOST;
  const remoteTarget = `${REMOTE_USER}@${remoteHost}`;

  if (args.includes('-h') || args.includes('--help')) {
    log('Usage: npm run yolo [options]');
    log('');
    log('Push a Yocto image to the Pi over the network and flash it live.');
    log('No local sudo required - all privileged operations happen on the Pi.');
    log('');
    log('Options:');
    log('  --target <host>  Target hostname or IP (default: dirtsim.local)');
    log('  --skip-build     Skip kas build, use existing image');
    log('  --clean          Force rebuild by cleaning image sstate first');
    log('  --clean-all      Force full rebuild (cleans server + image sstate)');
    log('  --dry-run        Show what would happen without doing it');
    log('  --hold-my-mead   Skip confirmation prompt (for scripts)');
    log('  -h, --help       Show this help');
    log('');
    log('This is the YOLO approach - if it fails, the previous slot still works.');
    process.exit(0);
  }

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck YOLO Update${colors.reset}`);
  log(`${colors.dim}(no local sudo required)${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  skull();

  // Pre-flight checks.
  if (!checkRemoteReachable(remoteHost, remoteTarget)) {
    error(`Cannot reach ${remoteHost}`);
    error('Make sure the Pi is running and accessible via SSH.');
    process.exit(1);
  }
  success(`${remoteHost} is reachable`);

  // Detect boot device.
  const bootDevice = getRemoteBootDevice(remoteTarget, REMOTE_DEVICE);
  info(`Boot device: ${bootDevice}`);

  // Build phase.
  if (!skipBuild) {
    await build(forceClean, forceCleanAll);
  }

  // Find rootfs image (ext4.gz - no extraction needed).
  const rootfs = findLatestRootfs();
  if (!rootfs) {
    error('No rootfs image found (*.ext4.gz).');
    error('Make sure IMAGE_FSTYPES includes "ext4.gz" and run "kas build kas-dirtsim.yml".');
    process.exit(1);
  }

  log('');
  info(`Rootfs: ${rootfs.name}`);
  info(`Size: ${formatBytes(rootfs.stat.size)}`);
  info(`Built: ${rootfs.stat.mtime.toLocaleString()}`);

  // Load SSH key config for remote injection.
  const config = loadConfig(CONFIG_FILE);
  let sshKeyPath = null;

  if (!config) {
    warn('No SSH key configured. Run "npm run flash -- --reconfigure" first.');
    warn('Image will be flashed without SSH key - you may be locked out!');
    // Skip prompt in dry-run mode.
    if (!dryRun) {
      const { prompt } = await import('../pi-base/scripts/lib/cli-utils.mjs');
      const proceed = await prompt('Continue anyway? (y/N): ');
      if (proceed.toLowerCase() !== 'y') {
        error('Aborted.');
        process.exit(1);
      }
    }
  } else {
    sshKeyPath = config.ssh_key_path;
    info(`SSH key: ${basename(sshKeyPath)}`);
  }

  // Check remote has enough space.
  const remoteSpace = getRemoteTmpSpace(remoteTarget, REMOTE_TMP);
  if (remoteSpace < rootfs.stat.size) {
    error(`Not enough space in ${REMOTE_TMP} on ${remoteHost}`);
    error(`Need: ${formatBytes(rootfs.stat.size)}, Have: ${formatBytes(remoteSpace)}`);
    process.exit(1);
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

  // Verify (skip in dry-run since we didn't actually transfer).
  if (!dryRun) {
    if (!verifyRemoteChecksum(remoteImagePath, remoteChecksumPath, remoteTarget)) {
      error('Transfer corrupted! Aborting.');
      process.exit(1);
    }
  }

  // Transfer SSH key if configured.
  let remoteKeyPath = null;
  if (sshKeyPath && !dryRun) {
    info('Transferring SSH key...');
    remoteKeyPath = `${REMOTE_TMP}/authorized_keys`;
    execSync(`scp -o BatchMode=yes "${sshKeyPath}" "${remoteTarget}:${remoteKeyPath}"`, { stdio: 'pipe' });
    success('SSH key transferred');
  }

  // Check if ab-update-with-key exists on remote, transfer if needed.
  // This handles the bootstrap case where the Pi doesn't have the new script yet.
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

  // Get boot time before flash for reboot verification.
  const originalBootTime = getRemoteBootTime(remoteTarget);

  // Flash with key injection on the Pi (no local sudo needed!).
  banner('Flashing image on Pi...', consola);
  cleanup.enterCriticalSection();
  await remoteFlashWithKey(remoteImagePath, remoteKeyPath, REMOTE_USER, remoteTarget, dryRun, holdMyMead, remoteUpdateScript);

  if (!dryRun) {
    // Wait for reboot.
    banner('Waiting for Pi to reboot...', consola);
    const online = await waitForReboot(remoteTarget, remoteHost, originalBootTime, 120);

    // Exiting critical section - Ctrl+C re-enabled.
    cleanup.exitCriticalSection();

    log('');
    if (online) {
      log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
      success('YOLO update complete!');
      info(`Connect with: ssh ${remoteTarget}`);
      log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    } else {
      log(`${colors.bold}${colors.yellow}════════════════════════════════════════════════════════════════${colors.reset}`);
      warn('Pi did not come back online within timeout.');
      warn('It may still be booting, or you may need to reflash.');
      log(`${colors.bold}${colors.yellow}════════════════════════════════════════════════════════════════${colors.reset}`);
    }
  } else {
    log('');
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    success('Dry run complete!');
    info('Run without --dry-run to actually flash.');
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
  }

  log('');
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
