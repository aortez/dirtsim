#!/usr/bin/env node
/**
 * YOLO remote update - push image over network and dd directly to disk.
 *
 * This is the "hold my mead" approach: we scp the image to the Pi,
 * verify the checksum, then dd it to the boot disk while running.
 * If it works, great! If not, pull the disk and reflash.
 *
 * Usage:
 *   npm run yolo                    # Build + push + flash + reboot
 *   npm run yolo -- --skip-build    # Push existing image (skip kas build)
 *   npm run yolo -- --dry-run       # Show what would happen
 *   npm run yolo -- --help          # Show help
 */

import { execSync, spawn } from 'child_process';
import { existsSync, statSync, readFileSync, readdirSync, createReadStream, mkdtempSync, unlinkSync, rmdirSync } from 'fs';
import { join, dirname, basename } from 'path';
import { fileURLToPath } from 'url';
import { createInterface } from 'readline';
import { createHash } from 'crypto';
import { tmpdir } from 'os';
import { createConsola } from 'consola';

// Custom reporter with detailed timestamps (HH:MM:SS.mmm).
const timestampReporter = {
  log(logObj) {
    const d = new Date(logObj.date);
    const hours = String(d.getHours()).padStart(2, '0');
    const minutes = String(d.getMinutes()).padStart(2, '0');
    const seconds = String(d.getSeconds()).padStart(2, '0');
    const ms = String(d.getMilliseconds()).padStart(3, '0');
    const timestamp = `${hours}:${minutes}:${seconds}.${ms}`;

    // Badge based on type.
    const badge = logObj.type === 'success' ? '✔' :
                  logObj.type === 'error' ? '✖' :
                  logObj.type === 'warn' ? '⚠' :
                  logObj.type === 'info' ? 'ℹ' :
                  logObj.type === 'start' ? '▶' : ' ';

    // Color based on type.
    const color = logObj.type === 'success' ? '\x1b[32m' :
                  logObj.type === 'error' ? '\x1b[31m' :
                  logObj.type === 'warn' ? '\x1b[33m' :
                  logObj.type === 'info' ? '\x1b[36m' : '';

    const reset = '\x1b[0m';
    const dim = '\x1b[2m';

    console.log(`${dim}[${timestamp}]${reset} ${color}${badge}${reset} ${logObj.args.join(' ')}`);
  },
};

const consola = createConsola({
  reporters: [timestampReporter],
});

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const IMAGE_DIR = join(YOCTO_DIR, 'build/tmp/deploy/images/raspberrypi5');
const CONFIG_FILE = join(YOCTO_DIR, '.flash-config.json');

// Remote target configuration.
const REMOTE_HOST = 'dirtsim.local';
const REMOTE_USER = 'dirtsim';
const REMOTE_TARGET = `${REMOTE_USER}@${REMOTE_HOST}`;
const REMOTE_DEVICE = '/dev/sda';
const REMOTE_TMP = '/tmp';

// Colors for terminal output (still needed for some custom formatting).
const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
};

// Wrap consola for our needs.
const log = (msg) => console.log(msg);
const info = (msg) => consola.info(msg);
const success = (msg) => consola.success(msg);
const warn = (msg) => consola.warn(msg);
const error = (msg) => consola.error(msg);

function banner(title) {
  consola.box(title);
}

function skull() {
  log('');
  log(`${colors.yellow}    ☠️  YOLO MODE - NO SAFETY NET  ☠️${colors.reset}`);
  log(`${colors.dim}    If this fails, pull the disk and reflash.${colors.reset}`);
  log('');
}

// ============================================================================
// Utilities
// ============================================================================

/**
 * Format bytes to human readable string.
 */
function formatBytes(bytes) {
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (bytes >= 1024 && i < units.length - 1) {
    bytes /= 1024;
    i++;
  }
  return `${bytes.toFixed(1)} ${units[i]}`;
}

/**
 * Prompt user for input.
 */
async function prompt(question) {
  const rl = createInterface({
    input: process.stdin,
    output: process.stdout,
  });

  return new Promise(resolve => {
    rl.question(question, answer => {
      rl.close();
      resolve(answer.trim());
    });
  });
}

