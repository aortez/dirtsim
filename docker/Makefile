# Docker orchestration for dirtsim builds.
# Usage:
#   make build-image    # Build the Docker image
#   make build-debug    # Build dirtsim debug in container
#   make build-release  # Build dirtsim release in container
#   make test           # Run tests in container
#   make format         # Format code in container
#   make shell          # Interactive shell in container
#   make clean-image    # Remove Docker image
#   make help           # Show this help

.PHONY: help build-image build-debug build-release test format format-check lint ccache-stats shell clean-image print-image docs-screenshots

# Image name and tag.
IMAGE_NAME := dirtsim-builder
# Tag with hash of Dockerfile + date so images rebuild daily and per-branch.
IMAGE_TAG ?= $(shell (cat Dockerfile; date +%Y-%m-%d) | sha256sum | cut -c1-16)
IMAGE := $(IMAGE_NAME):$(IMAGE_TAG)

# Get the project root (parent of docker directory).
PROJECT_ROOT := $(shell cd .. && pwd)

# SSH key directory (override with SSH_DIR=/path/to/.ssh).
SSH_DIR := $(HOME)/.ssh

# Default SSH target for docs screenshot capture.
DIRTSIM_SSH_HOST ?= dirtsim2.local
DIRTSIM_REMOTE_TMP ?= /tmp/dirtsim-ui-docs
DIRTSIM_SSH_CONTROL_PATH ?= /tmp/dirtsim-ssh/cm-%r@%h:%p

# Ccache directory and size limit.
CCACHE_DIR ?= $(HOME)/.ccache/dirtsim
CCACHE_MAXSIZE ?= 500M

# Run as the current user to avoid permission issues.
DOCKER_USER := $(shell id -u):$(shell id -g)

# Base docker run command.
DOCKER_RUN := docker run --rm \
	--user $(DOCKER_USER) \
	-v $(PROJECT_ROOT):/workspace \
	-v $(SSH_DIR):$(SSH_DIR):ro \
	-v $(CCACHE_DIR):$(CCACHE_DIR) \
	-e HOME=$(HOME) \
	-e CCACHE_DIR=$(CCACHE_DIR) \
	-e CCACHE_MAXSIZE=$(CCACHE_MAXSIZE) \
	-w /workspace/apps \
	$(IMAGE)

define maybe_env
$(if $(strip $($(1))),-e $(1)=$($(1)),)
endef

DOCS_SCREENSHOT_ENV := \
	$(call maybe_env,DOCS_SCREENSHOT_ONLY) \
	$(call maybe_env,DOCS_SCREENSHOT_RESET_WAIT_MS) \
	$(call maybe_env,DOCS_SCREENSHOT_MIN_BYTES) \
	$(call maybe_env,DOCS_SCREENSHOT_MAX_RETRIES)

help:
	@echo "Docker build targets:"
	@echo "  make build-image    - Build the Docker image with all dependencies"
	@echo "  make build-debug    - Build dirtsim (debug) inside Docker container"
	@echo "  make build-release  - Build dirtsim (release) inside Docker container"
	@echo "  make test           - Run unit tests inside Docker container"
	@echo "  make format         - Format code inside Docker container"
	@echo "  make format-check   - Check code formatting inside Docker container"
	@echo "  make lint           - Run JavaScript lint inside Docker container"
	@echo "  make shell          - Start interactive bash shell in container"
	@echo "  make docs-screenshots - Capture UI docs screenshots inside Docker"
	@echo "  make clean-image    - Remove Docker image"
	@echo ""
	@echo "Example workflow:"
	@echo "  make build-image    # First time only (or when deps change)"
	@echo "  make build-debug    # Build the project"
	@echo "  make test           # Run tests"

print-image:
	@echo $(IMAGE)

build-image:
	@echo "Building Docker image $(IMAGE)..."
	docker build -t $(IMAGE) -f Dockerfile ..

build-debug: build-image
	@echo "Building dirtsim (debug) in Docker..."
	$(DOCKER_RUN) bash -c "ccache -z && make -j\$$(nproc) debug && ccache -s"

build-release: build-image
	@echo "Building dirtsim (release) in Docker..."
	$(DOCKER_RUN) bash -c "ccache -z && make -j\$$(nproc) release && ccache -s"

test: build-image
	@echo "Running tests in Docker..."
	$(DOCKER_RUN) ./build-debug/bin/dirtsim-tests

format: build-image
	@echo "Formatting code in Docker..."
	$(DOCKER_RUN) make format

format-check: build-image
	@echo "Checking code formatting in Docker..."
	$(DOCKER_RUN) bash -c "make format && git diff --quiet || (echo 'Code formatting issues detected. Run make format locally.' && git diff && exit 1)"

lint: build-image
	@echo "Linting JavaScript in Docker..."
	$(DOCKER_RUN) bash -c "cd /workspace && mkdir -p /tmp/npm-cache && NPM_CONFIG_CACHE=/tmp/npm-cache npm ci --no-audit --no-fund && npm run lint"

ccache-stats:
	@$(DOCKER_RUN) ccache -s

shell: build-image
	@echo "Starting interactive shell in Docker container..."
	docker run --rm -it \
		--user $(DOCKER_USER) \
		-v $(PROJECT_ROOT):/workspace \
		-v $(SSH_DIR):$(SSH_DIR):ro \
		-v $(CCACHE_DIR):$(CCACHE_DIR) \
		-e HOME=$(HOME) \
		-e CCACHE_DIR=$(CCACHE_DIR) \
		-e CCACHE_MAXSIZE=$(CCACHE_MAXSIZE) \
		-w /workspace/apps \
		$(IMAGE) \
		/bin/bash

docs-screenshots: build-image
	@echo "Capturing UI docs screenshots in Docker..."
	docker run --rm \
		--user $(DOCKER_USER) \
		-v $(PROJECT_ROOT):/workspace \
		-v $(SSH_DIR):$(SSH_DIR):ro \
		-v $(CCACHE_DIR):$(CCACHE_DIR) \
		-e HOME=$(HOME) \
		-e CCACHE_DIR=$(CCACHE_DIR) \
		-e CCACHE_MAXSIZE=$(CCACHE_MAXSIZE) \
		-e DIRTSIM_SSH_HOST=$(DIRTSIM_SSH_HOST) \
		-e DIRTSIM_SSH_USER=$(DIRTSIM_SSH_USER) \
		-e DIRTSIM_REMOTE_TMP=$(DIRTSIM_REMOTE_TMP) \
	-e DIRTSIM_SSH_CONTROL_PATH=$(DIRTSIM_SSH_CONTROL_PATH) \
	$(DOCS_SCREENSHOT_ENV) \
	-w /workspace/apps/src/ui/docs \
	$(IMAGE) \
	node scripts/render_cli_screenshots.mjs

clean-image:
	@echo "Removing Docker image $(IMAGE)..."
	docker rmi $(IMAGE) || true
