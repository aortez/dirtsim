#!/usr/bin/env node
/**
 * Smoke test for a deployed DirtSim device.
 *
 * Runs StatusGet against server and UI via the deployed dirtsim-cli,
 * and verifies the responses look healthy.
 */

import { checkRemoteReachable, ssh } from '../pi-base/scripts/lib/index.mjs';

const DEFAULT_HOST = 'dirtsim.local';
const DEFAULT_USER = 'dirtsim';
const DEFAULT_RETRIES = 20;
const DEFAULT_DELAY_SEC = 5;
const DEFAULT_TIMEOUT_MS = 5000;

function showHelp() {
  console.log(`
Smoke Test - Verify DirtSim services via deployed CLI

Usage:
  node scripts/smoke-test.mjs [options]

Options:
  --target <host>     Target hostname (default: ${DEFAULT_HOST})
  --user <user>       SSH user (default: ${DEFAULT_USER})
  --retries <count>   Number of retries per component (default: ${DEFAULT_RETRIES})
  --delay <seconds>   Delay between retries (default: ${DEFAULT_DELAY_SEC})
  --timeout <ms>      CLI timeout per StatusGet (default: ${DEFAULT_TIMEOUT_MS})
  -h, --help          Show this help
`);
}

function fail(message) {
  console.error(`Error: ${message}`);
  process.exit(1);
}

function parseIntArg(value, name, minValue) {
  const parsed = Number.parseInt(value, 10);
  if (Number.isNaN(parsed) || parsed < minValue) {
    fail(`${name} must be an integer >= ${minValue}`);
  }
  return parsed;
}

function parseFloatArg(value, name, minValue) {
  const parsed = Number.parseFloat(value);
  if (!Number.isFinite(parsed) || parsed < minValue) {
    fail(`${name} must be a number >= ${minValue}`);
  }
  return parsed;
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function runStatusGet(remoteTarget, component, timeoutMs) {
  const command = `/usr/bin/dirtsim-cli ${component} StatusGet --timeout ${timeoutMs}`;
  const output = ssh(remoteTarget, command, { timeout: 10 });

  if (!output) {
    return { ok: false, error: 'command failed or returned no output' };
  }

  let parsed;
  try {
    parsed = JSON.parse(output);
  } catch (err) {
    const lines = output.split('\n').map(line => line.trim()).filter(Boolean);
    if (lines.length > 0) {
      try {
        parsed = JSON.parse(lines[lines.length - 1]);
      } catch (innerErr) {
        return {
          ok: false,
          error: `invalid JSON output: ${innerErr.message}`,
          raw: output
        };
      }
    } else {
      return { ok: false, error: `invalid JSON output: ${err.message}`, raw: output };
    }
  }

  if (parsed.error) {
    return { ok: false, error: parsed.error, raw: output };
  }

  if (!parsed.value) {
    return { ok: false, error: 'missing value in response', raw: output };
  }

  if (component === 'server' && parsed.value.state === 'Error') {
    const msg = parsed.value.error_message || 'server state is Error';
    return { ok: false, error: msg, raw: output };
  }

  if (component === 'ui' && parsed.value.connected_to_server !== true) {
    return { ok: false, error: 'ui not connected to server', raw: output };
  }

  return { ok: true, value: parsed.value };
}

async function waitForStatus(remoteTarget, component, retries, delaySec, timeoutMs) {
  for (let attempt = 1; attempt <= retries; attempt++) {
    const result = runStatusGet(remoteTarget, component, timeoutMs);
    if (result.ok) {
      return result.value;
    }

    const suffix = attempt < retries ? ', retrying...' : '';
    console.log(
      `[${component}] attempt ${attempt}/${retries} failed: ${result.error}${suffix}`
    );

    if (attempt < retries) {
      await sleep(delaySec * 1000);
    }
  }

  return null;
}

async function main() {
  const args = process.argv.slice(2);
  let host = DEFAULT_HOST;
  let user = DEFAULT_USER;
  let retries = DEFAULT_RETRIES;
  let delaySec = DEFAULT_DELAY_SEC;
  let timeoutMs = DEFAULT_TIMEOUT_MS;

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    switch (arg) {
      case '--target':
      case '--host':
        if (i + 1 >= args.length) {
          fail(`${arg} requires a hostname`);
        }
        host = args[++i];
        break;
      case '--user':
        if (i + 1 >= args.length) {
          fail(`${arg} requires a username`);
        }
        user = args[++i];
        break;
      case '--retries':
        if (i + 1 >= args.length) {
          fail(`${arg} requires a value`);
        }
        retries = parseIntArg(args[++i], '--retries', 1);
        break;
      case '--delay':
        if (i + 1 >= args.length) {
          fail(`${arg} requires a value`);
        }
        delaySec = parseFloatArg(args[++i], '--delay', 0);
        break;
      case '--timeout':
        if (i + 1 >= args.length) {
          fail(`${arg} requires a value`);
        }
        timeoutMs = parseIntArg(args[++i], '--timeout', 1);
        break;
      case '-h':
      case '--help':
        showHelp();
        return;
      default:
        fail(`Unknown argument: ${arg}`);
    }
  }

  const remoteTarget = `${user}@${host}`;

  console.log(`Checking reachability for ${remoteTarget}...`);
  if (!checkRemoteReachable(host, remoteTarget)) {
    fail(`${host} is not reachable via ping/ssh`);
  }

  console.log('Running smoke test via dirtsim-cli StatusGet...');

  const serverStatus = await waitForStatus(
    remoteTarget,
    'server',
    retries,
    delaySec,
    timeoutMs
  );
  if (!serverStatus) {
    fail('server StatusGet failed');
  }
  console.log(`Server OK (state=${serverStatus.state})`);

  const uiStatus = await waitForStatus(remoteTarget, 'ui', retries, delaySec, timeoutMs);
  if (!uiStatus) {
    fail('ui StatusGet failed');
  }
  console.log(
    `UI OK (state=${uiStatus.state}, connected_to_server=${uiStatus.connected_to_server})`
  );

  console.log('Smoke test passed.');
}

main().catch(err => {
  fail(err.message);
});
