#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROM_DIR="${APP_DIR}/testdata/roms"
ROM_FILE_NAME="Flappy.Paratroopa.World.Unl.nes"
ROM_PATH="${ROM_DIR}/${ROM_FILE_NAME}"
ROM_SHA256="f45fc9ab0790bbaabbe7dfeff5397f6282e6c6d50ec49b2a13226207148b6d93"
ROM_URL="https://github.com/captain-http/flappy-paratroopa-nes/releases/download/v1.0/${ROM_FILE_NAME}"
ROOT_ROM_PATH="${APP_DIR}/../${ROM_FILE_NAME}"

mkdir -p "${ROM_DIR}"

checksum_file() {
    local path="$1"
    sha256sum "${path}" | awk '{print $1}'
}

if [[ -f "${ROM_PATH}" ]]; then
    existing_checksum="$(checksum_file "${ROM_PATH}")"
    if [[ "${existing_checksum}" == "${ROM_SHA256}" ]]; then
        echo "NES test ROM already present: ${ROM_PATH}"
        exit 0
    fi
    echo "Existing ROM checksum mismatch at ${ROM_PATH}; refreshing."
fi

if [[ -f "${ROOT_ROM_PATH}" ]]; then
    root_checksum="$(checksum_file "${ROOT_ROM_PATH}")"
    if [[ "${root_checksum}" == "${ROM_SHA256}" ]]; then
        cp -f "${ROOT_ROM_PATH}" "${ROM_PATH}"
        echo "Copied ROM from repo root to ${ROM_PATH}"
        exit 0
    fi
fi

tmp_path="${ROM_PATH}.tmp"
echo "Downloading NES test ROM from ${ROM_URL}"
curl -fL --retry 3 --retry-delay 1 --retry-connrefused -o "${tmp_path}" "${ROM_URL}"

downloaded_checksum="$(checksum_file "${tmp_path}")"
if [[ "${downloaded_checksum}" != "${ROM_SHA256}" ]]; then
    rm -f "${tmp_path}"
    echo "Checksum mismatch for downloaded ROM." >&2
    echo "Expected: ${ROM_SHA256}" >&2
    echo "Actual:   ${downloaded_checksum}" >&2
    exit 1
fi

mv -f "${tmp_path}" "${ROM_PATH}"
echo "NES test ROM ready: ${ROM_PATH}"
