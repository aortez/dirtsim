#!/usr/bin/env node
/**
 * Build the Yocto image inside the Docker build container.
 *
 * Usage:
 *   npm run docker-build
 *   npm run docker-build -- --clean
 *   npm run docker-build -- --clean-all
 *   npm run docker-build -- --shell
 */

import { colors, error, info, log, success, warn } from '../pi-base/scripts/lib/index.mjs';
import { ensureYoctoDockerImage, runInYoctoDocker } from './lib/docker-yocto.mjs';

const DEFAULT_CONFIG = 'kas-dirtsim.yml';
const DEFAULT_IMAGE_TARGET = 'dirtsim-image';

function showHelp() {
  log('Usage: npm run docker-build [options]');
  log('');
  log('Build the Yocto image inside the dirtsim-builder Docker image.');
  log('');
  log('Options:');
  log('  --clean            Clean dirtsim-image sstate before build');
  log('  --clean-all        Clean dirtsim-server + dirtsim-audio + dirtsim-image sstate');
  log('  --config           KAS config file (default: kas-dirtsim.yml)');
  log('  --image-target     Image target for cleansstate (default: dirtsim-image)');
  log('  --shell            Start an interactive shell in the container');
  log('  --rebuild-image    Force rebuild of the Docker image');
  log('  --skip-image-build Skip Docker image build if it already exists');
  log('  --no-tty           Disable TTY allocation');
  log('  -h, --help         Show this help');
}

async function runKasBuild(cleanMode, imageRef, tty, kasConfig, imageTarget) {
  if (cleanMode === 'clean') {
    info(`Cleaning ${imageTarget} sstate...`);
    await runInYoctoDocker(
      ['kas', 'shell', kasConfig, '-c', `bitbake -c cleansstate ${imageTarget}`],
      { imageRef, tty }
    );
  } else if (cleanMode === 'clean-all') {
    info(`Cleaning dirtsim-server, dirtsim-audio, and ${imageTarget} sstate...`);
    await runInYoctoDocker(
      [
        'kas',
        'shell',
        kasConfig,
        '-c',
        `bitbake -c cleansstate dirtsim-server dirtsim-audio ${imageTarget}`,
      ],
      { imageRef, tty }
    );
  }

  info(`Building ${imageTarget}...`);
  await runInYoctoDocker(['kas', 'build', kasConfig], { imageRef, tty });
}

async function main() {
  const args = process.argv.slice(2);
  const clean = args.includes('--clean');
  const cleanAll = args.includes('--clean-all');
  const shell = args.includes('--shell');
  const rebuildImage = args.includes('--rebuild-image');
  const skipImageBuild = args.includes('--skip-image-build');
  const noTty = args.includes('--no-tty');
  const configIndex = args.indexOf('--config');
  const imageTargetIndex = args.indexOf('--image-target');

  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    return;
  }

  if (clean && cleanAll) {
    error('Choose either --clean or --clean-all.');
    process.exit(1);
  }

  if (shell && (clean || cleanAll)) {
    warn('Ignoring clean flags when --shell is set.');
  }

  log(`${colors.bold}${colors.cyan}Sparkle Duck Yocto Docker Build${colors.reset}`);

  const imageRef = await ensureYoctoDockerImage({
    rebuild: rebuildImage,
    skipBuild: skipImageBuild,
  });

  if (shell) {
    info(`Starting shell in ${imageRef}...`);
    await runInYoctoDocker(['/bin/bash'], {
      imageRef,
      interactive: true,
      tty: !noTty,
    });
    return;
  }

  let kasConfig = DEFAULT_CONFIG;
  if (configIndex >= 0) {
    if (configIndex === args.length - 1) {
      error('Missing value for --config.');
      process.exit(1);
    }
    kasConfig = args[configIndex + 1];
  }

  let imageTarget = DEFAULT_IMAGE_TARGET;
  if (imageTargetIndex >= 0) {
    if (imageTargetIndex === args.length - 1) {
      error('Missing value for --image-target.');
      process.exit(1);
    }
    imageTarget = args[imageTargetIndex + 1];
  }

  const cleanMode = cleanAll ? 'clean-all' : clean ? 'clean' : null;
  await runKasBuild(cleanMode, imageRef, !noTty, kasConfig, imageTarget);

  success('Docker Yocto build complete.');
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
