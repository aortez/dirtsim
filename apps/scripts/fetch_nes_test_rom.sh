#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROM_DIR="${APP_DIR}/testdata/roms"
ROM_MANIFEST_PATH="${ROM_DIR}/rom_manifest.json"

readonly FLAPPY_ID="flappy"
readonly STB_UNROM_ID="super_tilt_bro_unrom_no_network"
readonly DEFAULT_ID="${FLAPPY_ID}"

readonly STB_REPO="sgadrat/super-tilt-bro"
readonly STB_WORKFLOW_FILE="compile.yaml"
readonly STB_EXPECTED_HEAD_SHA="b132fd25add46f816e04be64c434386743b84b8b"

mkdir -p "${ROM_DIR}"

checksum_file() {
    local path="$1"
    sha256sum "${path}" | awk '{print $1}'
}

usage() {
    cat <<'EOF'
Usage:
  fetch_nes_test_rom.sh [rom-id ...]
  fetch_nes_test_rom.sh --all
  fetch_nes_test_rom.sh --list

ROM IDs:
  flappy
  super_tilt_bro_unrom_no_network

Default:
  If no arguments are provided, fetches: flappy
EOF
}

rom_file_name() {
    case "$1" in
        "${FLAPPY_ID}") echo "Flappy.Paratroopa.World.Unl.nes" ;;
        "${STB_UNROM_ID}") echo "tilt_no_network_unrom_(E).nes" ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

rom_sha256() {
    case "$1" in
        "${FLAPPY_ID}") echo "f45fc9ab0790bbaabbe7dfeff5397f6282e6c6d50ec49b2a13226207148b6d93" ;;
        "${STB_UNROM_ID}") echo "6f80d56ce0b242a4faceafafea321feb1c364ab8e7937646e8580ae9289a4ec3" ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

rom_spdx() {
    case "$1" in
        "${FLAPPY_ID}") echo "MIT" ;;
        "${STB_UNROM_ID}") echo "WTFPL" ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

rom_source_url() {
    case "$1" in
        "${FLAPPY_ID}")
            echo "https://github.com/captain-http/flappy-paratroopa-nes/releases/download/v1.0/Flappy.Paratroopa.World.Unl.nes"
            ;;
        "${STB_UNROM_ID}")
            echo "https://github.com/${STB_REPO}/actions/workflows/${STB_WORKFLOW_FILE}"
            ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

rom_build_source() {
    case "$1" in
        "${FLAPPY_ID}")
            echo "captain-http/flappy-paratroopa-nes release v1.0"
            ;;
        "${STB_UNROM_ID}")
            echo "${STB_REPO} workflow ${STB_WORKFLOW_FILE} @ ${STB_EXPECTED_HEAD_SHA} artifact: roms"
            ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

rom_fetch_method() {
    case "$1" in
        "${FLAPPY_ID}") echo "direct-download" ;;
        "${STB_UNROM_ID}") echo "github-actions-artifact" ;;
        *)
            echo "Unknown ROM id: $1" >&2
            exit 1
            ;;
    esac
}

fetch_from_release_url() {
    local url="$1"
    local output_path="$2"

    curl -fL --retry 3 --retry-delay 1 --retry-connrefused -o "${output_path}" "${url}"
}

fetch_stb_unrom_from_actions() {
    local rom_file_name="$1"
    local output_path="$2"

    if ! command -v gh >/dev/null 2>&1; then
        echo "Error: gh CLI is required for ${STB_UNROM_ID} fetch path." >&2
        exit 1
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        echo "Error: unzip is required for ${STB_UNROM_ID} fetch path." >&2
        exit 1
    fi

    local run_id
    run_id="$(gh api "repos/${STB_REPO}/actions/workflows/${STB_WORKFLOW_FILE}/runs?status=completed&per_page=100" \
        --jq ".workflow_runs[] | select(.conclusion == \"success\" and .head_sha == \"${STB_EXPECTED_HEAD_SHA}\") | .id" \
        | head -n 1)"
    if [[ -z "${run_id}" ]]; then
        echo "Error: no successful ${STB_WORKFLOW_FILE} run found for ${STB_REPO} at ${STB_EXPECTED_HEAD_SHA}." >&2
        exit 1
    fi

    local artifact_id
    artifact_id="$(gh api "repos/${STB_REPO}/actions/runs/${run_id}/artifacts" \
        --jq '.artifacts[] | select(.name == "roms" and .expired == false) | .id' \
        | head -n 1)"
    if [[ -z "${artifact_id}" ]]; then
        echo "Error: ROM artifact not available for run ${run_id} (possibly expired)." >&2
        exit 1
    fi

    local zip_path
    zip_path="$(mktemp "${ROM_DIR}/stb-roms-XXXXXX.zip")"
    gh api "repos/${STB_REPO}/actions/artifacts/${artifact_id}/zip" > "${zip_path}"

    local zip_member
    zip_member="$(unzip -Z1 "${zip_path}" | grep -F "/${rom_file_name}" | head -n 1 || true)"
    if [[ -z "${zip_member}" ]]; then
        rm -f "${zip_path}"
        echo "Error: ${rom_file_name} not found in artifact ${artifact_id}." >&2
        exit 1
    fi

    unzip -p "${zip_path}" "${zip_member}" > "${output_path}"
    rm -f "${zip_path}"
}