/**
 * Run a command with inherited stdio.
 */
async function run(cmd, args, options = {}) {
  return new Promise((resolve, reject) => {
    const proc = spawn(cmd, args, { stdio: 'inherit', cwd: YOCTO_DIR, ...options });
    proc.on('close', code => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`${cmd} exited with code ${code}`));
      }
    });
    proc.on('error', reject);
  });
}

/**
 * Run a command and capture output.
 */
function runCapture(cmd, options = {}) {
  try {
    return execSync(cmd, { encoding: 'utf-8', stdio: 'pipe', ...options }).trim();
  } catch (err) {
    return null;
  }
}

/**
 * Run a command on the remote host via SSH.
 */
function ssh(command, options = {}) {
  const sshCmd = `ssh -o ConnectTimeout=5 -o BatchMode=yes ${REMOTE_TARGET} "${command}"`;
  return runCapture(sshCmd, options);
}

/**
 * Run a command on the remote host via SSH, with inherited stdio for progress.
 */
async function sshRun(command) {
  return run('ssh', [
    '-o', 'ConnectTimeout=10',
    '-o', 'BatchMode=yes',
    REMOTE_TARGET,
    command,
  ]);
}

/**
 * Calculate SHA256 checksum of a file.
 */
async function calculateChecksum(filePath) {
  return new Promise((resolve, reject) => {
    const hash = createHash('sha256');
    const stream = createReadStream(filePath);

    stream.on('data', data => hash.update(data));
    stream.on('end', () => resolve(hash.digest('hex')));
    stream.on('error', reject);
  });
}

// ============================================================================
// SSH Key Configuration
// ============================================================================

/**
 * Load flash configuration from .flash-config.json.
 */
function loadConfig() {
  try {
    if (!existsSync(CONFIG_FILE)) {
      return null;
    }
    const content = readFileSync(CONFIG_FILE, 'utf-8');
    const config = JSON.parse(content);
    if (!config.ssh_key_path || !existsSync(config.ssh_key_path)) {
      return null;
    }
    return config;
  } catch {
    return null;
  }
}

// ============================================================================
// Image Preparation (Local Customization)
// ============================================================================

/**
 * Prepare image with local customizations (SSH key injection, etc.).
 * Returns path to the prepared (customized) image.
 */
async function prepareImage(imagePath, config) {
  banner('Preparing image with local customizations...');

  const workDir = mkdtempSync(join(tmpdir(), 'yolo-image-'));
  const wicPath = join(workDir, 'image.wic');
  const mountPoint = join(workDir, 'rootfs');
  const preparedImagePath = join(workDir, 'prepared-image.wic.gz');

  try {
    // Decompress image.
    info('Decompressing image...');
    execSync(`gunzip -c "${imagePath}" > "${wicPath}"`, { stdio: 'pipe' });

    // Set up loop device with partition scanning.
    info('Setting up loop device...');
    const loopDevice = execSync(`sudo losetup -fP --show "${wicPath}"`, {
      encoding: 'utf-8',
      stdio: 'pipe',
    }).trim();

    try {
      // Mount partition 2 (rootfs).
      const rootfsPartition = `${loopDevice}p2`;
      execSync(`mkdir -p "${mountPoint}"`, { stdio: 'pipe' });

      info('Mounting rootfs...');
      execSync(`sudo mount "${rootfsPartition}" "${mountPoint}"`, { stdio: 'pipe' });

      try {
        // Inject SSH key.
        if (config && config.ssh_key_path) {
          info(`Injecting SSH key: ${basename(config.ssh_key_path)}`);
          const sshKey = readFileSync(config.ssh_key_path, 'utf-8').trim();
          const authorizedKeysPath = join(mountPoint, 'home/dirtsim/.ssh/authorized_keys');

          execSync(`echo '${sshKey}' | sudo tee "${authorizedKeysPath}" > /dev/null`, { stdio: 'pipe' });
          execSync(`sudo chmod 600 "${authorizedKeysPath}"`, { stdio: 'pipe' });
          execSync(`sudo chown 1000:1000 "${authorizedKeysPath}"`, { stdio: 'pipe' });
          success('SSH key injected!');
        }

        // Future: Add more customizations here.

      } finally {
        // Unmount.
        info('Unmounting...');
        execSync(`sudo umount "${mountPoint}"`, { stdio: 'pipe' });
      }

    } finally {
      // Detach loop device.
      info('Detaching loop device...');
      execSync(`sudo losetup -d "${loopDevice}"`, { stdio: 'pipe' });
    }

    // Recompress.
    info('Recompressing image...');
    execSync(`gzip -c "${wicPath}" > "${preparedImagePath}"`, { stdio: 'pipe' });

    // Clean up uncompressed image.
    unlinkSync(wicPath);
    rmdirSync(mountPoint);

    success('Image prepared!');
    return { preparedImagePath, workDir };

  } catch (err) {
    // Clean up on error.
    try {
      execSync(`sudo umount "${mountPoint}" 2>/dev/null || true`, { stdio: 'pipe' });
      execSync(`sudo losetup -D 2>/dev/null || true`, { stdio: 'pipe' });
      unlinkSync(wicPath);
      rmdirSync(mountPoint);
      rmdirSync(workDir);
    } catch {
      // Ignore cleanup errors.
    }
    throw err;
  }
}

