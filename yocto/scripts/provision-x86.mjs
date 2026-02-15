#!/usr/bin/env node
/**
 * Provision an x86 machine for DirtSim deployment.
 *
 * One-time setup: creates dirtsim user, working directories, installs
 * systemd service files, and configures SSH access. After provisioning,
 * use ./update.sh --target <host> --fast to deploy binaries.
 *
 * Usage:
 *   npm run provision-x86 -- --target garden.local
 *   npm run provision-x86 -- --target garden.local --dry-run
 *   npm run provision-x86 -- --target garden.local --user myuser
 */

import { execSync } from 'child_process';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { userInfo } from 'os';
import { createConsola } from 'consola';

import {
  colors,
  log,
  info,
  success,
  warn,
  error,
  banner,
  setupConsolaLogging,
} from '../pi-base/scripts/lib/index.mjs';

// Path setup.
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const SERVICE_FILES_DIR = join(YOCTO_DIR, 'meta-dirtsim/recipes-dirtsim/dirtsim/files');

// Set up consola with timestamp reporter.
const timestampReporter = setupConsolaLogging();
const consola = createConsola({ reporters: [timestampReporter] });

// Service files to install on x86 targets.
// The 'file' is the local filename; 'name' is what it gets installed as on the target.
const SERVICES = [
  { file: 'dirtsim-audio.service', name: 'dirtsim-audio.service' },
  { file: 'dirtsim-config-setup-x86.service', name: 'dirtsim-config-setup.service' },
  { file: 'dirtsim-os-manager.service', name: 'dirtsim-os-manager.service' },
  { file: 'dirtsim-server.service', name: 'dirtsim-server.service' },
  { file: 'dirtsim-ui-x86.service', name: 'dirtsim-ui.service' },
];

function runCapture(command) {
  try {
    return execSync(command, { stdio: ['ignore', 'pipe', 'pipe'] })
      .toString()
      .trim();
  } catch {
    return null;
  }
}

/**
 * Run a command on the remote host via SSH.
 * Returns true on success, false on failure. In dry-run mode, just logs.
 */
function sshExec(remoteTarget, command, dryRun) {
  if (dryRun) {
    info(`Would run: ${command}`);
    return true;
  }
  try {
    execSync(`ssh -o ConnectTimeout=10 -o BatchMode=yes ${remoteTarget} "${command}"`, { stdio: 'pipe' });
    return true;
  } catch {
    return false;
  }
}

/**
 * SCP a file to the remote host.
 * Returns true on success, false on failure. In dry-run mode, just logs.
 */
function scpFile(localPath, remoteTarget, remotePath, dryRun) {
  if (dryRun) {
    info(`Would scp: ${localPath} → ${remoteTarget}:${remotePath}`);
    return true;
  }
  try {
    execSync(
      `scp -o ConnectTimeout=10 -o BatchMode=yes "${localPath}" "${remoteTarget}:${remotePath}"`,
      { stdio: 'pipe' },
    );
    return true;
  } catch {
    return false;
  }
}

