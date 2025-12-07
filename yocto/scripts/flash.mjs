#!/usr/bin/env node
/**
 * Flash script for Sparkle Duck Yocto images.
 *
 * Usage:
 *   npm run flash                       # Interactive device selection
 *   npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
 *   npm run flash -- --list             # Just list devices
 *   npm run flash -- --dry-run          # Show what would happen without flashing
 */

import { execSync, spawn } from 'child_process';
import { existsSync, readdirSync, statSync, readlinkSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { createInterface } from 'readline';

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

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const IMAGE_DIR = join(YOCTO_DIR, 'build/tmp/deploy/images/raspberrypi5');

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
      // Follow symlinks to get real file for mtime.
      stat: statSync(join(IMAGE_DIR, f)),
    }))
    .sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);

  // Prefer our custom image, fall back to core-image-base.
  const dirtsimImage = files.find(f => f.name === 'dirtsim-image-raspberrypi5.rootfs.wic.gz');
  if (dirtsimImage) {
    return dirtsimImage;
  }
  const coreImage = files.find(f => f.name === 'core-image-base-raspberrypi5.rootfs.wic.gz');
  if (coreImage) {
    return coreImage;
  }

  return files[0] || null;
}

/**
 * Get list of block devices suitable for flashing.
 * Returns removable devices and excludes the system disk.
 */
function getBlockDevices() {
  try {
    const output = execSync('lsblk -d -o NAME,SIZE,TYPE,RM,TRAN,MODEL -J', {
      encoding: 'utf-8',
    });
    const data = JSON.parse(output);

    return data.blockdevices
      .filter(dev => {
        // Only disk types.
        if (dev.type !== 'disk') return false;
        // Skip loop devices.
        if (dev.name.startsWith('loop')) return false;
        // Skip nvme (usually system disk).
        if (dev.name.startsWith('nvme')) return false;
        // Prefer removable (RM=1) or USB transport.
        return dev.rm === true || dev.rm === '1' || dev.tran === 'usb';
      })
      .map(dev => ({
        device: `/dev/${dev.name}`,
        size: dev.size,
        model: dev.model || 'Unknown',
        transport: dev.tran || 'unknown',
        removable: dev.rm === true || dev.rm === '1',
      }));
  } catch (err) {
    error(`Failed to list block devices: ${err.message}`);
    return [];
  }
}

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
 * Check if bmaptool is available.
 */
function hasBmaptool() {
  try {
    execSync('which bmaptool', { encoding: 'utf-8', stdio: 'pipe' });
    return true;
  } catch {
    return false;
  }
}

/**
 * Flash the image to the device.
 */
async function flashImage(imagePath, bmapPath, device, dryRun = false) {
  const useBmap = hasBmaptool() && existsSync(bmapPath);

  log('');
  if (dryRun) {
    log(`${colors.bold}${colors.yellow}═══════════════════════════════════════════════════${colors.reset}`);
    log(`${colors.bold}${colors.yellow}  DRY RUN - No changes will be made${colors.reset}`);
    log(`${colors.bold}${colors.yellow}═══════════════════════════════════════════════════${colors.reset}`);
  } else {
    log(`${colors.bold}${colors.red}═══════════════════════════════════════════════════${colors.reset}`);
    log(`${colors.bold}${colors.red}  WARNING: This will ERASE ALL DATA on ${device}${colors.reset}`);
    log(`${colors.bold}${colors.red}═══════════════════════════════════════════════════${colors.reset}`);
  }
  log('');

  info(`Image: ${imagePath}`);
  info(`Target: ${device}`);
  info(`Method: ${useBmap ? 'bmaptool (fast)' : 'dd (slower)'}`);
  log('');

  if (dryRun) {
    info('Dry run complete. Would execute:');
    log('');
    if (useBmap) {
      log(`  sudo umount ${device}* 2>/dev/null || true`);
      log(`  sudo bmaptool copy --bmap "${bmapPath}" "${imagePath}" "${device}"`);
    } else {
      log(`  sudo umount ${device}* 2>/dev/null || true`);
      log(`  gunzip -c "${imagePath}" | sudo dd of="${device}" bs=4M status=progress conv=fsync`);
    }
    log(`  sync`);
    log('');
    return;
  }

  const confirm = await prompt(`Type "${device}" to confirm: `);
  if (confirm !== device) {
    error('Confirmation failed. Aborting.');
    process.exit(1);
  }

  log('');

  // Unmount any partitions on the device.
  try {
    info('Unmounting any mounted partitions...');
    execSync(`sudo umount ${device}* 2>/dev/null || true`, { stdio: 'inherit' });
  } catch {
    // Ignore unmount errors.
  }

  if (useBmap) {
    // Use bmaptool for faster flashing.
    const cmd = `sudo bmaptool copy --bmap "${bmapPath}" "${imagePath}" "${device}"`;
    info(`Running: ${cmd}`);
    log('');

    const proc = spawn('sudo', [
      'bmaptool', 'copy',
      '--bmap', bmapPath,
      imagePath,
      device,
    ], { stdio: 'inherit' });

    await new Promise((resolve, reject) => {
      proc.on('close', code => {
        if (code === 0) {
          resolve();
        } else {
          reject(new Error(`bmaptool exited with code ${code}`));
        }
      });
    });
  } else {
    // Fall back to dd.
    const cmd = `gunzip -c "${imagePath}" | sudo dd of="${device}" bs=4M status=progress conv=fsync`;
    info(`Running: ${cmd}`);
    log('');

    const proc = spawn('sh', ['-c', cmd], { stdio: 'inherit' });

    await new Promise((resolve, reject) => {
      proc.on('close', code => {
        if (code === 0) {
          resolve();
        } else {
          reject(new Error(`dd exited with code ${code}`));
        }
      });
    });
  }

  // Final sync to ensure all writes are flushed.
  info('Syncing...');
  execSync('sync', { stdio: 'inherit' });
}

