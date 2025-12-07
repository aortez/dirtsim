#!/usr/bin/env node
/**
 * Sparkle Duck deployment script
 * Syncs code to remote Pi and optionally builds/restarts the service
 */

import { spawn } from 'child_process';
import { parseArgs } from 'util';

const { values: args, positionals } = parseArgs({
  options: {
    host: { type: 'string', short: 'h', description: 'SSH host (user@hostname)' },
    path: { type: 'string', short: 'p', description: 'Remote path to project' },
    'ssh-key': { type: 'string', short: 'i', description: 'Path to SSH private key' },
    build: { type: 'boolean', short: 'b', default: true, description: 'Run build after sync' },
    restart: { type: 'boolean', short: 'r', default: true, description: 'Restart service after build' },
    'build-type': { type: 'string', default: 'release', description: 'Build type (debug/release)' },
    debug: { type: 'boolean', short: 'd', default: false, description: 'Build debug instead of release' },
    cross: { type: 'boolean', short: 'x', default: false, description: 'Cross-compile locally for aarch64' },
    'sync-sysroot': { type: 'boolean', default: false, description: 'Sync sysroot from Pi before cross-compile' },
    'dry-run': { type: 'boolean', short: 'n', default: false, description: 'Show what would be done' },
    local: { type: 'boolean', short: 'l', default: false, description: 'Local deploy (skip rsync, run locally)' },
    help: { type: 'boolean', default: false },
  },
  allowPositionals: true,
});

function usage() {
  console.log(`
Sparkle Duck Deploy Script

Usage: deploy.mjs [options]

Options:
  -h, --host <user@host>   SSH host (required for remote deploy)
  -p, --path <path>        Remote project path (required for remote deploy)
  -i, --ssh-key <path>     SSH private key path
  -b, --build              Run build after sync (default: true)
  -r, --restart            Restart service after build (default: true)
  --build-type <type>      Build type: debug or release (default: release)
  -d, --debug              Build debug instead of release (shortcut for --build-type debug)
  -x, --cross              Cross-compile locally for aarch64 (faster than building on Pi)
  --sync-sysroot           Sync sysroot from Pi before cross-compile
  -n, --dry-run            Show commands without executing
  -l, --local              Local deploy (skip rsync, build and restart locally)
  --help                   Show this help

Examples:
  # Remote deploy (builds on Pi - slow)
  ./deploy.mjs -h oldman@pi5 -p /home/oldman/workspace/sparkle-duck/test-lvgl

  # Cross-compile locally and deploy (fast!)
  ./deploy.mjs -x -h oldman@pi5 -p /home/oldman/workspace/sparkle-duck/test-lvgl

  # First-time cross-compile (sync sysroot from Pi)
  ./deploy.mjs -x --sync-sysroot -h oldman@pi5 -p /home/oldman/workspace/sparkle-duck/test-lvgl

  # Local deploy (on Pi itself)
  ./deploy.mjs --local
`);
  process.exit(0);
}

if (args.help) usage();

if (!args.local && (!args.host || !args.path)) {
  console.error('Error: --host and --path are required for remote deploy (or use --local)');
  usage();
}

function run(cmd, cmdArgs, options = {}) {
  return new Promise((resolve, reject) => {
    const fullCmd = `${cmd} ${cmdArgs.join(' ')}`;

    if (args['dry-run']) {
      console.log(`[dry-run] ${fullCmd}`);
      resolve({ code: 0, stdout: '', stderr: '' });
      return;
    }

    console.log(`\n> ${fullCmd}`);

    const proc = spawn(cmd, cmdArgs, {
      stdio: 'inherit',
      ...options,
    });

    proc.on('close', (code) => {
      if (code === 0) {
        resolve({ code });
      } else {
        reject(new Error(`Command failed with code ${code}: ${fullCmd}`));
      }
    });

    proc.on('error', (err) => {
      reject(new Error(`Failed to execute: ${fullCmd}\n${err.message}`));
    });
  });
}

function sshArgs() {
  const sshOpts = ['-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=accept-new'];
  if (args['ssh-key']) {
    sshOpts.push('-i', args['ssh-key']);
  }
  return sshOpts;
}

// Try to run a command, but don't fail if it errors (used for pause/resume).
async function tryRun(cmd, cmdArgs, options = {}) {
  try {
    await run(cmd, cmdArgs, options);
    return true;
  } catch (err) {
    console.log(`  (command failed, continuing: ${err.message})`);
    return false;
  }
}

