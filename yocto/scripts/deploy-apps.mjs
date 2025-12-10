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
 *   npm run deploy          # Deploy both server and UI
 *   npm run deploy server   # Deploy server only
 *   npm run deploy ui       # Deploy UI only
 */

import { execSync, spawn } from 'child_process';
import { existsSync } from 'fs';
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const consola = require('consola');

const PI_HOST = 'dirtsim.local';
const PI_USER = 'dirtsim';
const YOCTO_DIR = new URL('..', import.meta.url).pathname;
const BUILD_DIR = `${YOCTO_DIR}build/tmp/work/cortexa76-poky-linux`;

const APPS = {
    server: {
        recipe: 'sparkle-duck-server',
        binary: `${BUILD_DIR}/sparkle-duck-server/git/build/bin/sparkle-duck-server`,
        remotePath: '/usr/bin/sparkle-duck-server',
        service: 'sparkle-duck-server',
    },
    ui: {
        recipe: 'sparkle-duck-ui',
        binary: `${BUILD_DIR}/sparkle-duck-ui/git/build/bin/sparkle-duck-ui`,
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

async function checkPiReachable() {
    try {
        execSync(`ping -c 1 -W 2 ${PI_HOST}`, { stdio: 'pipe' });
        consola.success(`${PI_HOST} is reachable`);
        return true;
    } catch {
        consola.error(`Cannot reach ${PI_HOST}`);
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

    if (!existsSync(app.binary)) {
        consola.error(`Binary not found: ${app.binary}`);
        return false;
    }

    consola.start(`Deploying ${appName}...`);

    try {
        // SCP binary to /tmp/.
        run(`scp ${app.binary} ${PI_USER}@${PI_HOST}:/tmp/`);

        // Stop service, copy binary, start service.
        const remoteCmd = [
            `sudo systemctl stop ${app.service}`,
            `sudo cp /tmp/$(basename ${app.binary}) ${app.remotePath}`,
            `sudo systemctl start ${app.service}`,
        ].join(' && ');

        run(`ssh ${PI_USER}@${PI_HOST} "${remoteCmd}"`);

        consola.success(`${appName} deployed and restarted!`);
        return true;
    } catch (error) {
        consola.error(`Failed to deploy ${appName}: ${error.message}`);
        return false;
    }
}

async function main() {
    const args = process.argv.slice(2);

    // Determine which apps to deploy.
    let apps = ['server', 'ui'];  // Default: both.
    if (args.includes('server') && !args.includes('ui')) {
        apps = ['server'];
    } else if (args.includes('ui') && !args.includes('server')) {
        apps = ['ui'];
    }

    consola.box(`Quick Deploy: ${apps.join(', ')}`);

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
