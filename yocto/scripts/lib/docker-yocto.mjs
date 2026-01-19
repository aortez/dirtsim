import { mkdirSync, realpathSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

import { run, runCapture } from '../../pi-base/scripts/lib/index.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = realpathSync(dirname(dirname(__dirname)));
const PROJECT_ROOT = realpathSync(dirname(YOCTO_DIR));
const DOCKER_DIR = join(PROJECT_ROOT, 'docker');
const DOCKER_HOME = join(YOCTO_DIR, '.docker-home');

const IMAGE_NAME = 'dirtsim-builder';

function ensureDockerHome() {
  mkdirSync(DOCKER_HOME, { recursive: true });
}

function ensureDockerAvailable() {
  const version = runCapture('docker version --format "{{.Server.Version}}"');
  if (!version) {
    throw new Error('Docker is not available. Is the Docker daemon running?');
  }
}

function getDockerTag() {
  const tag = runCapture(
    "make -s -C docker --eval='print-tag: ; @echo $(IMAGE_TAG)' print-tag",
    { cwd: PROJECT_ROOT }
  );
  if (!tag) {
    throw new Error('Failed to resolve Docker image tag.');
  }
  return tag;
}

function dockerImageExists(imageRef) {
  const imageId = runCapture(`docker images -q ${imageRef}`);
  return Boolean(imageId);
}

export async function ensureYoctoDockerImage({ rebuild = false, skipBuild = false } = {}) {
  ensureDockerAvailable();
  const tag = getDockerTag();
  const imageRef = `${IMAGE_NAME}:${tag}`;
  const exists = dockerImageExists(imageRef);

  if (skipBuild && !exists) {
    throw new Error(`Docker image not found: ${imageRef}`);
  }

  if (!skipBuild && (rebuild || !exists)) {
    await run('make', ['-C', DOCKER_DIR, 'build-image'], { cwd: PROJECT_ROOT });
  }

  return imageRef;
}

export async function runInYoctoDocker(commandArgs, options = {}) {
  ensureDockerAvailable();
  ensureDockerHome();

  const imageRef = options.imageRef || await ensureYoctoDockerImage();
  const userId = typeof process.getuid === 'function' ? process.getuid() : null;
  const groupId = typeof process.getgid === 'function' ? process.getgid() : null;
  const user = options.user || (userId !== null && groupId !== null ? `${userId}:${groupId}` : null);
  const workdir = options.workdir || YOCTO_DIR;
  const env = options.env || {};
  const cacheRoot = options.cacheRoot || process.env.DIRTSIM_CACHE_ROOT;
  const extraArgs = options.extraArgs || [];
  const interactive = Boolean(options.interactive);
  const tty = Boolean(options.tty);

  const args = ['run', '--rm'];
  if (interactive) {
    args.push('-i');
  }
  if (tty && process.stdout.isTTY) {
    args.push('-t');
  }
  if (user) {
    args.push('--user', user);
  }

  args.push('-v', `${PROJECT_ROOT}:${PROJECT_ROOT}`);
  args.push('-e', `HOME=${DOCKER_HOME}`);
  if (cacheRoot) {
    mkdirSync(cacheRoot, { recursive: true });
    args.push('-v', `${cacheRoot}:${cacheRoot}`);
    if (!env.DIRTSIM_CACHE_ROOT) {
      env.DIRTSIM_CACHE_ROOT = cacheRoot;
    }
  }
  for (const [key, value] of Object.entries(env)) {
    args.push('-e', `${key}=${value}`);
  }
  args.push('-w', workdir);
  args.push(...extraArgs);
  args.push(imageRef, ...commandArgs);

  await run('docker', args);
}
