# Repository Guidelines

## Start Here
- Read `README.md` for repo overview.
- For app work, read `apps/CLAUDE.md`, `apps/README.md`, and `apps/design_docs/coding_convention.md`.
- For deployment details, see `yocto/README.md`.

## Project Structure
- `apps/`: main simulation app (server, UI, CLI) and design docs.
- `apps/src/`: C++ source for core, server, UI, and CLI.
- `apps/src/cli/README.md`: CLI usage and command reference.
- `yocto/`: Yocto layer and deployment tooling for Raspberry Pi images.
- `docker/`: Docker build environment for CI/local development.
- `docs/`: supplemental documentation.

## Build, Run, Test (run from `apps/` unless noted)
- `make debug` / `make release`: build binaries into `build-debug/` or `build-release/`.
- `./build-debug/bin/cli run-all`: run server + UI locally.
- `./build-debug/bin/cli integration_test`: quick smoke test for server/UI/CLI.
- `make test`: run unit tests (GoogleTest); filter with `make test ARGS='--gtest_filter=State*'`.
- `make format`: apply formatting rules.
- `./build-debug/bin/cli cleanup`: stop local dirtsim processes.

## Remote Testing & Deployment
- Default device host: `dirtsim.local` (server `:8080`, UI `:7070`).
- Test host: `dirtsim2.local` via SSH; CLI, server, and UI are installed there.
- Full yolo update (A/B image update + reboot):
  - `cd yocto && npm run yolo -- --target dirtsim2.local --hold-my-mead`
- Fast yolo update (app-only, no reboot; requires prior full build):
  - `cd yocto && npm run yolo -- --target dirtsim2.local --fast`
  - `cd yocto && ./update.sh --target dirtsim2.local --fast`
- Tail remote logs:
  - `./tail_remote_logs.sh dirtsim2.local`
  - `ssh dirtsim2.local "sudo journalctl -u dirtsim-server.service -u dirtsim-ui.service -f --no-pager"`
- Remote CLI status checks:
  - `ssh dirtsim2.local "dirtsim-cli server StatusGet"`
  - `ssh dirtsim2.local "dirtsim-cli ui StatusGet"`

## Coding Conventions (condensed)
- Comments end in periods; keep inline comments rare and focus docs at file/class scope.
- Case: `UpperCamelCase` types, `lowerCamelCase` functions/members.
- JSON is only for transport boundaries; convert to typed structs internally.
- CLI output: machine-readable data to stdout, logs/errors to stderr.
- Prefer alphabetical ordering where it improves readability.
- Favor poka-yoke/root-cause prevention over patching symptoms.
- Prefer const, early exits, and RAII; avoid `std::move` unless required; use designated initializers.
- Keep implementations in `.cpp` when possible; use forward declarations and `unique_ptr`/`shared_ptr` to cut compile chains.
- Use logging macros (`LOG_*`, `SLOG_*`) instead of manual `spdlog` prefixes.

## Commit & PR Notes
- Commit messages are short, imperative sentences.
- PRs describe behavior changes and include screenshots/clips for UI changes when applicable.
- Install hooks with `cd apps && ./hooks/install-hooks.sh`.

## Logs
- Log file: `dirtsim.log` next to executables; console defaults to INFO and file includes DEBUG/TRACE.
