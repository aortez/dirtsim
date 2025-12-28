#!/usr/bin/env node
/**
 * Flash script for DirtSim Yocto images.
 *
 * Uses shared utilities from sparkle-duck-shared (pi-base submodule).
 *
 * Features:
 * - Flashes Yocto image to USB/SD card.
 * - Injects your SSH public key for passwordless login.
 * - Prompts for WiFi credentials and injects them for first-boot connectivity.
 * - Backs up and restores /data partition from the disk (WiFi credentials, logs, config).
 * - Remembers your key preference in .flash-config.json.
 *
 * Usage:
 *   npm run flash                       # Interactive device selection
 *   npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
 *   npm run flash -- --list             # Just list devices
 *   npm run flash -- --dry-run          # Show what would happen without flashing
 *   npm run flash -- --reconfigure      # Re-select SSH key
 */

import { join, dirname, basename } from 'path';
import { fileURLToPath } from 'url';
import { existsSync } from 'fs';

// Import shared utilities from pi-base.
import {
  colors,
  log,
  info,
  success,
  warn,
  error,
  prompt,
  formatBytes,
  loadConfig,
  saveConfig,
  configureSSHKey,
  injectSSHKey,
  hasDataPartition,
  backupDataPartition,
  restoreDataPartition,
  cleanupBackup,
  setHostname,
  getBlockDevices,
  findLatestImage,
  flashImage,
  getWifiCredentials,
  injectWifiCredentials,
  validateDeviceIdentity,
  isLargeDevice,
} from '../pi-base/scripts/lib/index.mjs';

// Project-specific configuration.
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const IMAGE_DIR = join(YOCTO_DIR, 'build/tmp/deploy/images/raspberrypi-dirtsim');
const CONFIG_FILE = join(YOCTO_DIR, '.flash-config.json');
const WIFI_CREDS_FILE = join(YOCTO_DIR, 'wifi-creds.local');
const DEFAULT_HOSTNAME = 'dirtsim';
const IMAGE_SUFFIX = '.wic.gz';
const PREFERRED_IMAGES = [
  'dirtsim-image-raspberrypi-dirtsim.rootfs.wic.gz',
  'core-image-base-raspberrypi-dirtsim.rootfs.wic.gz',
];

// User configuration - matches what's created in dirtsim-image.bb.
const SSH_USERNAME = 'dirtsim';
const SSH_UID = 1000;

/**
 * Get or create SSH key configuration.
 */
async function ensureSSHKeyConfig(forceReconfigure = false) {
  if (forceReconfigure) {
    return await configureSSHKey(CONFIG_FILE);
  }

  const config = loadConfig(CONFIG_FILE);
  if (config) {
    info(`Using SSH key: ${basename(config.ssh_key_path)}`);
    return config;
  }

  info('No SSH key configured yet.');
  return await configureSSHKey(CONFIG_FILE);
}

function showHelp() {
  console.log(`
DirtSim Yocto Flash Tool

Flash Yocto images to USB/SD cards with SSH key injection.

Usage:
  npm run flash [options]

Options:
  --device <dev>   Flash directly to device (still confirms)
  --interactive    Force interactive prompts (ignore config file)
  --list           List available devices and exit
  --dry-run        Show what would happen without flashing
  --reconfigure    Re-select SSH key
  -h, --help       Show this help

Examples:
  npm run flash                       # Use config file or interactive
  npm run flash -- --interactive      # Force interactive mode
  npm run flash -- --device /dev/sdb  # Direct flash (still confirms)
  npm run flash -- --list             # Just list devices
  npm run flash -- --dry-run          # Preview without flashing

Configuration:
  Defaults can be set in .flash-config.json:
    - device: Auto-select device (e.g., "/dev/sda")
    - device_serial: Stored serial number for device identity verification
    - hostname: Pre-set hostname
    - backup_data: Auto-backup data partition (true/false)
    - skip_confirmation: Skip final confirmation (true/false)

  Copy .flash-config.json.example to .flash-config.json to get started.

Safety Features:
  - Device identity: The serial number is saved after first flash. If a different
    device appears at the same path, you must type "YES" to confirm.
  - Large device warning: Devices larger than 200GB require typing "YES" to flash.
`);
}

