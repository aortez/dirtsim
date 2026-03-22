#!/usr/bin/env node

import { spawn } from 'child_process';
import { dirname } from 'path';
import { fileURLToPath } from 'url';

import { resolveKasConfig } from './lib/kas-config.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = dirname(__dirname);

async function main() {
  const args = process.argv.slice(2);
  const configIndex = args.indexOf('--config');

  let kasConfig = 'kas-dirtsim.yml';
  if (configIndex >= 0) {
    if (configIndex === args.length - 1) {
      throw new Error('Missing value for --config');
    }
    kasConfig = args[configIndex + 1];
    args.splice(configIndex, 2);
  }

  const resolvedConfig = resolveKasConfig(kasConfig);
  const proc = spawn('kas', ['build', resolvedConfig, ...args], {
    cwd: YOCTO_DIR,
    stdio: 'inherit',
  });

  const exitCode = await new Promise((resolve, reject) => {
    proc.on('close', resolve);
    proc.on('error', reject);
  });

  if (exitCode !== 0) {
    throw new Error(`kas exited with code ${exitCode}`);
  }
}

main().catch(err => {
  console.error(err.message);
  process.exit(1);
});
