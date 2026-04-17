#!/bin/bash
#
# Install git hooks for dirtsim development.
#
# Usage: ./hooks/install-hooks.sh
#

set -euo pipefail

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)

if [ -z "$REPO_ROOT" ]; then
    echo "Error: Not in a git repository"
    exit 1
fi

HOOKS_DIR="$REPO_ROOT/.git/hooks"

echo "Installing git hooks..."

install_hook() {
    local hook_name="$1"
    local hook_path="$HOOKS_DIR/$hook_name"
    local template_path="$REPO_ROOT/apps/hooks/$hook_name"

    if [ ! -f "$template_path" ]; then
        echo "Error: Hook template missing: $template_path"
        exit 1
    fi

    if [ -f "$hook_path" ] && [ ! -L "$hook_path" ]; then
        echo "Warning: Existing $hook_name hook found (not a symlink)"
        echo "Backing up to $hook_name.backup"
        mv "$hook_path" "$hook_path.backup"
    fi

    ln -sf "../../apps/hooks/$hook_name" "$hook_path"
    echo "$hook_name hook installed"
}

install_hook pre-commit
install_hook pre-push

echo ""
echo "Git hooks installed successfully!"
echo ""
echo "The pre-commit hook will:"
echo "  1. Format C++ code with clang-format"
echo "  2. Lint JavaScript with ESLint"
echo "  3. Run unit and integration tests"
echo ""
echo "The pre-push hook will:"
echo "  1. Run SMB ROM-dependent tests when SMB search files changed"
echo ""
echo "Skip options:"
echo "  SKIP_TESTS=1 git commit      - Skip pre-commit tests only"
echo "  SKIP_LINT=1 git commit       - Skip JS linting only"
echo "  SKIP_SMB_TESTS=1 git push    - Skip SMB pre-push tests only"
