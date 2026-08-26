# remotifyd

[![C tests](https://github.com/langgenius/remotifyd/actions/workflows/tests.yml/badge.svg)](https://github.com/langgenius/remotifyd/actions/workflows/tests.yml)
[![License: GPL-3.0-only](https://img.shields.io/badge/License-GPL--3.0--only-blue.svg)](LICENSE)

[Remotify](https://remotify-labs.com) is software purpose-built for AI agents
to operate all kinds of remote devices. `remotifyd` is the small native daemon
that runs on each device and connects it to Remotify.

## Why not SSH, Tailscale, or a traditional remote-control agent?

Those tools solve human remote access or network connectivity. AI agents need
a different interface.

1. **Agents need editing primitives, not just a shell.** Agents frequently
   apply patches and need dedicated tools for safely editing files. A typical
   AI-agent integration over SSH gives the agent a remote shell, but no
   agent-native patch or edit primitive. That pushes the agent toward commands
   such as `sed` for source edits; those text substitutions are brittle and
   often produce incorrect changes. Remotify provides patch and file operations
   as explicit tools.
2. **Many remote-control products have left older Windows machines behind.**
   Their current clients cannot run on systems such as Windows XP or Windows 7,
   even though those machines still operate real equipment and software.
3. **General-purpose remote access can be much heavier than the job requires.**
   Clients can grow to hundreds of megabytes, request broad privileges, and
   modify network interfaces to create an overlay network. That is unnecessary
   when an agent only needs a narrow, auditable way to run commands and change
   files on an authorized device.
4. **Direct SSH has no team control plane.** When several people or agents need
   to manage many machines, direct SSH connections bypass a central place to
   attribute access, retain an operation trail, or revoke a member before the
   next operation reaches a device. Remotify puts a team-aware management layer
   in the path: every operation is tied to an authenticated user and Agent
   session, device access is checked centrally, and administrators can revoke
   that access without rotating credentials across every machine.

Remotify is designed for AI agents from the protocol up. It includes dedicated
patch and file-transfer operations, and a release build of `remotifyd` is about
3 MB. The daemon is written entirely in C so the same small codebase can run on
old and modern machines: Windows XP, Windows 7, Windows 10/11, Linux, and
macOS.

## How it fits together

```text
AI agent
   |
   | Remotify CLI
   v
Remotify service
   |
   | authenticated outbound WebSocket
   v
remotifyd on the remote device
   |
   +-- run a command and report its output
   +-- apply a patch
   +-- upload or download a file
```

`remotifyd` does not contain an AI agent and does not open an inbound remote
shell. It keeps an authenticated outbound connection to Remotify and executes
only the operations delivered for its enrolled device. Commands, patches, and
file writes are never automatically replayed after an uncertain transport
failure.

Internally, a long-running parent owns the network connection and a separate
Executor process owns command execution. They communicate through fixed binary
IPC frames. See [docs/INTERNALS.md](docs/INTERNALS.md) for the detailed runtime,
ownership, WebSocket, IPC, and process-lifecycle design.

## Build targets

The release matrix currently includes:

- Windows XP, x86
- Windows 7, x86-64
- Windows 10/11, x86-64
- Linux, x86-64 and ARM64
- macOS, Intel and Apple silicon

## Build

The project requires CMake and a C toolchain. Pinned native dependencies are
downloaded and built by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For the same named release artifacts used by Remotify, use the Makefile. For
example, on an x86-64 Linux host or toolchain:

```sh
make build-release TARGET=linux TARGET_NAME=remotifyd-linux-amd64
```

`TARGET` is one of `darwin`, `linux`, `windowsxp`, `windows7`, or `windows10`.
The output architecture comes from the active compiler and toolchain, not from
`TARGET_NAME`. Windows targets require the corresponding Windows
cross-toolchain.

## Tests

The C test suite covers the patch engine, including its stress and fault-stress
paths.

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-tests --target remotifyd_patch_tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Protocol sources

Generated device-protocol sources live in `src/device_protocol_gen.*` and are
checked in so this repository builds independently. Their schema source lives
in the parent Remotify repository. From a recursive Remotify checkout,
regenerate both the Go and C implementations with:

```sh
make -C protocol generate
```

Commit the regenerated C sources here before updating Remotify's submodule
pointer.

## License

`remotifyd` is licensed under [GPL-3.0-only](LICENSE). Third-party dependency
licenses and source attributions are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
