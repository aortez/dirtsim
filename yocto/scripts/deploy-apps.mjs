#!/usr/bin/env node
/**
 * Quick deploy script for userspace applications.
 *
 * Rebuilds sparkle-duck-server and sparkle-duck-ui via Yocto cross-compilation,
 * then SCPs the binaries to the Pi and restarts the services.
 *
 * Much faster than a full YOLO update (~60-90s vs 3+ minutes).
 *
 * Usage:
 *   npm run deploy                              # Deploy both server and UI
 *   npm run deploy server                       # Deploy server only
 *   npm run deploy ui                           # Deploy UI only
 *   npm run deploy -- --host dirtsim-clock.local  # Deploy to specific host
 */

import { execSync, spawn } from 'child_process';
import { existsSync } from 'fs';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const consola = require('consola');

const DEFAULT_HOST = 'dirtsim.local';
const PI_USER = 'dirtsim';

// Parsed from command line.
let piHost = DEFAULT_HOST;
const YOCTO_DIR = new URL('..', import.meta.url).pathname;
const WORK_DIR = `${YOCTO_DIR}build/tmp/work`;

// Supported architectures (Pi5=cortexa76, Pi4=cortexa72).
const ARCH_PATTERNS = ['cortexa76-poky-linux', 'cortexa72-poky-linux'];

/**
 * Find the most recently built binary for a recipe across all architectures.
 */
function findBinary(recipe, binaryName) {
    const { statSync } = require('fs');
    let newest = null;
    let newestTime = 0;

    for (const arch of ARCH_PATTERNS) {
        const path = `${WORK_DIR}/${arch}/${recipe}/git/build/bin/${binaryName}`;
        if (existsSync(path)) {
            const mtime = statSync(path).mtimeMs;
            if (mtime > newestTime) {
                newestTime = mtime;
                newest = path;
            }
        }
    }
    return newest;
}

const APPS = {
    server: {
        recipe: 'sparkle-duck-server',
        binaryName: 'sparkle-duck-server',
        remotePath: '/usr/bin/sparkle-duck-server',
        service: 'sparkle-duck-server',
    },
    ui: {
        recipe: 'sparkle-duck-ui',
        binaryName: 'sparkle-duck-ui',
        remotePath: '/usr/bin/sparkle-duck-ui',
        service: 'sparkle-duck-ui',
    },
};

function run(cmd, options = {}) {
    consola.info(`Running: ${cmd}`);
    return execSync(cmd, { stdio: 'inherit', ...options });
}

function runQuiet(cmd) {
    return execSync(cmd, { encoding: 'utf-8' }).trim();
}

function showHelp() {
    console.log(`
Quick Deploy - Deploy sparkle-duck apps to a Raspberry Pi

Usage:
  npm run deploy [options] [apps]

Apps:
  server         Deploy server only
  ui             Deploy UI only
  (default)      Deploy both server and UI

Options:
  --host <host>  Target hostname (default: ${DEFAULT_HOST})
  -h, --help     Show this help

Examples:
  npm run deploy                                # Deploy both to dirtsim.local
  npm run deploy server                         # Deploy server only
  npm run deploy -- --host dirtsim-clock.local  # Deploy to different host
  npm run deploy -- --host dirtsim-clock.local ui  # Deploy UI to different host
`);
}

async function checkPiReachable() {
    try {
        execSync(`ping -c 1 -W 2 ${piHost}`, { stdio: 'pipe' });
        consola.success(`${piHost} is reachable`);
        return true;
    } catch {
        consola.error(`Cannot reach ${piHost}`);
        return false;
    }
}

async function buildApps(apps) {
    const recipes = apps.map(a => APPS[a].recipe).join(' ');

    consola.start(`Building ${recipes}...`);

    // Force recompile and rebuild.
    const cmd = `kas shell kas-dirtsim.yml -c "bitbake ${recipes} -c compile -f && bitbake ${recipes}"`;

    try {
        run(cmd, { cwd: YOCTO_DIR });
        consola.success('Build complete!');
        return true;
    } catch (error) {
        consola.error('Build failed!');
        return false;
    }
}

async function deployApp(appName) {
    const app = APPS[appName];

    const binary = findBinary(app.recipe, app.binaryName);
    if (!binary) {
        consola.error(`Binary not found for ${appName} in any architecture: ${ARCH_PATTERNS.join(', ')}`);
        return false;
    }

    consola.start(`Deploying ${appName} from ${binary}...`);

    try {
        // SCP binary to /tmp/.
        run(`scp ${binary} ${PI_USER}@${piHost}:/tmp/`);

        // Stop service, copy binary, start service.
        const remoteCmd = [
            `sudo systemctl stop ${app.service}`,
            `sudo cp /tmp/${app.binaryName} ${app.remotePath}`,
            `sudo systemctl start ${app.service}`,
        ].join(' && ');

        run(`ssh ${PI_USER}@${piHost} "${remoteCmd}"`);

        consola.success(`${appName} deployed and restarted!`);
        return true;
    } catch (error) {
        consola.error(`Failed to deploy ${appName}: ${error.message}`);
        return false;
    }
}

async function main() {
    const args = process.argv.slice(2);

    // Handle help.
    if (args.includes('-h') || args.includes('--help')) {
        showHelp();
        process.exit(0);
    }

    // Parse --host option.
    const hostIndex = args.indexOf('--host');
    if (hostIndex !== -1) {
        if (hostIndex + 1 >= args.length) {
            consola.error('--host requires a hostname argument');
            process.exit(1);
        }
        piHost = args[hostIndex + 1];
    }

    // Determine which apps to deploy.
    let apps = ['server', 'ui'];  // Default: both.
    if (args.includes('server') && !args.includes('ui')) {
        apps = ['server'];
    } else if (args.includes('ui') && !args.includes('server')) {
        apps = ['ui'];
    }

    consola.box(`Quick Deploy: ${apps.join(', ')} → ${piHost}`);

    // Check Pi is reachable.
    if (!await checkPiReachable()) {
        process.exit(1);
    }

    // Build apps.
    const startTime = Date.now();
    if (!await buildApps(apps)) {
        process.exit(1);
    }

    // Deploy each app.
    for (const app of apps) {
        if (!await deployApp(app)) {
            process.exit(1);
        }
    }

    const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
    consola.success(`Done in ${elapsed}s!`);
}

main().catch(err => {
    consola.error(err);
    process.exit(1);
});
