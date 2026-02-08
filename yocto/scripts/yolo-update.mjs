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
 *   npm run yolo -- --clean-all               # Force full rebuild (cleans server + audio + image)
 *   npm run yolo -- --skip-build              # Push existing image (skip kas build)
 *   npm run yolo -- --docker                  # Build with Docker image
 *   npm run yolo -- --fast                    # Fast dev deploy (ninja + scp + restart)
 *   npm run yolo -- --dry-run                 # Show what would happen
 *   npm run yolo -- --help                    # Show help
 */

import { execSync } from 'child_process';
import { existsSync, statSync, readdirSync, readFileSync, writeFileSync, unlinkSync } from 'fs';
import { join, dirname, basename, isAbsolute } from 'path';
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
import { ensureYoctoDockerImage, runInYoctoDocker } from './lib/docker-yocto.mjs';

// Path setup.
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);

function resolveKasBuildDir() {
  const buildDir = process.env.KAS_BUILD_DIR;
  if (!buildDir) {
    return join(YOCTO_DIR, 'build');
  }
  return isAbsolute(buildDir) ? buildDir : join(YOCTO_DIR, buildDir);
}

const KAS_BUILD_DIR = resolveKasBuildDir();
const IMAGE_DIR = join(KAS_BUILD_DIR, 'tmp/deploy/images/raspberrypi-dirtsim');
const CONFIG_FILE = join(YOCTO_DIR, '.flash-config.json');
const PROFILES_DIR = join(YOCTO_DIR, 'profiles');

// Remote target configuration (defaults).
const DEFAULT_HOST = 'dirtsim.local';
const REMOTE_USER = 'dirtsim';
const REMOTE_DEVICE = '/dev/sda';
const REMOTE_TMP = '/tmp';

function unique(items) {
  const seen = new Set();
  const out = [];
  for (const item of items) {
    if (!item) continue;
    if (seen.has(item)) continue;
    seen.add(item);
    out.push(item);
  }
  return out;
}

