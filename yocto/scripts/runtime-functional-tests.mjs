#!/usr/bin/env node
/**
 * Run DirtSim CLI functional tests inside the x86 runtime Docker image.
 */

import { error, info, log, run, success, warn } from '../pi-base/scripts/lib/index.mjs';
import {
  buildRuntimeImage,
  ensureLocalRuntimeImage,
  ensureRootfs,
  getRuntimeDefaults,
  getRuntimeImageRefs,
  getRuntimePaths,
  runtimeImageExists,
} from './lib/runtime-image.mjs';

const DEFAULT_TIMEOUT_MS = 15000;
const DEFAULT_TESTS = [
  'canTrain',
  'canSetGenerationsAndTrain',
  'canPlantTreeSeed',
];
const ALL_TESTS = [
  'canExit',
  ...DEFAULT_TESTS,
  'canLoadGenomeFromBrowser',
  'canOpenTrainingConfigPanel',
];

const defaults = getRuntimeDefaults();

function showHelp() {
  log('Usage: npm run runtime-functional-tests -- [options]');
  log('');
  log('Run DirtSim functional tests inside the x86 runtime Docker container.');
  log('');
  log('Options:');
  log('  --test <name>         Run a single test (repeatable).');
  log('  --tests <a,b,c>       Comma-separated test list.');
  log('  --all                 Run the full test list.');
  log(`  --timeout <ms>        CLI timeout per test (default: ${DEFAULT_TIMEOUT_MS}).`);
  log(`  --config <file>       KAS config (default: ${defaults.kasConfig}).`);
  log(`  --image-target <name> Yocto image target (default: ${defaults.imageTarget}).`);
  log(`  --image-name <name>   Image name (default: ${defaults.imageName}).`);
  log(`  --image-tag <tag>     Image tag (default: ${defaults.imageTag}).`);
  log(`  --registry <host>     Registry host (default: ${defaults.registryHost}).`);
  log('  --image <ref>         Full image ref (overrides name/tag).');
  log('  --ensure              Ensure runtime image exists (pull from registry if missing).');
  log('  --build               Force local runtime image build.');
  log('  --build-if-missing    Build locally if pull fails (requires --ensure).');
  log('  --data-dir <path>     Bind mount host path to /data/dirtsim.');
  log('  --publish-ports       Publish 7070/8080/9090 to the host.');
  log('  --disable-xvfb        Disable Xvfb in the container.');
  log('  -h, --help            Show this help.');
  log('');
  log(`Valid tests: ${ALL_TESTS.join(', ')}`);
}

function fail(message) {
  error(message);
  process.exit(1);
}

function parseIntArg(value, name, minValue) {
  const parsed = Number.parseInt(value, 10);
  if (Number.isNaN(parsed) || parsed < minValue) {
    fail(`${name} must be an integer >= ${minValue}`);
  }
  return parsed;
}

function parseTestsArg(value) {
  return value
    .split(',')
    .map(item => item.trim())
    .filter(Boolean);
}

function resolveTests({ tests, useAll }) {
  const selected = tests.length > 0 ? tests : (useAll ? ALL_TESTS : DEFAULT_TESTS);
  const invalid = selected.filter(name => !ALL_TESTS.includes(name));
  if (invalid.length > 0) {
    fail(`Unknown functional test(s): ${invalid.join(', ')}`);
  }
  return selected;
}

function buildContainerScript(testName, timeoutMs) {
  const testArgs =
    `--timeout ${timeoutMs} ` +
    '--os-manager-address ws://localhost:9090 ' +
    '--ui-address ws://localhost:7070 ' +
    '--server-address ws://localhost:8080';

  const lines = [
    'set -u',
    `TEST_NAME="${testName}"`,
    'dirtsim-os-manager -p 9090 --backend local &',
    'OS_MANAGER_PID=$!',
    'cleanup() {',
    '  kill "$OS_MANAGER_PID" 2>/dev/null || true',
    '}',
    'trap cleanup EXIT',
    'ready=""',
    'for i in $(seq 1 50); do',
    '  if dirtsim-cli --address ws://localhost:9090 os-manager SystemStatus >/dev/null 2>&1; then',
    '    ready=1',
    '    break',
    '  fi',
    '  sleep 0.2',
    'done',
    'if [ -z "$ready" ]; then',
    '  echo "Timed out waiting for os-manager at ws://localhost:9090" >&2',
    '  exit 1',
    'fi',
    'echo "Running functional test: $TEST_NAME"',
    `result=$(dirtsim-cli functional-test "$TEST_NAME" ${testArgs} 2>&1)`,
    'status=$?',
    'printf "%s\\n" "$result"',
    'if [ $status -ne 0 ]; then',
    '  if printf "%s" "$result" | grep -q "unknown functional test"; then',
    '    echo "Skipping unsupported functional test: $TEST_NAME" >&2',
    '    exit 0',
    '  fi',
    '  exit $status',
    'fi',
  ];

  return lines.join('\n');
}