async function main() {
  const args = process.argv.slice(2).map(arg => arg.replace(/\r/g, ''));
  const dryRun = args.includes('--dry-run');

  const targetIdx = args.indexOf('--target');
  const remoteHost = (targetIdx !== -1 && args[targetIdx + 1]) ? args[targetIdx + 1] : null;

  const userIdx = args.indexOf('--user');
  const remoteUser = (userIdx !== -1 && args[userIdx + 1]) ? args[userIdx + 1] : userInfo().username;

  if (args.includes('-h') || args.includes('--help')) {
    log('Usage: npm run provision-x86 -- --target <host> [options]');
    log('');
    log('One-time setup for x86 DirtSim targets.');
    log('');
    log('Options:');
    log('  --target <host>  Target hostname or IP (required)');
    log('  --user <user>    SSH user on target (default: current user)');
    log('  --dry-run        Show what would happen without doing it');
    log('  -h, --help       Show this help');
    log('');
    log('After provisioning, deploy with:');
    log('  ./update.sh --target <host> --fast');
    process.exit(0);
  }

  if (!remoteHost) {
    error('--target is required');
    error('Usage: npm run provision-x86 -- --target garden.local');
    process.exit(1);
  }

  const remoteTarget = `${remoteUser}@${remoteHost}`;

  log('');
  log(`${colors.bold}${colors.cyan}DirtSim x86 Provisioning${colors.reset}`);
  if (dryRun) {
    log(`${colors.yellow}(dry-run mode - no changes will be made)${colors.reset}`);
  }
  log('');
  info(`Target: ${remoteTarget}`);
  log('');

  // ── Connectivity check ──────────────────────────────────────────────
  banner('Checking connectivity...', consola);

  const pingResult = runCapture(`ping -c 1 -W 2 ${remoteHost}`);
  if (!pingResult) {
    error(`Cannot ping ${remoteHost}`);
    process.exit(1);
  }

  const sshResult = runCapture(
    `ssh -o ConnectTimeout=5 -o BatchMode=yes ${remoteTarget} "echo ok"`,
  );
  if (sshResult !== 'ok') {
    error(`Cannot SSH to ${remoteTarget}`);
    error('Ensure SSH key auth is set up for this host.');
    process.exit(1);
  }
  success(`${remoteHost} is reachable`);

  // ── Create dirtsim user ─────────────────────────────────────────────
  banner('Creating dirtsim user...', consola);

  const userCheck = runCapture(
    `ssh -o ConnectTimeout=5 -o BatchMode=yes ${remoteTarget} "id dirtsim 2>/dev/null && echo exists"`,
  );
  if (userCheck && userCheck.includes('exists')) {
    info('dirtsim user already exists');
  } else {
    if (!sshExec(remoteTarget, 'sudo useradd -m -s /bin/bash -G sudo,video,audio,input dirtsim', dryRun)) {
      error('Failed to create dirtsim user');
      process.exit(1);
    }
    success('dirtsim user created');
  }

  // ── Passwordless sudo for dirtsim ───────────────────────────────────
  banner('Configuring passwordless sudo...', consola);

  const sudoersLine = 'dirtsim ALL=(ALL) NOPASSWD: ALL';
  const sudoersFile = '/etc/sudoers.d/dirtsim';
  if (!sshExec(remoteTarget,
    `echo '${sudoersLine}' | sudo tee ${sudoersFile} > /dev/null && sudo chmod 440 ${sudoersFile}`,
    dryRun)) {
    warn('Failed to configure passwordless sudo (deploy may require password)');
  } else {
    success('Passwordless sudo configured');
  }

  // ── Create directories ──────────────────────────────────────────────
  banner('Creating directories...', consola);

  sshExec(remoteTarget, 'sudo mkdir -p /data/dirtsim /etc/dirtsim', dryRun);
  sshExec(remoteTarget, 'sudo chown -R dirtsim:dirtsim /data/dirtsim', dryRun);
  success('Directories ready');

  // ── Install systemd service files ───────────────────────────────────
  banner('Installing systemd services...', consola);

  for (const svc of SERVICES) {
    const localPath = join(SERVICE_FILES_DIR, svc.file);
    const remoteTmpPath = `/tmp/${svc.name}`;

    if (!scpFile(localPath, remoteTarget, remoteTmpPath, dryRun)) {
      error(`Failed to transfer ${svc.file}`);
      process.exit(1);
    }
    if (!sshExec(remoteTarget, `sudo cp /tmp/${svc.name} /etc/systemd/system/${svc.name}`, dryRun)) {
      error(`Failed to install ${svc.name}`);
      process.exit(1);
    }
    success(`${svc.name}`);
  }

  // ── Dynamic linker compatibility ─────────────────────────────────────
  banner('Setting up dynamic linker...', consola);

  // Yocto x86 binaries expect the linker at /usr/lib/ld-linux-x86-64.so.2
  // but Ubuntu puts it at /lib64/ld-linux-x86-64.so.2. Symlink to bridge.
  sshExec(remoteTarget,
    'sudo ln -sf /lib64/ld-linux-x86-64.so.2 /usr/lib/ld-linux-x86-64.so.2 2>/dev/null || true',
    dryRun);
  success('Dynamic linker symlink created');

  // ── Configure X11 for kiosk-style startup ────────────────────────────
  banner('Configuring X11 (standalone, no display manager)...', consola);

  // Allow non-root users to start X (needed for dirtsim-ui service).
  sshExec(remoteTarget,
    "echo 'allowed_users=anybody' | sudo tee /etc/X11/Xwrapper.config > /dev/null",
    dryRun);
  sshExec(remoteTarget,
    "echo 'needs_root_rights=yes' | sudo tee -a /etc/X11/Xwrapper.config > /dev/null",
    dryRun);
  success('Xwrapper.config updated');

  // Ensure xinit + xset are installed for direct X11 startup and idle timeout control.
  sshExec(remoteTarget, 'sudo apt-get install -y xinit x11-xserver-utils > /dev/null 2>&1', dryRun);
  success('xinit/xset available');

  // Disable and stop display manager — dirtsim-ui starts X directly via xinit.
  // Boot to multi-user (text mode); interactive login still works on tty1-6.
  sshExec(remoteTarget, 'sudo systemctl disable lightdm 2>/dev/null || true', dryRun);
  sshExec(remoteTarget, 'sudo systemctl disable gdm3 2>/dev/null || true', dryRun);
  sshExec(remoteTarget, 'sudo systemctl stop lightdm 2>/dev/null || true', dryRun);
  sshExec(remoteTarget, 'sudo systemctl stop gdm3 2>/dev/null || true', dryRun);
  sshExec(remoteTarget, 'sudo systemctl set-default multi-user.target', dryRun);
  success('Display manager disabled and stopped (text-mode boot)');

  // ── Enable services ─────────────────────────────────────────────────
  banner('Enabling services...', consola);

  const serviceNames = SERVICES.map(s => s.name).join(' ');
  sshExec(remoteTarget, 'sudo systemctl daemon-reload', dryRun);
  sshExec(remoteTarget, `sudo systemctl enable ${serviceNames}`, dryRun);
  success('Services enabled');

  // ── SSH access for dirtsim user ─────────────────────────────────────
  banner('Setting up SSH access for dirtsim user...', consola);

  // Copy the provisioning user's authorized_keys so the same SSH keys work for dirtsim.
  sshExec(remoteTarget, 'sudo mkdir -p /home/dirtsim/.ssh', dryRun);
  sshExec(remoteTarget,
    'sudo cp ~/.ssh/authorized_keys /home/dirtsim/.ssh/authorized_keys 2>/dev/null || true',
    dryRun);
  sshExec(remoteTarget, 'sudo chown -R dirtsim:dirtsim /home/dirtsim/.ssh', dryRun);
  sshExec(remoteTarget, 'sudo chmod 700 /home/dirtsim/.ssh', dryRun);
  sshExec(remoteTarget,
    'sudo chmod 600 /home/dirtsim/.ssh/authorized_keys 2>/dev/null || true',
    dryRun);
  success('SSH access configured for dirtsim user');

  // ── Done ────────────────────────────────────────────────────────────
  log('');
  log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
  success('Provisioning complete!');
  info(`Deploy with: ./update.sh --target ${remoteHost} --fast`);
  log(`${colors.bold}${colors.green}════════════════════════════════════════════════════════════════${colors.reset}`);
  log('');
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
