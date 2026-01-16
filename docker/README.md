# Docker Build Environment

This directory contains the Docker-based build and test environment for dirtsim.

## Quick Start

```bash
# Build the Docker image (first time or when dependencies change).
make build-image

# Build dirtsim in Docker.
make build-debug

# Run tests in Docker.
make test

# Interactive shell in container.
make shell
```

## Yocto Builds

The same Docker image can build the Yocto image:

```bash
cd yocto
npm run docker-build
```

## Available Targets

- `make build-image` - Build the Docker image with all dependencies
- `make build-debug` - Build dirtsim (debug) inside Docker container
- `make build-release` - Build dirtsim (release) inside Docker container
- `make test` - Run unit tests inside Docker container
- `make format` - Format code inside Docker container
- `make format-check` - Check code formatting (CI-friendly)
- `make shell` - Start interactive bash shell in container
- `make clean-image` - Remove Docker image

## SSH Keys for Remote Testing

The container mounts your `~/.ssh` directory read-only for SSH access to remote test devices (e.g., `dirtsim.local`).

**Local development:**
```bash
# Uses $HOME/.ssh by default.
make test

# Override SSH key location.
SSH_DIR=/path/to/.ssh make test
```

**CI (GitHub Actions):**
Store the SSH private key as a GitHub secret, then inject it at runtime:

```yaml
- name: Set up SSH key
  run: |
    mkdir -p ~/.ssh
    echo "${{ secrets.SSH_PRIVATE_KEY }}" > ~/.ssh/id_ed25519
    chmod 600 ~/.ssh/id_ed25519
    ssh-keyscan dirtsim.local >> ~/.ssh/known_hosts

- name: Run tests in Docker
  run: cd docker && make test
```

## File Permissions

The container runs as root, but mounts the host workspace as a volume. Build artifacts created in the container will be owned by root on the host. If needed, fix permissions with:

```bash
sudo chown -R $USER:$USER ../apps/build-debug
```

## Caching

The current CI setup uses Docker's built-in layer caching, which works well on self-hosted runners with persistent local storage. The image tag includes the current date, so a new image is built automatically on the first PR of each day or when the Dockerfile changes.

**Advanced option:** For more aggressive caching (e.g., multi-runner setups), use explicit cache export/import with Buildx:

```yaml
- name: Set up Docker Buildx
  uses: docker/setup-buildx-action@v3

- name: Build Docker image
  uses: docker/build-push-action@v5
  with:
    context: .
    file: docker/Dockerfile
    cache-from: type=local,src=/tmp/.buildx-cache
    cache-to: type=local,dest=/tmp/.buildx-cache-new,mode=max
```

See `.github/workflows/docker-build.yml` for the actual CI setup.