/**
 * Clean up prepared image temp files.
 */
function cleanupPreparedImage(workDir) {
  try {
    execSync(`rm -rf "${workDir}"`, { stdio: 'pipe' });
  } catch {
    warn(`Failed to clean up temp directory: ${workDir}`);
  }
}

// ============================================================================
// Image Discovery
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
// Pre-flight Checks
// ============================================================================

/**
 * Check if the remote host is reachable.
 */
function checkRemoteReachable() {
  info(`Checking if ${REMOTE_HOST} is reachable...`);

  const result = runCapture(`ping -c 1 -W 2 ${REMOTE_HOST}`);
  if (result === null) {
    return false;
  }

  // Also check SSH.
  const sshResult = ssh('echo ok');
  return sshResult === 'ok';
}

/**
 * Get available space in /tmp on remote (in bytes).
 * Uses -k flag for BusyBox compatibility (returns KB).
 */
function getRemoteTmpSpace() {
  // Use awk with escaped braces for SSH.
  const result = ssh("df -k " + REMOTE_TMP + " | tail -1 | awk '{ print \\$4 }'");
  if (result) {
    const kb = parseInt(result, 10);
    if (!isNaN(kb)) {
      // Result is in KB, convert to bytes.
      return kb * 1024;
    }
  }
  return 0;
}

/**
 * Get the boot device on the remote system.
 */
function getRemoteBootDevice() {
  // Find what device / is mounted from.
  const result = ssh(`mount | grep ' / ' | cut -d' ' -f1 | sed 's/[0-9]*$//'`);
  return result || REMOTE_DEVICE;
}

// ============================================================================
// Build Phase
// ============================================================================

/**
 * Run the Yocto build.
 */
async function build() {
  banner('Building dirtsim-image...');
  await run('kas', ['build', 'kas-dirtsim.yml']);
  success('Build complete!');
}

// ============================================================================
// Transfer Phase
// ============================================================================

/**
 * Transfer image to remote host.
 */
async function transferImage(imagePath, checksum, dryRun = false) {
  const imageName = basename(imagePath);
  const remoteImagePath = `${REMOTE_TMP}/${imageName}`;
  const remoteChecksumPath = `${REMOTE_TMP}/${imageName}.sha256`;

  banner('Transferring image to Pi...');

  info(`Source: ${imageName}`);
  info(`Target: ${REMOTE_TARGET}:${remoteImagePath}`);
  log('');

  if (dryRun) {
    log(`${colors.yellow}DRY RUN - would execute:${colors.reset}`);
    log(`  scp ${imagePath} ${REMOTE_TARGET}:${remoteImagePath}`);
    log('');
    return { remoteImagePath, remoteChecksumPath };
  }

  // Transfer the image with progress.
  await run('scp', [
    '-o', 'ConnectTimeout=10',
    '-o', 'BatchMode=yes',
    imagePath,
    `${REMOTE_TARGET}:${remoteImagePath}`,
  ]);

  success('Image transferred!');

  // Write checksum file on remote.
  info('Writing checksum file...');
  ssh(`echo '${checksum}  ${imageName}' > ${remoteChecksumPath}`);

  return { remoteImagePath, remoteChecksumPath };
}

