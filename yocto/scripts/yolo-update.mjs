#!/usr/bin/env node
/**
 * YOLO remote update - push image over network and dd directly to disk.
 *
 * This is the "hold my mead" approach: we scp the image to the Pi,
 * verify the checksum, then dd it to the boot disk while running.
 * If it works, great! If not, pull the disk and reflash.
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
import { existsSync, statSync, readdirSync } from 'fs';
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
  skull,
  loadConfig,
  run,
  createCleanupManager,
  ssh,
  checkRemoteReachable,
  getRemoteTmpSpace,
  getRemoteBootDevice,
  getRemoteBootTime,
  waitForReboot,
  transferImage,
  verifyRemoteChecksum,
  calculateChecksum,
  prepareRootfs,
  cleanupPreparedImage,
  remoteFlash,
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
 * Find the latest .wic.gz image file.
 */
function findLatestImage() {
  if (!existsSync(IMAGE_DIR)) {
    return null;
  }

  const files = readdirSync(IMAGE_DIR)
    .filter(f => f.endsWith('.wic.gz') && !f.includes('->'))
    .map(f => ({
      name: f,
      path: join(IMAGE_DIR, f),
      stat: statSync(join(IMAGE_DIR, f)),
    }))
    .sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);

  // Prefer our custom image.
  const dirtsimImage = files.find(f => f.name === 'dirtsim-image-raspberrypi5.rootfs.wic.gz');
  if (dirtsimImage) {
    return dirtsimImage;
  }

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
    log('This is the YOLO approach - if it fails, pull the disk and reflash.');
    process.exit(0);
  }

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck YOLO Update${colors.reset}`);
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

  // Find image.
  const image = findLatestImage();
  if (!image) {
    error('No image found. Run "kas build kas-dirtsim.yml" first.');
    process.exit(1);
  }

  log('');
  info(`Image: ${image.name}`);
  info(`Size: ${formatBytes(image.stat.size)}`);
  info(`Built: ${image.stat.mtime.toLocaleString()}`);

  // Load SSH key config for image customization.
  const config = loadConfig(CONFIG_FILE);
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
    info(`SSH key: ${basename(config.ssh_key_path)}`);
  }

  // Extract rootfs and inject SSH key.
  let rootfsToTransfer = image.path;
  let workDir = null;

  if (!dryRun && config) {
    banner('Extracting and preparing rootfs...', consola);
    const prepared = await prepareRootfs(image.path, config, REMOTE_USER);
    rootfsToTransfer = prepared.preparedRootfsPath;
    workDir = prepared.workDir;
    cleanup.trackResource('tempdir', workDir);
  }

  try {
    // Get the size of the prepared rootfs.
    const rootfsSize = statSync(rootfsToTransfer).size;

    // Check remote has enough space.
    const remoteSpace = getRemoteTmpSpace(remoteTarget, REMOTE_TMP);
    if (remoteSpace < rootfsSize) {
      error(`Not enough space in ${REMOTE_TMP} on ${remoteHost}`);
      error(`Need: ${formatBytes(rootfsSize)}, Have: ${formatBytes(remoteSpace)}`);
      process.exit(1);
    }
    success(`Remote has enough space (${formatBytes(remoteSpace)} available)`);

    // Calculate checksum of prepared rootfs.
    info('Calculating checksum...');
    const checksum = await calculateChecksum(rootfsToTransfer);
    success(`Checksum: ${checksum.substring(0, 16)}...`);

    // Transfer.
    banner('Transferring image to Pi...', consola);
    const { remoteImagePath, remoteChecksumPath } = await transferImage(
      rootfsToTransfer, checksum, remoteTarget, REMOTE_TMP, dryRun
    );

    // Verify (skip in dry-run since we didn't actually transfer).
    if (!dryRun) {
      if (!verifyRemoteChecksum(remoteImagePath, remoteChecksumPath, remoteTarget)) {
        error('Transfer corrupted! Aborting.');
        process.exit(1);
      }
    }

    // Get boot time before flash for reboot verification.
    const originalBootTime = getRemoteBootTime(remoteTarget);

    // Flash!
    banner('Flashing image on Pi...', consola);
    cleanup.enterCriticalSection();
    await remoteFlash(remoteImagePath, bootDevice, remoteTarget, dryRun, holdMyMead);

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

  } finally {
    // Clean up prepared image temp files.
    if (workDir) {
      cleanupPreparedImage(workDir);
      cleanup.untrackResource('tempdir');
    }
  }
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
