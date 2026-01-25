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

import { colors, error, info, log, success } from '../pi-base/scripts/lib/index.mjs';
import {
  buildRuntimeImage,
  ensureLocalRuntimeImage,
  ensureRegistryRunning,
  ensureRootfs,
  getRuntimeDefaults,
  getRuntimeImageRefs,
  getRuntimePaths,
  publishRuntimeImage,
} from './lib/runtime-image.mjs';

const defaults = getRuntimeDefaults();

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
  log(`  --config             KAS config file (default: ${defaults.kasConfig})`);
  log(`  --image-target       Yocto image target (default: ${defaults.imageTarget})`);
  log(`  --image-name         Image name (default: ${defaults.imageName})`);
  log(`  --image-tag          Image tag (default: ${defaults.imageTag})`);
  log(`  --registry           Registry host:port (default: ${defaults.registryHost})`);
  log(`  --registry-name      Registry container name (default: ${defaults.registryName})`);
  log(`  --registry-data-dir  Registry data dir (default: ${defaults.registryDataDir})`);
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

  const kasConfig = readArgValue(args, '--config') || defaults.kasConfig;
  const imageTarget = readArgValue(args, '--image-target') || defaults.imageTarget;
  const imageName = readArgValue(args, '--image-name') || defaults.imageName;
  const imageTag = readArgValue(args, '--image-tag') || defaults.imageTag;
  const registryHost =
    readArgValue(args, '--registry')
    || process.env.DIRTSIM_RUNTIME_REGISTRY
    || defaults.registryHost;
  const registryName = readArgValue(args, '--registry-name') || defaults.registryName;
  const registryDataDir =
    readArgValue(args, '--registry-data-dir')
    || process.env.DIRTSIM_RUNTIME_REGISTRY_DATA_DIR
    || defaults.registryDataDir;

  const { localImage, registryImage } = getRuntimeImageRefs({
    imageName,
    imageTag,
    registryHost,
  });
  const { rootfsPath, runtimeRootfs } = getRuntimePaths({ imageTarget });

  log(`${colors.bold}${colors.cyan}DirtSim x86 Runtime Image${colors.reset}`);

  if (ensureOnly) {
    await ensureLocalRuntimeImage({
      localImage,
      registryImage,
      buildIfMissing,
      publishIfMissing: publish,
      kasConfig,
      imageTarget,
      rootfsPath,
      runtimeRootfs,
    });
    success(`Runtime image available: ${localImage}`);
    return;
  }

  if (publish && ensureRegistry) {
    info('Ensuring local registry is running...');
    await ensureRegistryRunning(registryHost, registryName, registryDataDir);
  }

  await ensureRootfs({ forceBuild, kasConfig, imageTarget, rootfsPath });

  info(`Building runtime image ${localImage}...`);
  await buildRuntimeImage({ rootfsPath, runtimeRootfs, localImage });

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
