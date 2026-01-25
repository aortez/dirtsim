import { copyFileSync, existsSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

import { info, run, runCapture, warn } from '../../pi-base/scripts/lib/index.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(dirname(__dirname));
const RUNTIME_DIR = join(YOCTO_DIR, 'runtime');

const DEFAULTS = {
  kasConfig: 'kas-dirtsim-x86.yml',
  imageTarget: 'dirtsim-x86-image',
  imageName: 'dirtsim-runtime',
  imageTag: 'x86-nightly',
  registryHost: 'oldman-desktop.local:5000',
  registryName: 'dirtsim-registry',
  registryDataDir: '/data/dirtsim-registry',
};

export function getRuntimeDefaults() {
  return { ...DEFAULTS };
}

export function getRuntimePaths({ imageTarget }) {
  return {
    rootfsPath: join(
      YOCTO_DIR,
      'build/tmp/deploy/images/genericx86-64',
      `${imageTarget}-genericx86-64.rootfs.tar.gz`
    ),
    runtimeRootfs: join(RUNTIME_DIR, 'rootfs.tar.gz'),
  };
}

export function getRuntimeImageRefs({ imageName, imageTag, registryHost }) {
  const localImage = `${imageName}:${imageTag}`;
  const registryImage = registryHost ? `${registryHost}/${imageName}:${imageTag}` : null;
  return { localImage, registryImage };
}

export function runtimeImageExists(imageRef) {
  const id = runCapture(`docker images -q ${imageRef}`);
  return Boolean(id);
}

export async function ensureRegistryRunning(registryHost, registryName, registryDataDir) {
  const parts = registryHost.split(':');
  const port = parts.length === 2 && /^\d+$/.test(parts[1]) ? parts[1] : '5000';

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

export async function ensureRootfs({ forceBuild, kasConfig, imageTarget, rootfsPath }) {
  if (!forceBuild && existsSync(rootfsPath)) {
    return;
  }

  info('Building x86 Yocto rootfs...');
  await run(
    'npm',
    ['run', 'docker-build', '--', '--config', kasConfig, '--image-target', imageTarget],
    { cwd: YOCTO_DIR }
  );

  if (!existsSync(rootfsPath)) {
    throw new Error(`Rootfs not found after build: ${rootfsPath}`);
  }
}

export async function buildRuntimeImage({ rootfsPath, runtimeRootfs, localImage }) {
  copyFileSync(rootfsPath, runtimeRootfs);
  await run('docker', ['build', '-t', localImage, RUNTIME_DIR]);
}

export async function publishRuntimeImage(localImage, registryImage) {
  if (!registryImage) {
    throw new Error('Registry host not configured.');
  }
  await run('docker', ['tag', localImage, registryImage]);
  await run('docker', ['push', registryImage]);
}

export async function ensureLocalRuntimeImage(options) {
  const {
    localImage,
    registryImage,
    buildIfMissing,
    publishIfMissing,
    kasConfig,
    imageTarget,
    rootfsPath,
    runtimeRootfs,
  } = options;

  if (runtimeImageExists(localImage)) {
    return;
  }

  if (!registryImage) {
    if (!buildIfMissing) {
      throw new Error(`Local image missing: ${localImage}`);
    }
    warn(`Registry not configured, building locally: ${localImage}`);
    await ensureRootfs({ forceBuild: false, kasConfig, imageTarget, rootfsPath });
    await buildRuntimeImage({ rootfsPath, runtimeRootfs, localImage });
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
    await ensureRootfs({ forceBuild: false, kasConfig, imageTarget, rootfsPath });
    await buildRuntimeImage({ rootfsPath, runtimeRootfs, localImage });
    if (publishIfMissing) {
      info(`Publishing runtime image to ${registryImage}...`);
      await publishRuntimeImage(localImage, registryImage);
    }
  }
}

