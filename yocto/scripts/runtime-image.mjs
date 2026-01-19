#!/usr/bin/env node
/**
 * Build and optionally publish the x86_64 Yocto runtime image.
 *
 * Usage:
 *   npm run runtime-image
 *   npm run runtime-image -- --build
 *   npm run runtime-image -- --ensure --registry oldman-desktop.local:5000
 *   npm run runtime-image -- --publish --ensure-registry --registry oldman-desktop.local:5000
 */

import { copyFileSync, existsSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

import { colors, error, info, log, success, warn, run, runCapture } from '../pi-base/scripts/lib/index.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);
const RUNTIME_DIR = join(YOCTO_DIR, 'runtime');

const DEFAULT_CONFIG = 'kas-dirtsim-x86.yml';
const DEFAULT_IMAGE_TARGET = 'dirtsim-x86-image';
const DEFAULT_IMAGE_NAME = 'dirtsim-runtime';
const DEFAULT_IMAGE_TAG = 'x86-nightly';
const DEFAULT_REGISTRY_HOST = 'oldman-desktop.local:5000';
const DEFAULT_REGISTRY_NAME = 'dirtsim-registry';
const DEFAULT_REGISTRY_DATA_DIR = '/data/dirtsim-registry';

function showHelp() {
  log('Usage: npm run runtime-image [options]');
  log('');
  log('Build and optionally publish the x86_64 Yocto runtime image.');
  log('');
  log('Options:');
  log('  --build              Force Yocto rebuild for the x86 image');
  log('  --ensure             Ensure local image exists (pull from registry if missing)');
  log('  --build-if-missing   Build locally if --ensure pull fails');
  log('  --publish            Push image to registry after building');
  log('  --ensure-registry    Start registry container if needed');
  log('  --config             KAS config file (default: kas-dirtsim-x86.yml)');
  log('  --image-target       Yocto image target (default: dirtsim-x86-image)');
  log('  --image-name         Image name (default: dirtsim-runtime)');
  log('  --image-tag          Image tag (default: x86-nightly)');
  log('  --registry           Registry host:port (default: oldman-desktop.local:5000)');
  log('  --registry-name      Registry container name (default: dirtsim-registry)');
  log('  --registry-data-dir  Registry data dir (default: /data/dirtsim-registry)');
  log('  -h, --help           Show this help');
}

function readArgValue(args, name) {
  const index = args.indexOf(name);
  if (index === -1) {
    return null;
  }
  if (index === args.length - 1) {
    throw new Error(`Missing value for ${name}`);
  }
  return args[index + 1];
}

function dockerImageExists(imageRef) {
  const id = runCapture(`docker images -q ${imageRef}`);
  return Boolean(id);
}

function parseRegistryHost(registryHost) {
  const parts = registryHost.split(':');
  if (parts.length === 2 && /^\d+$/.test(parts[1])) {
    return { host: parts[0], port: parts[1] };
  }
  return { host: registryHost, port: '5000' };
}

async function ensureRegistryRunning(registryHost, registryName, registryDataDir) {
  const { port } = parseRegistryHost(registryHost);
  mkdirSync(registryDataDir, { recursive: true });

  const running = runCapture(`docker ps --filter name=^/${registryName}$ --format "{{.Names}}"`);
  if (running) {
    return;
  }

  const existing = runCapture(`docker ps -a --filter name=^/${registryName}$ --format "{{.Names}}"`);
  if (existing) {
    await run('docker', ['start', registryName]);
    return;
  }

  await run('docker', [
    'run',
    '-d',
    '--restart=always',
    '--name',
    registryName,
    '-p',
    `${port}:5000`,
    '-v',
    `${registryDataDir}:/var/lib/registry`,
    'registry:2',
  ]);
}

async function ensureRootfs(forceBuild, kasConfig, imageTarget, rootfsPath) {
  if (!forceBuild && existsSync(rootfsPath)) {
    return;
  }

  info('Building x86 Yocto rootfs...');
  await run('npm', [
    'run',
    'docker-build',
    '--',
    '--config',
    kasConfig,
    '--image-target',
    imageTarget,
  ], { cwd: YOCTO_DIR });

  if (!existsSync(rootfsPath)) {
    throw new Error(`Rootfs not found after build: ${rootfsPath}`);
  }
}

async function buildRuntimeImage(rootfsPath, localImage) {
  const runtimeRootfs = join(RUNTIME_DIR, 'rootfs.tar.gz');
  copyFileSync(rootfsPath, runtimeRootfs);
  await run('docker', ['build', '-t', localImage, RUNTIME_DIR]);
}

async function ensureLocalImage(options) {
  const {
    localImage,
    registryImage,
    buildIfMissing,
    publishIfMissing,
    kasConfig,
    imageTarget,
    rootfsPath,
  } = options;
  if (dockerImageExists(localImage)) {
    return;
  }
  if (!registryImage) {
    if (!buildIfMissing) {
      throw new Error(`Local image missing: ${localImage}`);
    }
    warn(`Registry not configured, building locally: ${localImage}`);
    await ensureRootfs(false, kasConfig, imageTarget, rootfsPath);
    await buildRuntimeImage(rootfsPath, localImage);
    if (publishIfMissing) {
      warn('Skipping publish: registry host not configured.');
    }
    return;
  }

  info(`Pulling runtime image from ${registryImage}...`);
  try {
    await run('docker', ['pull', registryImage]);
    await run('docker', ['tag', registryImage, localImage]);
  } catch (err) {
    if (!buildIfMissing) {
      throw err;
    }
    warn(`Runtime image missing in registry, building locally: ${localImage}`);
    await ensureRootfs(false, kasConfig, imageTarget, rootfsPath);
    await buildRuntimeImage(rootfsPath, localImage);
    if (publishIfMissing) {
      info(`Publishing runtime image to ${registryImage}...`);
      await publishRuntimeImage(localImage, registryImage);
    }
  }
}

async function publishRuntimeImage(localImage, registryImage) {
  if (!registryImage) {
    throw new Error('Registry host not configured.');
  }
  await run('docker', ['tag', localImage, registryImage]);
  await run('docker', ['push', registryImage]);
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    return;
  }

  const forceBuild = args.includes('--build');
  const ensureOnly = args.includes('--ensure');
  const buildIfMissing =
    args.includes('--build-if-missing')
    || process.env.DIRTSIM_RUNTIME_BUILD_IF_MISSING === '1'
    || process.env.DIRTSIM_RUNTIME_BUILD_IF_MISSING === 'true';
  const publish =
    args.includes('--publish') || process.env.DIRTSIM_RUNTIME_PUBLISH === '1';
  const ensureRegistry = args.includes('--ensure-registry');

  const kasConfig = readArgValue(args, '--config') || DEFAULT_CONFIG;
  const imageTarget = readArgValue(args, '--image-target') || DEFAULT_IMAGE_TARGET;
  const imageName = readArgValue(args, '--image-name') || DEFAULT_IMAGE_NAME;
  const imageTag = readArgValue(args, '--image-tag') || DEFAULT_IMAGE_TAG;
  const registryHost =
    readArgValue(args, '--registry')
    || process.env.DIRTSIM_RUNTIME_REGISTRY
    || DEFAULT_REGISTRY_HOST;
  const registryName = readArgValue(args, '--registry-name') || DEFAULT_REGISTRY_NAME;
  const registryDataDir =
    readArgValue(args, '--registry-data-dir')
    || process.env.DIRTSIM_RUNTIME_REGISTRY_DATA_DIR
    || DEFAULT_REGISTRY_DATA_DIR;

  const localImage = `${imageName}:${imageTag}`;
  const registryImage = registryHost ? `${registryHost}/${imageName}:${imageTag}` : null;
  const rootfsPath = join(
    YOCTO_DIR,
    'build/tmp/deploy/images/genericx86-64',
    `${imageTarget}-genericx86-64.rootfs.tar.gz`,
  );

  log(`${colors.bold}${colors.cyan}DirtSim x86 Runtime Image${colors.reset}`);

  if (ensureOnly) {
    await ensureLocalImage({
      localImage,
      registryImage,
      buildIfMissing,
      publishIfMissing: publish,
      kasConfig,
      imageTarget,
      rootfsPath,
    });
    success(`Runtime image available: ${localImage}`);
    return;
  }

  if (publish && ensureRegistry) {
    info('Ensuring local registry is running...');
    await ensureRegistryRunning(registryHost, registryName, registryDataDir);
  }

  await ensureRootfs(forceBuild, kasConfig, imageTarget, rootfsPath);

  info(`Building runtime image ${localImage}...`);
  await buildRuntimeImage(rootfsPath, localImage);

  if (publish) {
    info(`Publishing runtime image to ${registryImage}...`);
    await publishRuntimeImage(localImage, registryImage);
  }

  success(`Runtime image ready: ${localImage}`);
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
