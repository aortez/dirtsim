import { existsSync } from 'fs';
import { dirname, extname, join } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const YOCTO_DIR = join(__dirname, '..', '..');

function appendUnique(items, value) {
  if (!value || items.includes(value)) {
    return;
  }
  items.push(value);
}

function localOverlayFor(configPath) {
  if (configPath.endsWith('.local.yml') || configPath.endsWith('.local.yaml')) {
    return null;
  }

  const extension = extname(configPath);
  if (!extension) {
    return null;
  }

  return `${configPath.slice(0, -extension.length)}.local${extension}`;
}

export function resolveKasConfig(baseConfig) {
  const explicitConfig = process.env.DIRTSIM_KAS_CONFIG;
  if (explicitConfig) {
    return explicitConfig;
  }

  const resolved = [];
  const baseParts = baseConfig.split(':').filter(Boolean);

  for (const part of baseParts) {
    appendUnique(resolved, part);

    const localOverlay = localOverlayFor(part);
    if (!localOverlay) {
      continue;
    }

    if (existsSync(join(YOCTO_DIR, localOverlay))) {
      appendUnique(resolved, localOverlay);
    }
  }

  const extraConfig = process.env.DIRTSIM_KAS_CONFIG_EXTRA;
  if (extraConfig) {
    for (const part of extraConfig.split(':').filter(Boolean)) {
      appendUnique(resolved, part);
    }
  }

  return resolved.join(':');
}