ensure_rom() {
    local rom_id="$1"
    local file_name sha256 source_url fetch_method rom_path root_rom_path
    file_name="$(rom_file_name "${rom_id}")"
    sha256="$(rom_sha256 "${rom_id}")"
    source_url="$(rom_source_url "${rom_id}")"
    fetch_method="$(rom_fetch_method "${rom_id}")"
    rom_path="${ROM_DIR}/${file_name}"
    root_rom_path="${APP_DIR}/../${file_name}"

    if [[ -f "${rom_path}" ]]; then
        local existing_checksum
        existing_checksum="$(checksum_file "${rom_path}")"
        if [[ "${existing_checksum}" == "${sha256}" ]]; then
            echo "NES ROM already present: ${rom_path}"
            return 0
        fi
        echo "Existing ROM checksum mismatch at ${rom_path}; refreshing."
    fi

    if [[ -f "${root_rom_path}" ]]; then
        local root_checksum
        root_checksum="$(checksum_file "${root_rom_path}")"
        if [[ "${root_checksum}" == "${sha256}" ]]; then
            cp -f "${root_rom_path}" "${rom_path}"
            echo "Copied ROM from repo root to ${rom_path}"
            return 0
        fi
    fi

    local tmp_path downloaded_checksum
    tmp_path="$(mktemp "${ROM_DIR}/${file_name}.XXXXXX.tmp")"
    echo "Downloading NES ROM (${rom_id}) from ${source_url}"
    case "${fetch_method}" in
        "direct-download")
            fetch_from_release_url "${source_url}" "${tmp_path}"
            ;;
        "github-actions-artifact")
            fetch_stb_unrom_from_actions "${file_name}" "${tmp_path}"
            ;;
        *)
            rm -f "${tmp_path}"
            echo "Unknown fetch method: ${fetch_method}" >&2
            exit 1
            ;;
    esac

    downloaded_checksum="$(checksum_file "${tmp_path}")"
    if [[ "${downloaded_checksum}" != "${sha256}" ]]; then
        rm -f "${tmp_path}"
        echo "Checksum mismatch for downloaded ROM (${rom_id})." >&2
        echo "Expected: ${sha256}" >&2
        echo "Actual:   ${downloaded_checksum}" >&2
        exit 1
    fi

    mv -f "${tmp_path}" "${rom_path}"
    echo "NES ROM ready: ${rom_path}"
}

write_manifest() {
    cat > "${ROM_MANIFEST_PATH}" <<EOF
{
  "schemaVersion": 1,
  "generatedBy": "apps/scripts/fetch_nes_test_rom.sh",
  "roms": [
    {
      "id": "${FLAPPY_ID}",
      "fileName": "$(rom_file_name "${FLAPPY_ID}")",
      "sha256": "$(rom_sha256 "${FLAPPY_ID}")",
      "spdxLicenseId": "$(rom_spdx "${FLAPPY_ID}")",
      "sourceUrl": "$(rom_source_url "${FLAPPY_ID}")",
      "buildSource": "$(rom_build_source "${FLAPPY_ID}")",
      "fetchMethod": "$(rom_fetch_method "${FLAPPY_ID}")"
    },
    {
      "id": "${STB_UNROM_ID}",
      "fileName": "$(rom_file_name "${STB_UNROM_ID}")",
      "sha256": "$(rom_sha256 "${STB_UNROM_ID}")",
      "spdxLicenseId": "$(rom_spdx "${STB_UNROM_ID}")",
      "sourceUrl": "$(rom_source_url "${STB_UNROM_ID}")",
      "buildSource": "$(rom_build_source "${STB_UNROM_ID}")",
      "fetchMethod": "$(rom_fetch_method "${STB_UNROM_ID}")"
    }
  ]
}
EOF
    echo "ROM manifest updated: ${ROM_MANIFEST_PATH}"
}

list_rom_ids() {
    echo "${FLAPPY_ID}"
    echo "${STB_UNROM_ID}"
}

rom_targets=()
if [[ "$#" -eq 0 ]]; then
    rom_targets=("${DEFAULT_ID}")
else
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --list)
            list_rom_ids
            exit 0
            ;;
        --all)
            rom_targets=("${FLAPPY_ID}" "${STB_UNROM_ID}")
            shift
            ;;
        *)
            rom_targets=("$@")
            ;;
    esac
fi

for rom_id in "${rom_targets[@]}"; do
    case "${rom_id}" in
        "${FLAPPY_ID}"|"${STB_UNROM_ID}") ;;
        *)
            echo "Unknown ROM id: ${rom_id}" >&2
            usage >&2
            exit 1
            ;;
    esac
done

write_manifest

for rom_id in "${rom_targets[@]}"; do
    ensure_rom "${rom_id}"
done