async function main() {
  const args = process.argv.slice(2);

  // Handle help.
  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    process.exit(0);
  }

  // Parse simple args.
  const listOnly = args.includes('--list');
  const dryRun = args.includes('--dry-run');
  const reconfigure = args.includes('--reconfigure');
  const interactive = args.includes('--interactive');
  const deviceIndex = args.indexOf('--device');
  const specifiedDevice = deviceIndex !== -1 ? args[deviceIndex + 1] : null;

  log('');
  log(`${colors.bold}${colors.cyan}DirtSim Yocto Flash Tool${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  log('');

  // Ensure we have an SSH key configured.
  const config = await ensureSSHKeyConfig(reconfigure);

  // Find image.
  const image = findLatestImage(IMAGE_DIR, IMAGE_SUFFIX, PREFERRED_IMAGES);
  if (!image) {
    error('No image found. Run "npm run build" first.');
    process.exit(1);
  }

  log('');
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

  // Select device (priority: CLI flag > config file > interactive prompt).
  let targetDevice;
  let selectedDeviceInfo;

  if (specifiedDevice) {
    // Verify specified device is in our list.
    const found = devices.find(d => d.device === specifiedDevice);
    if (!found) {
      error(`Device ${specifiedDevice} not found or not suitable for flashing.`);
      process.exit(1);
    }
    targetDevice = specifiedDevice;
    selectedDeviceInfo = found;
  } else if (!interactive && config.device) {
    // Use device from config file (unless --interactive is set).
    const found = devices.find(d => d.device === config.device);
    if (!found) {
      warn(`Configured device ${config.device} not found, falling back to interactive selection.`);
    } else {
      // Validate device identity using serial number.
      const validation = validateDeviceIdentity(found, config);
      if (!validation.valid) {
        log('');
        warn(`⚠️  Device identity check failed!`);
        log('');

        // Show what matches vs what doesn't.
        const modelMatches = config.device_model && config.device_model === found.model;
        const sizeMatches = config.device_size && config.device_size === found.size;

        if (modelMatches) {
          success(`   Model: ${found.model} ✓`);
        } else if (config.device_model) {
          warn(`   Model: ${found.model} (was: ${config.device_model})`);
        } else {
          info(`   Model: ${found.model}`);
        }

        if (sizeMatches) {
          success(`   Size:  ${found.size} ✓`);
        } else if (config.device_size) {
          warn(`   Size:  ${found.size} (was: ${config.device_size})`);
        } else {
          info(`   Size:  ${found.size}`);
        }

        warn(`   Serial: ${found.serial || 'none'} (was: ${config.device_serial})`);
        log('');

        const confirmSerial = await prompt(`Type "YES" to confirm this is the correct device: `);
        if (confirmSerial !== 'YES') {
          error('Aborted. Device identity not confirmed.');
          process.exit(1);
        }

        // Update the stored identity since user confirmed.
        config.device_serial = found.serial;
        config.device_model = found.model;
        config.device_size = found.size;
        saveConfig(CONFIG_FILE, config);
        info(`Updated stored identity for ${config.device}`);
      }

      targetDevice = config.device;
      selectedDeviceInfo = found;
      info(`Using device from config: ${targetDevice}`);
      if (found.serial) {
        info(`Serial: ${found.serial}`);
      }
    }
  }

  if (!targetDevice) {
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
    selectedDeviceInfo = devices[index];
  }

  // Safety check for large devices (> 200GB).
  if (isLargeDevice(selectedDeviceInfo)) {
    log('');
    warn(`⚠️  WARNING: Large device detected!`);
    warn(`   ${selectedDeviceInfo.device}: ${selectedDeviceInfo.size} (${selectedDeviceInfo.model})`);
    warn(`   This is larger than 200GB - are you sure this isn't your main drive?`);
    log('');

    const confirmLarge = await prompt(`Type "YES" (in caps) to confirm flashing this large device: `);
    if (confirmLarge !== 'YES') {
      error('Aborted. Large device not confirmed.');
      process.exit(1);
    }
  }

  // Get hostname (priority: config file > prompt > default).
  let hostname = (!interactive && config.hostname) ? config.hostname : DEFAULT_HOSTNAME;

  // Prompt for hostname if interactive mode or not specified in config.
  if ((interactive || !config.hostname) && !specifiedDevice && !dryRun) {
    log('');
    const hostnameInput = await prompt(`Device hostname (default: ${hostname}): `);
    if (hostnameInput && hostnameInput.trim()) {
      const cleaned = hostnameInput.trim();
      if (/^[a-zA-Z0-9][a-zA-Z0-9-]*$/.test(cleaned)) {
        hostname = cleaned;
      } else {
        warn(`Invalid hostname "${cleaned}", using default: ${hostname}`);
      }
    }

    // Save hostname to config.
    config.hostname = hostname;
    saveConfig(CONFIG_FILE, config);
  } else if (!interactive && config.hostname) {
    info(`Using hostname from config: ${hostname}`);
  }

  // Get WiFi credentials (from file or prompt, skip if restoring backup).
  let wifiCredentials = null;
  if (!dryRun && !hasDataPartition(targetDevice)) {
    wifiCredentials = await getWifiCredentials(WIFI_CREDS_FILE);
  }

  // Check if we can backup /data from the disk before flashing.
  let backupDir = null;
  if (!dryRun && hasDataPartition(targetDevice)) {
    log('');
    info(`Found existing data partition on ${targetDevice}4`);

    // Use config value if specified (and not in interactive mode), otherwise prompt.
    let shouldBackup = (!interactive && config.backup_data !== undefined) ? config.backup_data : null;

    if (shouldBackup === null) {
      const doBackup = await prompt('Backup /data before flashing? (Y/n): ');
      shouldBackup = doBackup.toLowerCase() !== 'n';
    } else {
      info(`Using backup setting from config: ${shouldBackup ? 'yes' : 'no'}`);
    }

    if (shouldBackup) {
      backupDir = backupDataPartition(targetDevice);
      if (!backupDir) {
        const continueAnyway = await prompt('Continue without backup? (y/N): ');
        if (continueAnyway.toLowerCase() !== 'y') {
          info('Aborted.');
          process.exit(0);
        }
      }
    }
  }

  // Flash!
  try {
    await flashImage(image.path, targetDevice, {
      dryRun,
      bmapPath: existsSync(bmapPath) ? bmapPath : null,
      skipConfirm: (!interactive && config.skip_confirmation) || false,
    });

    // Inject SSH key after flashing.
    await injectSSHKey(targetDevice, config.ssh_key_path, SSH_USERNAME, SSH_UID, dryRun);

    // Set hostname.
    await setHostname(targetDevice, hostname, dryRun);

    // Inject WiFi credentials if provided (and not restoring a backup).
    if (wifiCredentials && !backupDir) {
      await injectWifiCredentials(
        targetDevice,
        wifiCredentials.ssid,
        wifiCredentials.password,
        dryRun
      );
    }

    // Restore /data if we have a backup.
    if (backupDir) {
      restoreDataPartition(targetDevice, backupDir, dryRun);
      cleanupBackup(backupDir);
    }

    log('');
    if (dryRun) {
      success('Dry run complete!');
      info('Run without --dry-run to actually flash.');
    } else {
      // Save device identity for future verification.
      const identityChanged =
        selectedDeviceInfo.serial !== config.device_serial ||
        selectedDeviceInfo.model !== config.device_model ||
        selectedDeviceInfo.size !== config.device_size;

      if (identityChanged) {
        config.device_serial = selectedDeviceInfo.serial;
        config.device_model = selectedDeviceInfo.model;
        config.device_size = selectedDeviceInfo.size;
        saveConfig(CONFIG_FILE, config);
      }

      log(`${colors.bold}${colors.green}═══════════════════════════════════════════════════${colors.reset}`);
      success('Flash complete!');
      if (backupDir) {
        success('/data restored - WiFi credentials preserved!');
      } else if (wifiCredentials) {
        success(`WiFi "${wifiCredentials.ssid}" configured!`);
      }
      log(`${colors.bold}${colors.green}═══════════════════════════════════════════════════${colors.reset}`);
      log('');
      info('You can now eject the drive and boot your Raspberry Pi.');
      info(`Login: ssh ${SSH_USERNAME}@${hostname}.local`);
      info(`SSH key: ${basename(config.ssh_key_path)}`);
    }
  } catch (err) {
    // Clean up backup on failure.
    cleanupBackup(backupDir);
    log('');
    error(`Flash failed: ${err.message}`);
    process.exit(1);
  }
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