function buildYoctoNinjaEnv(buildDir) {
  // Yocto recipes set up PATH so CMake can find cross tools like *-gcc-ar.
  // Fast deploy runs outside bitbake, so we recreate the minimal PATH additions.
  const recipeRootDir = dirname(buildDir); // .../tmp/work/.../<recipe>/git
  const sysrootNativeBin = join(recipeRootDir, 'recipe-sysroot-native', 'usr', 'bin');
  const extraPaths = [];

  if (existsSync(sysrootNativeBin)) {
    extraPaths.push(sysrootNativeBin);
    for (const entry of readdirSync(sysrootNativeBin, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      extraPaths.push(join(sysrootNativeBin, entry.name));
    }
  }

  const currentPath = process.env.PATH || '';
  return {
    ...process.env,
    PATH: unique([...extraPaths, currentPath]).join(':'),
  };
}

// Set up consola with timestamp reporter.
const timestampReporter = setupConsolaLogging();
const consola = createConsola({ reporters: [timestampReporter] });

// Set up cleanup manager for signal handling.
const cleanup = createCleanupManager();
cleanup.installSignalHandlers();

// ============================================================================
// Build Phase (Project-Specific)
// ============================================================================

let useDocker = false;
let dockerImageRef = null;

async function resolveDockerImage() {
  if (!dockerImageRef) {
    dockerImageRef = await ensureYoctoDockerImage();
  }
  return dockerImageRef;
}

async function runKas(args) {
  if (useDocker) {
    const imageRef = await resolveDockerImage();
    await runInYoctoDocker(['kas', ...args], { imageRef });
    return;
  }

  await run('kas', args, { cwd: YOCTO_DIR });
}

function runCapture(command, options = {}) {
  try {
    return execSync(command, { stdio: ['ignore', 'pipe', 'pipe'], ...options })
      .toString()
      .trim();
  } catch {
    return '';
  }
}

function getSshIdentity() {
  return process.env.DIRTSIM_SSH_IDENTITY
    || process.env.DIRTSIM_SSH_PRIVATE_KEY_PATH
    || null;
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

function buildSshOptions() {
  const options = ['-o BatchMode=yes', '-o ConnectTimeout=10'];
  const identity = getSshIdentity();
  if (identity) {
    options.push(`-i ${identity}`);
  }
  if (shouldDisableStrictHostKeyChecking()) {
    options.push('-o StrictHostKeyChecking=no', '-o UserKnownHostsFile=/dev/null');
  }
  return options.join(' ');
}

function buildScpOptions() {
  return buildSshOptions();
}

function shouldSkipPing() {
  return process.env.DIRTSIM_SKIP_PING === '1' || process.env.DIRTSIM_SKIP_PING === 'true';
}

function shouldSkipRemoteCheck() {
  return process.env.DIRTSIM_SKIP_REMOTE_CHECK === '1'
    || process.env.DIRTSIM_SKIP_REMOTE_CHECK === 'true';
}

function canSsh(remoteTarget) {
  const output = runCapture(`ssh ${buildSshOptions()} ${remoteTarget} "echo ok"`);
  return output === 'ok';
}

/**
 * Clean the image sstate to force a rebuild.
 */
async function cleanImage() {
  info('Cleaning dirtsim-image sstate to force rebuild...');
  await runKas(['shell', 'kas-dirtsim.yml', '-c', 'bitbake -c cleansstate dirtsim-image']);
  success('Clean complete!');
}

/**
 * Clean both server and image sstate for a full rebuild.
 */
async function cleanAll() {
  info('Cleaning dirtsim-server, dirtsim-audio, and dirtsim-image sstate...');
  await runKas([
    'shell',
    'kas-dirtsim.yml',
    '-c',
    'bitbake -c cleansstate dirtsim-server dirtsim-audio dirtsim-image',
  ]);
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

  await runKas(['build', 'kas-dirtsim.yml']);
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
// Profile Support
// ============================================================================

/**
 * Get available profiles from the profiles directory.
 * @returns {string[]} Array of profile names.
 */
function getAvailableProfiles() {
  if (!existsSync(PROFILES_DIR)) {
    return [];
  }
  try {
    return readdirSync(PROFILES_DIR, { withFileTypes: true })
      .filter(d => d.isDirectory())
      .map(d => d.name);
  } catch {
    return [];
  }
}

/**
 * Recursively get all files in a directory with their relative paths.
 * @param {string} dir - Directory to scan.
 * @param {string} base - Base path for relative paths.
 * @returns {Array<{localPath: string, remotePath: string}>}
 */
function getProfileFiles(dir, base = '') {
  const results = [];
  const entries = readdirSync(dir, { withFileTypes: true });

  for (const entry of entries) {
    const localPath = join(dir, entry.name);
    const remotePath = base ? `${base}/${entry.name}` : entry.name;

    if (entry.isDirectory()) {
      results.push(...getProfileFiles(localPath, remotePath));
    } else {
      results.push({ localPath, remotePath: `/${remotePath}` });
    }
  }

  return results;
}

/**
 * Apply a profile to an ext4 rootfs image using e2tools (no sudo required).
 * @param {string} rootfsPath - Path to the .ext4.gz file.
 * @param {string} profileName - Name of the profile to apply.
 * @returns {string} Path to the modified .ext4.gz file (in temp directory).
 */
function applyProfileToRootfs(rootfsPath, profileName) {
  const profileDir = join(PROFILES_DIR, profileName);
  if (!existsSync(profileDir)) {
    throw new Error(`Profile not found: ${profileName}`);
  }

  // Create temp directory for work.
  const workDir = join(tmpdir(), `dirtsim-profile-${Date.now()}`);
  execSync(`mkdir -p "${workDir}"`, { stdio: 'pipe' });

  const ext4Path = join(workDir, 'rootfs.ext4');
  const outputPath = join(workDir, 'rootfs.ext4.gz');

  try {
    // Decompress.
    info('Decompressing rootfs for profile overlay...');
    execSync(`gunzip -c "${rootfsPath}" > "${ext4Path}"`, { stdio: 'pipe' });

    // Get all files from the profile.
    const files = getProfileFiles(profileDir);

    if (files.length === 0) {
      warn(`Profile ${profileName} has no files to apply`);
    } else {
      info(`Applying ${files.length} file(s) from profile: ${profileName}`);

      // Create directories and copy files using e2tools.
      const dirsCreated = new Set();

      for (const file of files) {
        // Ensure parent directories exist.
        const parentDir = dirname(file.remotePath);
        if (parentDir !== '/' && !dirsCreated.has(parentDir)) {
          // e2mkdir needs each level created, so create the full path.
          const parts = parentDir.split('/').filter(p => p);
          let currentPath = '';
          for (const part of parts) {
            currentPath += `/${part}`;
            if (!dirsCreated.has(currentPath)) {
              try {
                execSync(`e2mkdir "${ext4Path}:${currentPath}"`, { stdio: 'pipe' });
              } catch {
                // Directory might already exist, that's fine.
              }
              dirsCreated.add(currentPath);
            }
          }
        }

        // Copy file.
        execSync(`e2cp "${file.localPath}" "${ext4Path}:${file.remotePath}"`, { stdio: 'pipe' });
        success(`  ${file.remotePath}`);
      }
    }

    // Recompress.
    info('Recompressing rootfs...');
    execSync(`gzip -c "${ext4Path}" > "${outputPath}"`, { stdio: 'pipe' });

    // Clean up uncompressed file.
    unlinkSync(ext4Path);

    success(`Profile ${profileName} applied!`);
    return outputPath;

  } catch (err) {
    // Clean up on error.
    try {
      execSync(`rm -rf "${workDir}"`, { stdio: 'pipe' });
    } catch {
      // Ignore cleanup errors.
    }
    throw err;
  }
}

// ============================================================================
// Fast Deploy Mode (Direct ninja + scp + restart)
// ============================================================================

// Build directory paths for ninja builds.
const WORK_DIR = join(KAS_BUILD_DIR, 'tmp/work');
const ARCH_PATTERNS = ['cortexa72-poky-linux', 'cortexa76-poky-linux'];

// Cross-toolchain strip for reducing binary size.
const STRIP_TOOL = join(
  KAS_BUILD_DIR,
  'tmp/sysroots-components/x86_64/binutils-cross-aarch64/usr/bin/aarch64-poky-linux/aarch64-poky-linux-strip'
);

/**
 * Find the build directory for a recipe.
 * Returns the most recently modified build directory across architectures.
 */
function findBuildDir(recipe) {
  for (const arch of ARCH_PATTERNS) {
    const buildDir = join(WORK_DIR, arch, recipe, 'git/build');
    if (existsSync(buildDir)) {
      return buildDir;
    }
  }
  return null;
}

/**
 * Find the binary path for a recipe.
 */
function findBinary(recipe, binaryName) {
  const buildDir = findBuildDir(recipe);
  if (!buildDir) return null;

  const binaryPath = join(buildDir, 'bin', binaryName);
  return existsSync(binaryPath) ? binaryPath : null;
}

function parseSystemStatus(output) {
  try {
    const parsed = JSON.parse(output);
    const value = parsed.value || parsed;
    if (!value) {
      return null;
    }
    return {
      uiStatus: value.ui_status || '',
      serverStatus: value.server_status || '',
    };
  } catch {
    return null;
  }
}

async function waitForSystemStatusOk(remoteTarget, remoteHost, timeoutSec = 120) {
  info('Checking SystemStatus via os-manager...');
  const startTime = Date.now();
  const timeoutMs = timeoutSec * 1000;
  let lastStatus = null;

  while (Date.now() - startTime < timeoutMs) {
    const output = runCapture(`ssh ${buildSshOptions()} ${remoteTarget} "dirtsim-cli os-manager SystemStatus"`);
    if (output) {
      const status = parseSystemStatus(output);
      if (status) {
        lastStatus = status;
        if (status.uiStatus === 'OK' && status.serverStatus === 'OK') {
          success(`${remoteHost} SystemStatus OK (UI+server)`);
          return true;
        }
      }
    }

    await new Promise(resolve => setTimeout(resolve, 2000));
  }

  if (lastStatus) {
    error(`SystemStatus not OK (ui_status=${lastStatus.uiStatus}, server_status=${lastStatus.serverStatus})`);
  } else {
    error('SystemStatus check failed (no response)');
  }
  return false;
}

/**
 * Fast deploy: ninja build + scp binaries + restart services.
 * Skips rootfs regeneration, image creation, and flash/reboot.
 */
async function fastDeploy(remoteHost, remoteTarget, dryRun) {
  const startTime = Date.now();

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck Fast Deploy${colors.reset}`);
  log(`${colors.dim}(ninja build + scp + restart)${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  log('');

  // Check Pi is reachable.
  if (!checkRemoteReachable(remoteHost, remoteTarget)) {
    error(`Cannot reach ${remoteHost}`);
    error('Make sure the Pi is running and accessible via SSH.');
    process.exit(1);
  }
  success(`${remoteHost} is reachable`);

  // Find build directories.
  const uiBuildDir = findBuildDir('dirtsim-ui');
  const serverBuildDir = findBuildDir('dirtsim-server');
  const osManagerBuildDir = findBuildDir('dirtsim-os-manager');
  const audioBuildDir = findBuildDir('dirtsim-audio');

  if (!uiBuildDir && !serverBuildDir && !osManagerBuildDir && !audioBuildDir) {
    error('No build directories found.');
    error('Run a full build first: ./update.sh --target <host>');
    process.exit(1);
  }

  // Build with ninja.
  banner('Building with ninja...', consola);

  const targets = [];
  if (uiBuildDir) {
    info(`UI build dir: ${uiBuildDir}`);
    targets.push({ name: 'dirtsim-ui', buildDir: uiBuildDir, target: 'dirtsim-ui' });
    targets.push({ name: 'dirtsim-cli', buildDir: uiBuildDir, target: 'cli' });
  }
  if (serverBuildDir) {
    info(`Server build dir: ${serverBuildDir}`);
    targets.push({ name: 'dirtsim-server', buildDir: serverBuildDir, target: 'dirtsim-server' });
  }
  if (osManagerBuildDir) {
    info(`OS manager build dir: ${osManagerBuildDir}`);
    targets.push({ name: 'dirtsim-os-manager', buildDir: osManagerBuildDir, target: 'dirtsim-os-manager' });
  }
  if (audioBuildDir) {
    info(`Audio build dir: ${audioBuildDir}`);
    targets.push({ name: 'dirtsim-audio', buildDir: audioBuildDir, target: 'dirtsim-audio' });
  }

  for (const t of targets) {
    if (!dryRun) {
      try {
        info(`Building ${t.name}...`);
        execSync(`ninja ${t.target}`, {
          cwd: t.buildDir,
          stdio: 'inherit',
          env: buildYoctoNinjaEnv(t.buildDir),
        });
      } catch (err) {
        error(`Failed to build ${t.name}`);
        process.exit(1);
      }
    } else {
      info(`Would build ${t.name} in ${t.buildDir}`);
    }
  }
  success('Build complete!');

  // Find binaries.
  const binaries = [];
  const uiBinary = findBinary('dirtsim-ui', 'dirtsim-ui');
  const serverBinary = findBinary('dirtsim-server', 'dirtsim-server');
  const osManagerBinary = findBinary('dirtsim-os-manager', 'dirtsim-os-manager');
  const audioBinary = findBinary('dirtsim-audio', 'dirtsim-audio');
  const cliBinary = findBinary('dirtsim-ui', 'cli');  // CLI is built with UI.

  if (uiBinary) {
    const stat = statSync(uiBinary);
    binaries.push({
      name: 'dirtsim-ui',
      path: uiBinary,
      size: stat.size,
      service: 'dirtsim-ui',
      remotePath: '/usr/bin/dirtsim-ui',
    });
  }
  if (serverBinary) {
    const stat = statSync(serverBinary);
    binaries.push({
      name: 'dirtsim-server',
      path: serverBinary,
      size: stat.size,
      service: 'dirtsim-server',
      remotePath: '/usr/bin/dirtsim-server',
    });
  }
  if (osManagerBinary) {
    const stat = statSync(osManagerBinary);
    binaries.push({
      name: 'dirtsim-os-manager',
      path: osManagerBinary,
      size: stat.size,
      service: 'dirtsim-os-manager',
      remotePath: '/usr/bin/dirtsim-os-manager',
    });
  }
  if (audioBinary) {
    const stat = statSync(audioBinary);
    binaries.push({
      name: 'dirtsim-audio',
      path: audioBinary,
      size: stat.size,
      service: 'dirtsim-audio',
      remotePath: '/usr/bin/dirtsim-audio',
    });
  }
  if (cliBinary) {
    const stat = statSync(cliBinary);
    binaries.push({
      name: 'dirtsim-cli',
      path: cliBinary,
      size: stat.size,
      service: null,  // No service for CLI.
      remotePath: '/usr/bin/dirtsim-cli',
    });
  }

  if (binaries.length === 0) {
    error('No binaries found after build.');
    process.exit(1);
  }

  // Strip and transfer binaries.
  banner('Stripping and transferring binaries...', consola);

  const hasStrip = existsSync(STRIP_TOOL);
  if (!hasStrip) {
    warn('Cross-strip tool not found, transferring unstripped binaries (slower).');
  }

  for (const bin of binaries) {
    info(`${bin.name}: ${formatBytes(bin.size)} (unstripped)`);

    if (!dryRun) {
      try {
        // Strip to temp file to avoid modifying build output.
        const strippedPath = `/tmp/${bin.name}.stripped`;
        if (hasStrip) {
          execSync(`cp "${bin.path}" "${strippedPath}" && "${STRIP_TOOL}" "${strippedPath}"`, { stdio: 'pipe' });
          const strippedStat = statSync(strippedPath);
          info(`${bin.name}: ${formatBytes(strippedStat.size)} (stripped)`);
          execSync(`scp ${buildScpOptions()} "${strippedPath}" "${remoteTarget}:/tmp/${bin.name}"`, { stdio: 'pipe' });
          unlinkSync(strippedPath);
        } else {
          execSync(`scp ${buildScpOptions()} "${bin.path}" "${remoteTarget}:/tmp/${bin.name}"`, { stdio: 'pipe' });
        }
        success(`${bin.name} transferred`);
      } catch (err) {
        error(`Failed to transfer ${bin.name}`);
        process.exit(1);
      }
    } else {
      info(`Would strip and scp ${bin.path} to ${remoteTarget}:/tmp/${bin.name}`);
    }
  }

  // Deploy config files if .local overrides exist.
  banner('Checking for config overrides...', consola);

  const APPS_CONFIG_DIR = join(YOCTO_DIR, '../apps/config');
  const configFiles = [];

  for (const configName of ['server.json.local', 'ui.json.local']) {
    const localPath = join(APPS_CONFIG_DIR, configName);
    if (existsSync(localPath)) {
      configFiles.push({
        name: configName,
        path: localPath,
        remotePath: `/etc/dirtsim/${configName}`,
      });
    }
  }

  if (configFiles.length > 0) {
    info(`Found ${configFiles.length} config override(s) to deploy`);
    for (const cfg of configFiles) {
      info(`  ${cfg.name}`);
      if (!dryRun) {
        try {
          execSync(`scp ${buildScpOptions()} "${cfg.path}" "${remoteTarget}:/tmp/${cfg.name}"`, { stdio: 'pipe' });
          // Copy and set permissions so dirtsim user can read the config files.
          execSync(`ssh ${buildSshOptions()} ${remoteTarget} "sudo cp /tmp/${cfg.name} ${cfg.remotePath} && sudo chmod 644 ${cfg.remotePath}"`, { stdio: 'pipe' });
          success(`${cfg.name} deployed`);
        } catch (err) {
          error(`Failed to deploy ${cfg.name}`);
          process.exit(1);
        }
      } else {
        info(`Would scp ${cfg.path} to ${remoteTarget}:${cfg.remotePath}`);
      }
    }

    // Run config setup service to fix all permissions (config files + home dirs).
    if (!dryRun) {
      try {
        info('Running config setup to fix permissions...');
        execSync(`ssh ${buildSshOptions()} ${remoteTarget} "sudo systemctl restart dirtsim-config-setup.service"`, { stdio: 'pipe' });
        success('Permissions fixed');
      } catch (err) {
        warn('Config setup service not available (needs full deployment)');
      }
    }
  } else {
    info('No .local config overrides found');
  }

  // Stop services, copy binaries, start services.
  banner('Restarting services...', consola);

  const serviceNames = binaries.map(b => b.service).filter(s => s).join(' ');
  const copyCommands = binaries.map(b => `sudo cp /tmp/${b.name} ${b.remotePath}`).join(' && ');

  const remoteCmd = `sudo systemctl stop ${serviceNames} && ${copyCommands} && sudo systemctl start ${serviceNames}`;

  // Delete old binaries before copying to avoid "no space" errors when rootfs is tight.
  const deleteCommands = binaries.map(b => `sudo rm -f ${b.remotePath}`).join(' && ');

  if (!dryRun) {
    try {
      info('Stopping services...');
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "sudo systemctl stop ${serviceNames}"`, { stdio: 'pipe' });

      info('Removing old binaries...');
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "${deleteCommands}"`, { stdio: 'pipe' });

      info('Copying new binaries...');
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "${copyCommands}"`, { stdio: 'pipe' });

      info('Starting services...');
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "sudo systemctl start ${serviceNames}"`, { stdio: 'pipe' });

      success('Services restarted!');
    } catch (err) {
      error('Failed to restart services');
      error(err.message);
      process.exit(1);
    }
  } else {
    info(`Would run on Pi: ${deleteCommands} && ${remoteCmd}`);
  }

  if (!dryRun) {
    const statusOk = await waitForSystemStatusOk(remoteTarget, remoteHost, 120);
    if (!statusOk) {
      error('SystemStatus check failed after fast deploy.');
      process.exit(1);
    }
  }

  // Done!
  const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
  log('');
  log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
  success(`Fast deploy complete in ${elapsed}s!`);
  info(`Connect with: ssh ${remoteTarget}`);
  log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
  log('');
}

// ============================================================================
// Main Entry Point
// ============================================================================

async function main() {
  const args = process.argv.slice(2).map(arg => arg.replace(/\r/g, ''));

  const skipBuild = args.includes('--skip-build');
  const forceClean = args.includes('--clean');
  const forceCleanAll = args.includes('--clean-all');
  const dryRun = args.includes('--dry-run');
  const holdMyMead = args.includes('--hold-my-mead');
  const fastMode = args.includes('--fast');
  const docker =
    args.some(arg => arg === '--docker' || arg.startsWith('--docker=')) ||
    process.env.DIRTSIM_YOCTO_DOCKER === '1' ||
    process.env.DIRTSIM_YOCTO_DOCKER === 'true';

  // Parse --target <hostname> argument.
  const targetIdx = args.indexOf('--target');
  const remoteHost = (targetIdx !== -1 && args[targetIdx + 1]) ? args[targetIdx + 1] : DEFAULT_HOST;
  const remoteTarget = `${REMOTE_USER}@${remoteHost}`;

  // Parse --profile <name> argument.
  const profileIdx = args.indexOf('--profile');
  const specifiedProfile = (profileIdx !== -1 && args[profileIdx + 1]) ? args[profileIdx + 1] : null;

  if (args.includes('-h') || args.includes('--help')) {
    const profiles = getAvailableProfiles();
    const profileList = profiles.length > 0 ? profiles.join(', ') : '(none)';

    log('Usage: npm run yolo [options]');
    log('');
    log('Push a Yocto image to the Pi over the network and flash it live.');
    log('No local sudo required - all privileged operations happen on the Pi.');
    log('');
    log('Options:');
    log('  --target <host>    Target hostname or IP (default: dirtsim.local)');
    log('  --profile <name>   Apply a configuration profile (e.g., production)');
    log('  --skip-build       Skip kas build, use existing image');
    log('  --docker           Build with the Docker Yocto image');
    log('  --fast             Fast dev deploy: ninja build + scp binaries + restart');
    log('                     Skips rootfs/image creation (~10s vs ~2min)');
    log('  --clean            Force rebuild by cleaning image sstate first');
    log('  --clean-all        Force full rebuild (cleans server + audio + image sstate)');
    log('  --dry-run          Show what would happen without doing it');
    log('  --hold-my-mead     Skip confirmation prompt (for scripts)');
    log('  -h, --help         Show this help');
    log('');
    log(`Available profiles: ${profileList}`);
    log('');
    log('This is the YOLO approach - if it fails, the previous slot still works.');
    process.exit(0);
  }

  useDocker = docker;
  if (fastMode && useDocker) {
    warn('Ignoring --docker in --fast mode (fast deploy runs locally).');
    useDocker = false;
  }
  if (useDocker) {
    info('Using Docker for Yocto build.');
  }

  // Fast mode: ninja build + scp + restart (skip rootfs/image/flash).
  // Profiles require rootfs modification, which fast mode skips.
  if (fastMode && specifiedProfile) {
    error('Cannot use --fast with --profile');
    error('--fast only copies binaries (no rootfs modification)');
    error('--profile requires rootfs overlay (use full update instead)');
    process.exit(1);
  }

  if (fastMode) {
    await fastDeploy(remoteHost, remoteTarget, dryRun);
    return;
  }

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck YOLO Update${colors.reset}`);
  log(`${colors.dim}(no local sudo required)${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  skull();

  // Pre-flight checks.
  if (shouldSkipRemoteCheck()) {
    warn('Skipping remote reachability checks (DIRTSIM_SKIP_REMOTE_CHECK set).');
  } else if (shouldSkipPing()) {
    info('Skipping ping check; verifying SSH only...');
    if (!canSsh(remoteTarget)) {
      error(`Cannot reach ${remoteHost} via SSH`);
      error('Make sure the Pi is running and accessible via SSH.');
      process.exit(1);
    }
    success(`${remoteHost} is reachable via SSH`);
  } else if (!checkRemoteReachable(remoteHost, remoteTarget)) {
    error(`Cannot reach ${remoteHost}`);
    error('Make sure the Pi is running and accessible via SSH.');
    process.exit(1);
  } else {
    success(`${remoteHost} is reachable`);
  }

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

  // Apply profile if specified.
  let rootfsToUse = rootfs.path;
  let profileWorkDir = null;

  if (specifiedProfile) {
    const availableProfiles = getAvailableProfiles();
    if (!availableProfiles.includes(specifiedProfile)) {
      error(`Unknown profile: ${specifiedProfile}`);
      info(`Available profiles: ${availableProfiles.join(', ') || '(none)'}`);
      process.exit(1);
    }

    if (!dryRun) {
      banner(`Applying profile: ${specifiedProfile}`, consola);
      rootfsToUse = applyProfileToRootfs(rootfs.path, specifiedProfile);
      profileWorkDir = dirname(rootfsToUse);

      // Update size info.
      const modifiedStat = statSync(rootfsToUse);
      info(`Modified rootfs size: ${formatBytes(modifiedStat.size)}`);
    } else {
      info(`Would apply profile: ${specifiedProfile}`);
    }
  }

  // Load SSH key config for remote injection.
  const envSshKeyPath = process.env.DIRTSIM_SSH_KEY_PATH || null;
  if (envSshKeyPath && !existsSync(envSshKeyPath)) {
    error(`DIRTSIM_SSH_KEY_PATH not found: ${envSshKeyPath}`);
    process.exit(1);
  }

  const config = loadConfig(CONFIG_FILE);
  let sshKeyPath = envSshKeyPath;

  if (!sshKeyPath && !config) {
    warn('No SSH key configured. Run "npm run flash -- --reconfigure" first.');
    warn('Image will be flashed without SSH key - you may be locked out!');
    if (process.env.CI === 'true' || process.env.GITHUB_ACTIONS === 'true') {
      error('Missing SSH key configuration in CI.');
      process.exit(1);
    }
    if (!dryRun) {
      const { prompt } = await import('../pi-base/scripts/lib/cli-utils.mjs');
      const proceed = await prompt('Continue anyway? (y/N): ');
      if (proceed.toLowerCase() !== 'y') {
        error('Aborted.');
        process.exit(1);
      }
    }
  } else if (!sshKeyPath && config) {
    sshKeyPath = config.ssh_key_path;
  }

  if (sshKeyPath) {
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
  const checksum = await calculateChecksum(rootfsToUse);
  success(`Checksum: ${checksum.substring(0, 16)}...`);

  // Transfer rootfs.
  banner('Transferring rootfs to Pi...', consola);
  const { remoteImagePath, remoteChecksumPath } = await transferImage(
    rootfsToUse, checksum, remoteTarget, REMOTE_TMP, dryRun
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
    execSync(`scp ${buildScpOptions()} "${sshKeyPath}" "${remoteTarget}:${remoteKeyPath}"`, { stdio: 'pipe' });
    success('SSH key transferred');
  }

  // Check if ab-update-with-key exists on remote, transfer if needed.
  // This handles the bootstrap case where the Pi doesn't have the new script yet.
  let remoteUpdateScript = 'ab-update-with-key';
  if (!dryRun) {
    try {
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "which ab-update-with-key"`, { stdio: 'pipe' });
    } catch {
      info('ab-update-with-key not found on Pi, transferring...');
      const localScript = join(YOCTO_DIR, 'pi-base/yocto/meta-pi-base/recipes-support/ab-boot/files/ab-update-with-key');
      const remoteScriptPath = `${REMOTE_TMP}/ab-update-with-key`;
      execSync(`scp ${buildScpOptions()} "${localScript}" "${remoteTarget}:${remoteScriptPath}"`, { stdio: 'pipe' });
      execSync(`ssh ${buildSshOptions()} ${remoteTarget} "chmod +x ${remoteScriptPath}"`, { stdio: 'pipe' });
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
      const statusOk = await waitForSystemStatusOk(remoteTarget, remoteHost, 180);
      if (!statusOk) {
        error('SystemStatus check failed after reboot.');
        process.exit(1);
      }
      log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
      success('YOLO update complete!');
      if (specifiedProfile) {
        success(`Profile: ${specifiedProfile}`);
      }
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

  // Clean up profile work directory.
  if (profileWorkDir) {
    try {
      execSync(`rm -rf "${profileWorkDir}"`, { stdio: 'pipe' });
    } catch {
      // Ignore cleanup errors.
    }
  }

  log('');
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
