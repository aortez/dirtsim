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

function showHelp() {
  log('Usage: npm run docker-build [options]');
  log('');
  log('Build the Yocto image inside the dirtsim-builder Docker image.');
  log('');
  log('Options:');
  log('  --clean            Clean dirtsim-image sstate before build');
  log('  --clean-all        Clean dirtsim-server + dirtsim-image sstate');
  log('  --shell            Start an interactive shell in the container');
  log('  --rebuild-image    Force rebuild of the Docker image');
  log('  --skip-image-build Skip Docker image build if it already exists');
  log('  --no-tty           Disable TTY allocation');
  log('  -h, --help         Show this help');
}

async function runKasBuild(cleanMode, imageRef, tty) {
  if (cleanMode === 'clean') {
    info('Cleaning dirtsim-image sstate...');
    await runInYoctoDocker(
      ['kas', 'shell', DEFAULT_CONFIG, '-c', 'bitbake -c cleansstate dirtsim-image'],
      { imageRef, tty }
    );
  } else if (cleanMode === 'clean-all') {
    info('Cleaning dirtsim-server and dirtsim-image sstate...');
    await runInYoctoDocker(
      ['kas', 'shell', DEFAULT_CONFIG, '-c', 'bitbake -c cleansstate dirtsim-server dirtsim-image'],
      { imageRef, tty }
    );
  }

  info('Building dirtsim-image...');
  await runInYoctoDocker(['kas', 'build', DEFAULT_CONFIG], { imageRef, tty });
}

async function main() {
  const args = process.argv.slice(2);
  const clean = args.includes('--clean');
  const cleanAll = args.includes('--clean-all');
  const shell = args.includes('--shell');
  const rebuildImage = args.includes('--rebuild-image');
  const skipImageBuild = args.includes('--skip-image-build');
  const noTty = args.includes('--no-tty');

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

  const cleanMode = cleanAll ? 'clean-all' : clean ? 'clean' : null;
  await runKasBuild(cleanMode, imageRef, !noTty);

  success('Docker Yocto build complete.');
}

main().catch(err => {
  error(err.message);
  process.exit(1);
});