/**
 * Main entry point.
 */
async function main() {
  const args = process.argv.slice(2);

  // Parse simple args.
  const listOnly = args.includes('--list');
  const dryRun = args.includes('--dry-run');
  const deviceIndex = args.indexOf('--device');
  const specifiedDevice = deviceIndex !== -1 ? args[deviceIndex + 1] : null;

  log('');
  log(`${colors.bold}${colors.cyan}Sparkle Duck Yocto Flash Tool${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  log('');

  // Find image.
  const image = findLatestImage();
  if (!image) {
    error('No image found. Run "kas build kas-dirtsim.yml" first.');
    process.exit(1);
  }

  info(`Image: ${image.name}`);
  info(`Size: ${formatBytes(image.stat.size)}`);
  info(`Built: ${image.stat.mtime.toLocaleString()}`);

  // Check for bmap file.
  const bmapPath = image.path.replace('.wic.gz', '.wic.bmap');
  if (existsSync(bmapPath)) {
    info(`Bmap: available (faster flashing)`);
  }

  log('');

  // List devices.
  const devices = getBlockDevices();

  if (devices.length === 0) {
    warn('No suitable devices found.');
    warn('Insert an SD card or USB drive and try again.');
    process.exit(1);
  }

  log(`${colors.bold}Available devices:${colors.reset}`);
  log('');
  devices.forEach((dev, i) => {
    const rmBadge = dev.removable ? `${colors.green}[removable]${colors.reset}` : '';
    log(`  ${colors.cyan}${i + 1})${colors.reset} ${dev.device}  ${dev.size}  ${dev.model}  ${rmBadge}`);
  });
  log('');

  if (listOnly) {
    process.exit(0);
  }

  // Select device.
  let targetDevice;

  if (specifiedDevice) {
    // Verify specified device is in our list.
    const found = devices.find(d => d.device === specifiedDevice);
    if (!found) {
      error(`Device ${specifiedDevice} not found or not suitable for flashing.`);
      process.exit(1);
    }
    targetDevice = specifiedDevice;
  } else {
    // Interactive selection.
    const choice = await prompt(`Select device (1-${devices.length}) or 'q' to quit: `);

    if (choice.toLowerCase() === 'q') {
      info('Aborted.');
      process.exit(0);
    }

    const index = parseInt(choice, 10) - 1;
    if (isNaN(index) || index < 0 || index >= devices.length) {
      error('Invalid selection.');
      process.exit(1);
    }

    targetDevice = devices[index].device;
  }

  // Flash!
  try {
    await flashImage(image.path, bmapPath, targetDevice, dryRun);
    log('');
    if (dryRun) {
      success('Dry run complete!');
      info('Run without --dry-run to actually flash.');
    } else {
      success('Flash complete!');
      info('You can now eject the SD card and boot your Raspberry Pi.');
      info('Default login: root (no password)');
      info('Hostname: dirtsim');
    }
  } catch (err) {
    log('');
    error(`Flash failed: ${err.message}`);
    process.exit(1);
  }
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