async function main() {
  const { host, path, local, cross } = args;
  // --debug flag overrides --build-type.
  const buildType = args.debug ? 'debug' : args['build-type'];

  console.log('=== Sparkle Duck Deploy ===');
  if (local) {
    console.log('Mode: local');
  } else if (cross) {
    console.log('Mode: cross-compile');
    console.log(`Target: ${host}:${path}`);
  } else {
    console.log('Mode: remote build');
    console.log(`Target: ${host}:${path}`);
  }
  console.log(`Build: ${args.build ? buildType : 'skip'}`);
  console.log(`Restart: ${args.restart ? 'yes' : 'no'}`);

  if (local) {
    // Local deployment - no rsync needed.
    const uiAddress = 'ws://localhost:7070';
    const cliPath = './build-debug/bin/cli';

    // Pause simulation before build (if running).
    console.log('\n--- Pausing simulation ---');
    const paused = await tryRun(cliPath, ['sim_pause', uiAddress]);

    if (args.build) {
      console.log('\n--- Building ---');
      await run('make', [buildType]);
    }

    if (args.restart) {
      console.log('\n--- Restarting service ---');
      await run('systemctl', ['--user', 'restart', 'sparkle-duck.service']);
      await run('sleep', ['1']);
      await run('systemctl', ['--user', 'status', 'sparkle-duck.service', '--no-pager']);
    }

    // Resume simulation if we paused it.
    if (paused) {
      console.log('\n--- Resuming simulation ---');
      await tryRun(cliPath, ['sim_run', uiAddress]);
    }
  } else if (cross) {
    // Cross-compilation deployment - build locally for aarch64, deploy binaries.
    const hostname = host.includes('@') ? host.split('@')[1] : host;
    const uiAddress = `ws://${hostname}:7070`;
    const cliPath = './build-debug/bin/cli';

    // Pause simulation before deploy (if running).
    console.log('\n--- Pausing simulation ---');
    const paused = await tryRun(cliPath, ['sim_pause', uiAddress]);

    // Optionally sync sysroot from Pi.
    if (args['sync-sysroot']) {
      console.log('\n--- Syncing sysroot from Pi ---');
      await run('./cmake/aarch64-sysroot-sync.sh', []);
    }

    // Check if sysroot exists.
    const fs = await import('fs');
    if (!fs.existsSync('./sysroot-aarch64')) {
      console.error('\nError: sysroot-aarch64 not found!');
      console.error('Run with --sync-sysroot to create it, or run:');
      console.error('  ./cmake/aarch64-sysroot-sync.sh');
      process.exit(1);
    }

    if (args.build) {
      // Configure and build with cross-toolchain.
      const buildDir = buildType === 'debug' ? 'build-aarch64-debug' : 'build-aarch64-release';
      const cmakeBuildType = buildType === 'debug' ? 'Debug' : 'Release';

      console.log('\n--- Configuring CMake for aarch64 ---');
      await run('cmake', [
        '-B', buildDir,
        '-S', '.',
        '-DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake',
        `-DCMAKE_BUILD_TYPE=${cmakeBuildType}`,
      ]);

      console.log('\n--- Building for aarch64 ---');
      const cpuCount = (await import('os')).cpus().length;
      await run('make', ['-C', buildDir, `-j${cpuCount}`]);

      // Sync just the binaries to the Pi.
      console.log('\n--- Syncing binaries to Pi ---');
      const remoteDir = buildType === 'debug' ? 'build-debug' : 'build-release';
      const rsyncArgs = [
        '-avz', '--delete',
        '-e', `ssh ${sshArgs().join(' ')}`,
        `${buildDir}/bin/`,
        `${host}:${path}/${remoteDir}/bin/`,
      ];
      await run('rsync', rsyncArgs);
    }

    // Restart service.
    if (args.restart) {
      console.log('\n--- Restarting service ---');
      const restartCmd = `systemctl --user restart sparkle-duck.service && sleep 1 && systemctl --user status sparkle-duck.service --no-pager`;
      await run('ssh', [...sshArgs(), host, restartCmd]);
    }

    // Resume simulation if we paused it.
    if (paused) {
      console.log('\n--- Resuming simulation ---');
      await tryRun(cliPath, ['sim_run', uiAddress]);
    }
  } else {
    // Remote deployment - sync source and build on Pi.
    // Extract hostname from user@host format for WebSocket address.
    const hostname = host.includes('@') ? host.split('@')[1] : host;
    const uiAddress = `ws://${hostname}:7070`;

    // Pause simulation before deploy (if running).
    console.log('\n--- Pausing simulation ---');
    const cliPath = './build-debug/bin/cli';
    const paused = await tryRun(cliPath, ['sim_pause', uiAddress]);

    // Step 1: Sync files with rsync.
    console.log('\n--- Syncing files ---');
    const rsyncArgs = [
      '-avz', '--delete',
      '--exclude', 'build-*',
      '--exclude', '.git',
      '--exclude', 'node_modules',
      '--exclude', '*.log',
      '--exclude', '.cache',
      '-e', `ssh ${sshArgs().join(' ')}`,
      './',
      `${host}:${path}/`,
    ];
    await run('rsync', rsyncArgs);

    // Step 2: Build on remote (if requested).
    if (args.build) {
      console.log('\n--- Building ---');
      const buildCmd = `cd ${path} && make ${buildType}`;
      await run('ssh', [...sshArgs(), host, buildCmd]);
    }

    // Step 3: Restart service (if requested).
    if (args.restart) {
      console.log('\n--- Restarting service ---');
      const restartCmd = `systemctl --user restart sparkle-duck.service && sleep 1 && systemctl --user status sparkle-duck.service --no-pager`;
      await run('ssh', [...sshArgs(), host, restartCmd]);
    }

    // Resume simulation if we paused it.
    if (paused) {
      console.log('\n--- Resuming simulation ---');
      await tryRun(cliPath, ['sim_run', uiAddress]);
    }
  }

  console.log('\n=== Deploy complete ===');
}

main().catch((err) => {
  console.error(`\nDeploy failed: ${err.message}`);
  process.exit(1);
});
