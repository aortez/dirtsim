#!/usr/bin/env node
/**
 * Build, flash, and verify the dirtsim Yocto image.
 *
 * Usage:
 *   npm run update              # Build, flash, wait for boot
 *   npm run update -- --build-only   # Just build
 *   npm run update -- --flash-only   # Flash and verify (skip build)
 *   npm run update -- --no-verify    # Skip ping/ssh verification
 */

import { execSync, spawn } from 'child_process';
import { dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);

const HOSTNAME = 'dirtsim.local';
const PING_TIMEOUT_SEC = 20;
const SSH_TIMEOUT_SEC = 10;

// Colors for terminal output.
const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m',
};

function log(msg) { console.log(msg); }
function info(msg) { console.log(`${colors.blue}ℹ${colors.reset} ${msg}`); }
function success(msg) { console.log(`${colors.green}✓${colors.reset} ${msg}`); }
function warn(msg) { console.log(`${colors.yellow}⚠${colors.reset} ${msg}`); }
function error(msg) { console.log(`${colors.red}✗${colors.reset} ${msg}`); }

function banner(title) {
  log('');
  log(`${colors.bold}${colors.cyan}╔═══════════════════════════════════════════════════════════════╗${colors.reset}`);
  log(`${colors.bold}${colors.cyan}║  ${title.padEnd(61)}║${colors.reset}`);
  log(`${colors.bold}${colors.cyan}╚═══════════════════════════════════════════════════════════════╝${colors.reset}`);
  log('');
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
 * Run the Yocto build.
 */
async function build() {
  banner('Building dirtsim-image...');
  await run('kas', ['build', 'kas-dirtsim.yml']);
  success('Build complete!');
}

/**
 * Run the flash script.
 */
async function flash() {
  banner('Flashing image...');
  await run('npm', ['run', 'flash']);
  success('Flash complete!');
}

/**
 * Sleep for given milliseconds.
 */
function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * Try to ping the host once.
 */
function tryPing(host) {
  try {
    execSync(`ping -c 1 -W 1 ${host}`, { stdio: 'pipe' });
    return true;
  } catch {
    return false;
  }
}

/**
 * Try to SSH to the host and run a simple command.
 */
function trySSH(host) {
  try {
    execSync(`ssh -o ConnectTimeout=2 -o BatchMode=yes root@${host} echo ok`, {
      stdio: 'pipe',
      timeout: 5000,
    });
    return true;
  } catch {
    return false;
  }
}

/**
 * Wait for the device to respond to ping.
 */
async function waitForPing(host, timeoutSec) {
  banner(`Waiting for ${host} to respond...`);

  const startTime = Date.now();
  const timeoutMs = timeoutSec * 1000;
  let dots = 0;

  while (Date.now() - startTime < timeoutMs) {
    process.stdout.write(`\r  Pinging${'.'.repeat(dots % 4).padEnd(4)}`);
    dots++;

    if (tryPing(host)) {
      process.stdout.write('\r' + ' '.repeat(20) + '\r');
      success(`${host} is responding to ping!`);
      return true;
    }

    await sleep(1000);
  }

  process.stdout.write('\r' + ' '.repeat(20) + '\r');
  warn(`Timeout waiting for ping after ${timeoutSec}s`);
  return false;
}

/**
 * Wait for SSH to become available.
 */
async function waitForSSH(host, timeoutSec) {
  info(`Checking SSH connectivity...`);

  const startTime = Date.now();
  const timeoutMs = timeoutSec * 1000;
  let dots = 0;

  while (Date.now() - startTime < timeoutMs) {
    process.stdout.write(`\r  Trying SSH${'.'.repeat(dots % 4).padEnd(4)}`);
    dots++;

    if (trySSH(host)) {
      process.stdout.write('\r' + ' '.repeat(20) + '\r');
      success(`SSH to ${host} is working!`);
      return true;
    }

    await sleep(1000);
  }

  process.stdout.write('\r' + ' '.repeat(20) + '\r');
  warn(`SSH not available after ${timeoutSec}s (may still be starting)`);
  return false;
}

/**
 * Main entry point.
 */
async function main() {
  const args = process.argv.slice(2);

  const buildOnly = args.includes('--build-only');
  const flashOnly = args.includes('--flash-only');
  const noVerify = args.includes('--no-verify');

  if (args.includes('-h') || args.includes('--help')) {
    log('Usage: npm run update [options]');
    log('');
    log('Options:');
    log('  --build-only   Build the image but don\'t flash');
    log('  --flash-only   Flash existing image (skip build)');
    log('  --no-verify    Skip ping/SSH verification after flash');
    log('  -h, --help     Show this help');
    process.exit(0);
  }

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck Update Tool${colors.reset}`);

  try {
    // Build phase.
    if (!flashOnly) {
      await build();
    }

    // Flash phase.
    if (!buildOnly) {
      await flash();

      // Verification phase.
      if (!noVerify) {
        log('');
        info('Insert the drive into the Pi and power on...');
        info('(Press Ctrl+C to skip verification)');
        log('');

        // Small delay to let user swap the drive.
        await sleep(3000);

        const pingOk = await waitForPing(HOSTNAME, PING_TIMEOUT_SEC);

        if (pingOk) {
          await waitForSSH(HOSTNAME, SSH_TIMEOUT_SEC);
        }
      }
    }

    // Done!
    log('');
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    success('All done!');
    if (!buildOnly) {
      info(`Connect with: ssh dirtsim`);
    }
    log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
    log('');

  } catch (err) {
    log('');
    error(err.message);
    process.exit(1);
  }
}

main();