/**
 * Verify checksum on remote host.
 */
function verifyRemoteChecksum(remoteImagePath, remoteChecksumPath) {
  info('Verifying checksum on Pi...');

  const result = ssh(`cd ${REMOTE_TMP} && sha256sum -c ${basename(remoteChecksumPath)}`);

  if (result && result.includes('OK')) {
    success('Checksum verified!');
    return true;
  }

  error('Checksum verification failed!');
  return false;
}

// ============================================================================
// Flash Phase (The YOLO Part)
// ============================================================================

/**
 * Flash the image on the remote host.
 * This is the point of no return.
 */
async function remoteFlash(remoteImagePath, device, dryRun = false, skipConfirm = false) {
  banner('Flashing image on Pi...');
  skull();

  if (dryRun) {
    log(`${colors.yellow}DRY RUN - would execute:${colors.reset}`);
    log('');
    log(`  # Stop services to reduce disk activity`);
    log(`  sudo systemctl stop sparkle-duck 2>/dev/null || true`);
    log('');
    log(`  # Decompress and write image (BusyBox dd)`);
    log(`  gunzip -c ${remoteImagePath} | sudo dd of=${device} bs=4M`);
    log('');
    log(`  # Sync and reboot`);
    log(`  sync && sync && sudo reboot -f`);
    log('');
    return;
  }

  // Final confirmation.
  log(`${colors.bold}${colors.red}═══════════════════════════════════════════════════════════════${colors.reset}`);
  log(`${colors.bold}${colors.red}  THIS WILL OVERWRITE ${device} ON ${REMOTE_HOST}${colors.reset}`);
  log(`${colors.bold}${colors.red}  The system may become unresponsive during the write.${colors.reset}`);
  log(`${colors.bold}${colors.red}  If it fails, you'll need to pull the disk and reflash.${colors.reset}`);
  log(`${colors.bold}${colors.red}═══════════════════════════════════════════════════════════════${colors.reset}`);
  log('');

  if (!skipConfirm) {
    const confirm = await prompt(`Type "yolo" to proceed: `);
    if (confirm.toLowerCase() !== 'yolo') {
      error('Aborted.');
      process.exit(1);
    }
  } else {
    log(`${colors.yellow}🍺 Hold my mead... here we go!${colors.reset}`);
  }

  log('');
  info('Stopping services...');
  ssh('sudo systemctl stop sparkle-duck 2>/dev/null || true');

  info('Starting flash (this may take a while)...');
  log('');

  // Run the dd command.  Connection may drop when reboot happens.
  // Note: BusyBox dd doesn't support status=progress or conv=fsync,
  // so we use basic options and run sync separately.
  // We use spawn with piped stdio to avoid terminal issues when SSH drops.
  try {
    const ddCmd = `gunzip -c ${remoteImagePath} | sudo dd of=${device} bs=4M && sync && sync && sudo reboot -f`;
    const proc = spawn('ssh', [
      '-o', 'ConnectTimeout=10',
      '-o', 'BatchMode=yes',
      '-o', 'ServerAliveInterval=5',
      REMOTE_TARGET,
      ddCmd,
    ], { stdio: ['ignore', 'pipe', 'pipe'] });

    // Forward output to console.
    proc.stdout.on('data', data => process.stdout.write(data));
    proc.stderr.on('data', data => process.stderr.write(data));

    await new Promise((resolve, reject) => {
      proc.on('close', code => {
        // Any exit is fine - connection will drop during reboot.
        resolve();
      });
      proc.on('error', reject);
    });
  } catch {
    // Expected - SSH connection drops when system reboots.
  }
  log('');
  info('Connection lost (expected during reboot)');
  success('Flash command sent!');
}

// ============================================================================
// Wait for Reboot
// ============================================================================

/**
 * Wait for the device to come back online.
 */