async function ensureImage({
  localImage,
  registryImage,
  ensure,
  buildIfMissing,
  forceBuild,
  kasConfig,
  imageTarget,
  rootfsPath,
  runtimeRootfs,
}) {
  if (forceBuild) {
    await ensureRootfs({ forceBuild: true, kasConfig, imageTarget, rootfsPath });
    await buildRuntimeImage({ rootfsPath, runtimeRootfs, localImage });
    return;
  }

  if (ensure) {
    await ensureLocalRuntimeImage({
      localImage,
      registryImage,
      buildIfMissing,
      publishIfMissing: false,
      kasConfig,
      imageTarget,
      rootfsPath,
      runtimeRootfs,
    });
    return;
  }

  if (!runtimeImageExists(localImage)) {
    fail(
      `Runtime image not found: ${localImage}\n` +
      'Run `npm run runtime-image` or re-run with --ensure/--build.'
    );
  }
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('-h') || args.includes('--help')) {
    showHelp();
    return;
  }

  let tests = [];
  let useAll = false;
  let timeoutMs = DEFAULT_TIMEOUT_MS;
  let kasConfig = defaults.kasConfig;
  let imageTarget = defaults.imageTarget;
  let imageName = defaults.imageName;
  let imageTag = defaults.imageTag;
  let registryHost = process.env.DIRTSIM_RUNTIME_REGISTRY || defaults.registryHost;
  let imageRef = null;
  let ensure = false;
  let forceBuild = false;
  let buildIfMissing = false;
  let dataDir = null;
  let publishPorts = false;
  let disableXvfb = false;

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    switch (arg) {
      case '--test':
        if (i + 1 >= args.length) {
          fail('--test requires a value');
        }
        tests.push(args[++i]);
        break;
      case '--tests':
        if (i + 1 >= args.length) {
          fail('--tests requires a value');
        }
        tests = tests.concat(parseTestsArg(args[++i]));
        break;
      case '--all':
        useAll = true;
        break;
      case '--timeout':
        if (i + 1 >= args.length) {
          fail('--timeout requires a value');
        }
        timeoutMs = parseIntArg(args[++i], '--timeout', 1);
        break;
      case '--config':
        if (i + 1 >= args.length) {
          fail('--config requires a value');
        }
        kasConfig = args[++i];
        break;
      case '--image-target':
        if (i + 1 >= args.length) {
          fail('--image-target requires a value');
        }
        imageTarget = args[++i];
        break;
      case '--image-name':
        if (i + 1 >= args.length) {
          fail('--image-name requires a value');
        }
        imageName = args[++i];
        break;
      case '--image-tag':
        if (i + 1 >= args.length) {
          fail('--image-tag requires a value');
        }
        imageTag = args[++i];
        break;
      case '--image':
        if (i + 1 >= args.length) {
          fail('--image requires a value');
        }
        imageRef = args[++i];
        break;
      case '--registry':
        if (i + 1 >= args.length) {
          fail('--registry requires a value');
        }
        registryHost = args[++i];
        break;
      case '--ensure':
        ensure = true;
        break;
      case '--build':
        forceBuild = true;
        break;
      case '--build-if-missing':
        buildIfMissing = true;
        break;
      case '--data-dir':
        if (i + 1 >= args.length) {
          fail('--data-dir requires a path');
        }
        dataDir = args[++i];
        break;
      case '--publish-ports':
        publishPorts = true;
        break;
      case '--disable-xvfb':
        disableXvfb = true;
        break;
      case '-h':
      case '--help':
        showHelp();
        return;
      default:
        fail(`Unknown argument: ${arg}`);
    }
  }

  if (buildIfMissing && !ensure) {
    warn('--build-if-missing is ignored without --ensure.');
  }

  const testList = resolveTests({ tests, useAll });
  let orderedTests = testList;
  if (testList.includes('canExit') && testList.length > 1) {
    orderedTests = testList.filter(name => name !== 'canExit');
    orderedTests.push('canExit');
    warn('canExit reboots os-manager; running it last.');
  }
  const { rootfsPath, runtimeRootfs } = getRuntimePaths({ imageTarget });

  let localImage = null;
  let registryImage = null;
  if (imageRef) {
    localImage = imageRef;
    registryImage = imageRef.includes('/') ? null : `${registryHost}/${imageRef}`;
  } else {
    const refs = getRuntimeImageRefs({ imageName, imageTag, registryHost });
    localImage = refs.localImage;
    registryImage = refs.registryImage;
  }

  await ensureImage({
    localImage,
    registryImage,
    ensure,
    buildIfMissing,
    forceBuild,
    kasConfig,
    imageTarget,
    rootfsPath,
    runtimeRootfs,
  });

  info(`Running functional tests in ${localImage}...`);
  for (const testName of orderedTests) {
    const argsForDocker = ['run', '--rm'];
    if (process.stdin.isTTY) {
      argsForDocker.push('-i');
    }
    if (process.stdout.isTTY) {
      argsForDocker.push('-t');
    }
    if (publishPorts) {
      argsForDocker.push('-p', '7070:7070', '-p', '8080:8080', '-p', '9090:9090');
    }
    if (dataDir) {
      argsForDocker.push('-v', `${dataDir}:/data/dirtsim`);
    }
    argsForDocker.push('-e', 'DIRTSIM_OS_BACKEND=local');
    if (disableXvfb) {
      argsForDocker.push('-e', 'DIRTSIM_DISABLE_XVFB=1');
    }
    argsForDocker.push(
      localImage,
      '/bin/sh',
      '-lc',
      buildContainerScript(testName, timeoutMs)
    );

    await run('docker', argsForDocker);
  }
  success('Functional tests completed.');
}

main().catch(err => {
  fail(err.message);
});
