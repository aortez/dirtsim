# Repository Guidelines

## Project Structure & Module Organization
- `apps/`: Main DirtSim application (server, UI, CLI) and design docs.
- `apps/src/`: C++ source for core, server, UI, and CLI.
- `apps/design_docs/`: Architecture, physics, and coding conventions.
- `apps/src/cli/README.md`: CLI usage and command reference.
- `yocto/`: Yocto layer and deployment tooling for Raspberry Pi images.
- `docker/`: Docker build environment for CI/local development.
- `docs/`: Supplemental documentation.

## Build, Test, and Development Commands
Run commands from `apps/` unless noted.
- `make debug`: Build debug binaries into `build-debug/`.
- `make release`: Build optimized binaries into `build-release/`.
- `./build-debug/bin/cli run-all`: Run server + UI locally.
- `make test`: Build and run unit tests (GoogleTest).
- `make format`: Apply repository formatting rules.
- `cd yocto && npm run yolo -- --hold-my-mead`: Build/deploy Pi image.

## Coding Style & Naming Conventions
- Case: `UpperCamelCase` for types, `lowerCamelCase` for functions/members.
- Comments end in periods; prefer sparse header/class docs over inline comments.
- JSON is only for transport (network/file I/O); use typed structs internally.
- CLI output: machine-readable results to stdout, logs/errors to stderr.
- Prefer alphabetical ordering of related items when it improves readability.

## Testing Guidelines
- Framework: GoogleTest; test files are typically named `*_test.cpp`.
- Run all tests: `make test`.
- Filter tests: `make test ARGS='--gtest_filter=State*'`.
- Direct run: `./build-debug/bin/dirtsim-tests --gtest_filter=StateIdle*`.

## Commit & Pull Request Guidelines
- Commit messages are short, imperative sentences; issues/PRs often noted
  in parentheses (e.g., `Polish IconRail UI and add auto-sizing world dimensions (#48)`).
- PRs should describe behavior changes, link related issues, and include
  screenshots or clips for UI changes when applicable.
- Install hooks for formatting/lint/tests: `cd apps && ./hooks/install-hooks.sh`.

## Deployment & Remote Notes
- Default services run on `dirtsim.local`; server UI endpoints: `:8080`/`:7070`.
- Use CLI for remote checks:
  `./build-debug/bin/cli --address ws://dirtsim.local:8080 server StatusGet`.