async function waitForReboot(timeoutSec = 120) {
  banner('Waiting for Pi to reboot...');

  const startTime = Date.now();
  const timeoutMs = timeoutSec * 1000;
  let dots = 0;

  // Wait a bit for the system to go down.
  info('Waiting for shutdown...');
  await new Promise(resolve => setTimeout(resolve, 1000));

  while (Date.now() - startTime < timeoutMs) {
    process.stdout.write(`\r  Waiting${'.'.repeat(dots % 4).padEnd(4)} (${Math.floor((Date.now() - startTime) / 1000)}s)`);
    dots++;

    const sshResult = ssh('echo ok');
    if (sshResult === 'ok') {
      process.stdout.write('\r' + ' '.repeat(40) + '\r');
      success(`${REMOTE_HOST} is back online!`);
      return true;
    }

    await new Promise(resolve => setTimeout(resolve, 2000));
  }

  process.stdout.write('\r' + ' '.repeat(40) + '\r');
  warn(`Timeout waiting for reboot after ${timeoutSec}s`);
  return false;
}

// ============================================================================
// Main Entry Point
// ============================================================================

async function main() {
  const args = process.argv.slice(2);

  const skipBuild = args.includes('--skip-build');
  const dryRun = args.includes('--dry-run');
  const holdMyMead = args.includes('--hold-my-mead');

  if (args.includes('-h') || args.includes('--help')) {
    log('Usage: npm run yolo [options]');
    log('');
    log('Push a Yocto image to the Pi over the network and flash it live.');
    log('');
    log('Options:');
    log('  --skip-build     Skip kas build, use existing image');
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
  if (!checkRemoteReachable()) {
    error(`Cannot reach ${REMOTE_HOST}`);
    error('Make sure the Pi is running and accessible via SSH.');
    process.exit(1);
  }
  success(`${REMOTE_HOST} is reachable`);

  // Detect boot device.
  const bootDevice = getRemoteBootDevice();
  info(`Boot device: ${bootDevice}`);

  // Build phase.
  if (!skipBuild) {
    await build();
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
  const config = loadConfig();
  if (!config) {
    warn('No SSH key configured. Run "npm run flash -- --reconfigure" first.');
    warn('Image will be flashed without SSH key - you may be locked out!');
    const proceed = await prompt('Continue anyway? (y/N): ');
    if (proceed.toLowerCase() !== 'y') {
      error('Aborted.');
      process.exit(1);
    }
  } else {
    info(`SSH key: ${basename(config.ssh_key_path)}`);
  }

  // Prepare image with local customizations (SSH key injection, etc.).
  let imageToTransfer = image.path;
  let workDir = null;

  if (!dryRun && config) {
    const prepared = await prepareImage(image.path, config);
    imageToTransfer = prepared.preparedImagePath;
    workDir = prepared.workDir;
  }

  try {
    // Get the size of the prepared image.
    const imageSize = statSync(imageToTransfer).size;

    // Check remote has enough space.
    const remoteSpace = getRemoteTmpSpace();
    if (remoteSpace < imageSize) {
      error(`Not enough space in ${REMOTE_TMP} on ${REMOTE_HOST}`);
      error(`Need: ${formatBytes(imageSize)}, Have: ${formatBytes(remoteSpace)}`);
      process.exit(1);
    }
    success(`Remote has enough space (${formatBytes(remoteSpace)} available)`);

    // Calculate checksum of prepared image.
    info('Calculating checksum...');
    const checksum = await calculateChecksum(imageToTransfer);
    success(`Checksum: ${checksum.substring(0, 16)}...`);

    // Transfer.
    const { remoteImagePath, remoteChecksumPath } = await transferImage(imageToTransfer, checksum, dryRun);

    // Verify (skip in dry-run since we didn't actually transfer).
    if (!dryRun) {
      if (!verifyRemoteChecksum(remoteImagePath, remoteChecksumPath)) {
        error('Transfer corrupted! Aborting.');
        process.exit(1);
      }
    }

    // Flash!
    await remoteFlash(remoteImagePath, bootDevice, dryRun, holdMyMead);

    if (!dryRun) {
      // Wait for reboot.
      const online = await waitForReboot(120);

      log('');
      if (online) {
        log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
        success('YOLO update complete!');
        info(`Connect with: ssh ${REMOTE_TARGET}`);
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
    }
  }
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
