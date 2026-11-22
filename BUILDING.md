# Building CorkDeck

## Apple Silicon Mac to Nintendo Switch

Tested with CorkDeck 0.3e (`adc0b522`), macOS 26.6.2, Homebrew Bison 3.8.2
and OrbStack running Linux ARM64 containers. The build produces the Switch NRO
and an SD-card ZIP. The Mac prepares Wine's headers and Windows setup programs.
The container supplies devkitA64, CMake, Ninja, libnx and Mesa.

### 1. Start from a clean checkout

Use a checkout that includes the shared Vulkan driver check in
`horizon-wine/tools/runtime_features.py`. The original `adc0b522` predates
that packaging fix. To reproduce that exact source plus the fix, run this
block **from the updated CorkDeck tree**. It carries just the three packaging
files' changes to a fresh checkout. If your clean checkout already contains
the fix, start at `git submodule update` below.

```sh
set -e
patch_file=$(mktemp)
git diff adc0b522 -- horizon-wine/tools/package-amd64.py \
  horizon-wine/tools/package-autorun.py horizon-wine/tools/runtime_features.py > "$patch_file"
git clone https://github.com/plv890/CorkDeck.git ../CorkDeck-mac
cd ../CorkDeck-mac
git checkout --detach adc0b522
git apply "$patch_file"
rm "$patch_file"
```

Run the remaining blocks in the same shell from the checkout root. Start the
Docker engine and have Homebrew and the Xcode Command Line Tools available.

```sh
set -e
git submodule update --init horizon-dlls
git submodule status horizon-dlls
brew install bison
export PATH="$(brew --prefix bison)/bin:$PATH"
bison --version
xcrun --find clang
docker info >/dev/null
```

For 0.3e the DLL submodule should be at
`ffb73f3074914f79b04a7c5635b478eaf0ab19e2`. Wine needs Bison 3.0 or
newer. The PATH change selects Homebrew's keg-only Bison for this shell.

### 2. Fetch the ARM64 image and Mac toolchain

The top-level `build.sh` still pulls the older tag from `horizon-wine/switch-dev.txt`
for the boot bundle. It returned `not found` during the Mac build. Use the
component script in step 3 so both container builds use the tested image.

```sh
export DOCKER_DEFAULT_PLATFORM=linux/arm64
export WINE_NX_DEVKIT_IMAGE=ghcr.io/autorunhq/switch-dev:2026.10.12@sha256:ba1197f4ff6afe334b960535812ef4c412346cbefb15548d9300d9fc2aebaf23
docker pull --platform linux/arm64 "$WINE_NX_DEVKIT_IMAGE"
docker image inspect "$WINE_NX_DEVKIT_IMAGE" --format '{{.Os}}/{{.Architecture}}'

toolchains="$PWD/horizon-wine/toolchains"
llvm_name=llvm-mingw-20260505-ucrt-macos-universal
export WINE_NX_LLVM_MINGW="$toolchains/$llvm_name"
mkdir -p "$toolchains"
(
  cd "$toolchains"
  curl -fL --retry 3 -o "$llvm_name.tar.xz" \
    "https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/$llvm_name.tar.xz"
  printf '%s  %s\n' 050379de888f0c843787819dadf183df3693330a5724643919e9121f16355295 \
    "$llvm_name.tar.xz" | shasum -a 256 -c -
  tar -xJf "$llvm_name.tar.xz"
)
```

### 3. Build the boot bundle and runtime

The boot step needs network access on a clean checkout. It fetches the pinned
Atmosphère 1.11.2 and Horizon-OC 2.5.1 inputs and builds both loaders and
Mesosphère against the released libnx. An explicit bundle path makes the
runtime build fail early if the manifest is missing.

```sh
export WINE_NX_JOBS=8
export WINE_NX_BOOT_BUNDLE_DIR="$toolchains/boot-payloads/bundle"
docker run --rm --platform linux/arm64 -v "$PWD:/work" -w /work \
  -e WINE_NX_JOBS -e DEVKITPRO=/opt/devkitpro-release \
  "$WINE_NX_DEVKIT_IMAGE" sh horizon-wine/build-boot-bundle.sh

export WINE_NX_FEX=1 WINE_NX_DXVK=1 WINE_NX_VKD3D=1
sh horizon-wine/build-amd64-components.sh
```

The component script runs Wine's `configure` and `make include/all`, fetches
Box64 and FFmpeg at their pinned revisions, and builds the CMake target
`wine-nx-runtime-nro` in Docker. The output is
`horizon-wine/build-switch-amd64/wine-nx-runtime.nro`. “AMD64” names the
Windows guest architecture. The Switch executable is AArch64.

### 4. Package and verify the SD-card ZIP

The component script already runs the first command below. To repackage an
existing NRO after editing the packager, start here with `WINE_NX_LLVM_MINGW`
set as above. The second command rebuilds the staging directory, compiles the
setup programs and checks them against the pinned DLL repository.

```sh
python3 horizon-wine/tools/package-amd64.py --fex --vulkan --dxvk --vkd3d
python3 horizon-wine/tools/package-autorun.py
```

Check the **finished archive** in a fresh directory. This checks ZIP CRCs,
runs the repository verifier on the extracted files, compares the extracted
tree to the staged tree, and compares the packaged NRO to the compiler output.

```sh
nro=horizon-wine/build-switch-amd64/wine-nx-runtime.nro
zip=horizon-wine/build-autorun/autorun-258.zip
unzip -tq "$zip"
(
  unpacked=$(mktemp -d)
  trap 'rm -rf "$unpacked"' EXIT
  unzip -q "$zip" -d "$unpacked"
  python3 horizon-wine/tools/verify-package.py "$unpacked/switch/wine" \
    --dlls horizon-dlls/switch/wine
  diff -qr horizon-wine/build-autorun/sd-card "$unpacked"
  cmp "$nro" "$unpacked/switch/wine/wine-nx-runtime.nro"
)
shasum -a 256 "$nro" "$zip"
```

For a fresh installation, extract the ZIP at the SD-card root. The executable
lands at `switch/wine/wine-nx-runtime.nro`. First set-up downloads the Windows
DLLs. For an existing installation, follow the upgrade steps in [the install
guide](docs/INSTALL.md).

**Reference artifacts, 2026-11-21.** The extracted package passed verification.
Hardware testing of this Mac build is pending. These hashes identify the
recorded files. ZIP entry timestamps can change the hash of a later build.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `wine-nx-runtime.nro` | 43,936,578 | `147ce59da6e3df0495816f7ef4432a606b59fb04f27c21bec3f5f5e1db65cbaf` |
| `autorun-258.zip` | 17,014,120 | `a94ead6c161c7eae090065d0ce23874478f6eba38f380963f3005cfbdc9dd1e4` |

### 5. Package the offline Windows components update

This ZIP updates an existing SD installation. It contains the Switch NRO,
the Windows components supported by that build, the installed DLL manifest
and COM registry, and the DLL repository's notice and licenses. Extract it at
the SD-card root and merge the `switch` folder. The launcher may still check
for updates at startup. The installed components work offline. Steam sign-in
requires a network connection.

For a clean build of the Chinese launcher, clone the branch from the fork.
Use the Homebrew/Bison setup in step 1 and the ARM64 toolchain and build
commands in steps 2 and 3 in this checkout. Set the build directory before
running step 3 to use the same paths as the diagnostic build.

```sh
git clone --branch codex/chinese-menu --recurse-submodules https://github.com/lerosua/CorkDeck.git CorkDeck-zh
cd CorkDeck-zh
export WINE_NX_BUILD_DIR="$PWD/horizon-wine/build-switch-network-diag"
```

With the NRO built, run these commands from the checkout root on the Mac.
`--build` takes the directory containing both `wine-nx-runtime.nro` and the
`horizon_dll_features.h` generated by that build. If step 3 used its default
directory, set `build=horizon-wine/build-switch-amd64` instead.

```sh
brew install zstd
git submodule update --init horizon-dlls
build=horizon-wine/build-switch-network-diag
zip="$build/CorkDeck-zh-CN-offline-update.zip"
python3 horizon-wine/tools/package-offline-update.py --build "$build" --output "$zip"
```

The script reads the revision pinned by the launcher's `HORIZON_DLLS_REVISION`
and exports it from the local submodule's Git history. For this build that
revision is `5d6eccb82c266f037dd4305037c986b6303cd59a`. If a shallow
submodule lacks the object, fetch it once with
`git -C horizon-dlls fetch origin 5d6eccb82c266f037dd4305037c986b6303cd59a`.
Packaging runs locally with the Mac compiler and leaves the submodule checkout
in place.

The script uses the launcher's DLL manager to select compatible files, check
their SHA-256 against the pinned manifest and write the installation records.
Before moving the ZIP to the output path, it extracts the archive to a
fresh directory, checks its member list and CRCs, compares all extracted files
(including the NRO, installation records and licenses) with the staged files,
and runs the launcher's SHA-256 and readiness checks again.

Run the following commands to verify an existing ZIP and record the artifact hashes.

```sh
python3 horizon-wine/tools/package-offline-update.py --build "$build" --output "$zip" --verify-only
shasum -a 256 "$build/wine-nx-runtime.nro" "$zip"
```

The tested diagnostic build reports `current=1605 pending=0 unsupported=4 skipped=2`
and `ready=1` on readback. Four files require unavailable features
and two entries use a directory this launcher does not support. ZIP entry
timestamps can change the archive hash between runs.

## Host regression checks on Mac or Linux

Run from the repository root after initializing `horizon-dlls`:

```sh
sh horizon-wine/tests/run-host-checks.sh
# Or select a suite while working on it:
sh horizon-wine/tests/run-host-checks.sh steam
```

On macOS the entry point uses an ARM64 Linux container with SDL, crypto and
archive development libraries. It builds `tests/Dockerfile.host-tests` if the
local `corkdeck-menu-host:2026.10.12` image is absent. This keeps Linux compiler
flags and libnx-only files out of the Mac host compiler. On Linux, install the
packages listed in that Dockerfile before running the same entry point.
The fixtures use synthetic accounts and files. They do not connect to Steam
or install anything on the Switch. Full launcher checks also require the
built loader NPDM (run the NRO build above first).

## Historical upstream baseline on Linux/WSL

Baseline revision: `91107d45eb3204ff80d3637bfa37cefd021a3a1c`.

Steam host checks (Linux/WSL, synthetic data):

```sh
sh horizon-wine/tests/check-steam-host.sh
sh horizon-wine/tests/check-steam-ui-host.sh
sh horizon-wine/tests/check-steam-cdn-host.sh
sh horizon-wine/tests/check-steam-shell-host.sh
```

The CDN check uses an ephemeral local HTTPS server, a locally generated test CA,
and normal certificate/hostname verification. It proves connection reuse on the
host, not Horizon throughput. The shell regression uses upstream synthetic PE
fixtures with the existing Wine renderer because the host harness does not create
a graphics-manager object. It confirms the existing manual Play path returns its
executable without Steam; it does not execute a Windows game on the host.
Status: baseline NRO and ZIP built successfully on 2026-09-30. Upstream package
verification and ZIP integrity checks passed. **Hardware validation is pending**;
the full host test suite also has the failures documented below.

### Host versus target

The target is Nintendo Switch/Horizon, not Linux. Linux/macOS runs the build
tools; devkitA64 cross-compiles the native Switch executable inside Docker.
On Windows, use WSL2 for the upstream POSIX shell scripts and Docker engine.
WSL is not a dependency installed on the Switch and does not replace its runtime.

This workstation uses Ubuntu 22.04 in the existing `AvatarLab` WSL distribution.
Docker and build dependencies were installed there for this task. Its other
application files have not been changed.

```sh
sudo apt-get update
sudo apt-get install docker.io bison flex build-essential pkg-config curl git python3 xz-utils
sudo systemctl start docker
sudo docker info
```

Use a shell authorized to access Docker. That access is effectively root-level
access on the build host; it is not required on the game console.

### Sources and dependencies

Prefer a Linux filesystem checkout with LF line endings and a path without spaces.
Windows Git's automatic CRLF conversion can make `sh build.sh` fail with
`set: Illegal option` before any compilation starts.

```sh
git -c core.autocrlf=false clone https://github.com/autorunhq/autorun.git autorun
cd autorun
git checkout 91107d45eb3204ff80d3637bfa37cefd021a3a1c
git switch -c feature/steam-provider
sh build.sh --jobs 4
```

`build.sh` obtains:

- The exact `horizon-dlls` gitlink revision (do not use submodule `--remote`).
- LLVM-MinGW `20260505`, selecting the Linux x86_64, Linux ARM64 or macOS archive.
- `ghcr.io/autorunhq/switch-dev:2026.09.28.4`, named in
  `horizon-wine/switch-dev.txt`. Record its resolved digest after a successful pull.
- The pinned upstream Atmosphere/Horizon-OC boot payload inputs, followed by the
  existing Wine/Box64/FEX build inputs. No Steam-related runtime upgrades.

The Docker image contains the Switch compiler, libnx, Mesa and other native
dependencies. The script sets `WINE_NX_LLVM_MINGW` and `WINE_NX_JOBS`; it enables
the normal FEX/DXVK/VKD3D package configuration itself. Do not replace component
versions or disable components just to make a Steam build pass.

`--dlls` additionally rebuilds changed Windows DLLs; this is not necessary merely
to obtain the unchanged runtime package. `--x86` selects the optional upstream
x86 runtime; it is not the default baseline. Avoid `--clean` during investigation
unless the generated build directories are expendable.

### Outputs and evidence

The expected native artifact is
`horizon-wine/build-switch-amd64/wine-nx-runtime.nro`; packaging writes
`horizon-wine/build-autorun/autorun-NNN.zip`. The archive contains the SD layout.
The upstream packaging tool validates it against the DLL repository; the Windows
DLL payload is not bundled into this archive.

Before claiming Milestone 0 complete, save the successful build log, image digest,
tool versions, submodule revision, `build-manifest.json`, and SHA-256 of the NRO
and ZIP. Run the upstream package verification. Hardware validation remains a
separate step: launch the baseline, open the existing library and run a previously
working manually added game. Existing logs are under `sdmc:/switch/wine/logs`.

### Current observations

- First attempt: Windows CRLF checkout caused shell failure before compilation.
- Checkout normalization: restore exact upstream bytes for data files that
  intentionally contain CRLF; do not commit blanket line-ending changes.
- Docker 29.1.3 is installed and starts in WSL; the pinned image is downloadable.
- Resolved toolchain image digest:
  `sha256:b0799c2a76177bf0c34a74b6601e4db958fd181800190d46da4402be225d555a`.
- Image inspection: devkitA64 GCC 15.2.0, mbedTLS 2.28.10; curl, libsodium,
  liblzma and zstd are available. Availability alone is not a dependency audit.
- DLL gitlink: `ffb73f3074914f79b04a7c5635b478eaf0ab19e2`.
- Baseline compilation and package verification completed using the repaired
  image described below. No Steam functionality is present in these artifacts.
- Build-only fix: both packaging scripts now honor `WINE_NX_LLVM_MINGW`, already
  exported by `build.sh`, instead of always selecting the macOS compiler path.
  The macOS fallback is preserved. Linux LLVM-MinGW compiled the existing
  `autorun_setup.c` successfully; `llvm-readobj` confirmed COFF-i386 output.
- Existing `launcher_catalog` and `launcher_svg` host tests passed on Linux with
  GCC, AddressSanitizer and UndefinedBehaviorSanitizer. The SVG link requires
  `-lm` on Linux. These checks do not validate Horizon, Steam or the full launcher.
- A build-only copy is being prepared in `/opt/autorun-build` inside WSL to avoid
  cross-filesystem overhead and spaces in the host build path. The editable
  feature checkout remains `D:\autorun fork\autorun`.

#### Host test caveats

Linux host-test compilation also needs `-lm` for the SVG test and an explicit
`stdint.h` include in the SteamGridDB client for `SIZE_MAX`. These are build
portability fixes, not changes to the execution pipeline.

Clang 14 ASan PIE test executables intermittently segfaulted before a diagnostic
on this WSL kernel. A local `/usr/bin/clang -no-pie` wrapper passed ten consecutive
SVG runs; it is used only for host tests, never Switch compilation. The expanded
suite then reported 12,113,216 leaked bytes in the existing `tests/osk.c` font
fixture. The leak-checking suite is **not passing**. A separate UI smoke run may
disable leak detection to continue other checks, but cannot close that finding.
That smoke run passed keyboard checks, initialized the SDL dummy/software UI and
selected the expected EXE, but `Run once` returned `0` with an empty target instead
of the expected `1`. Therefore the full launcher smoke suite also remains failing.
No launch/input/runtime changes were made to conceal or fix this unrelated
baseline behavior. Keep this distinction when comparing future Steam changes.

#### Pinned image defect

The pinned amd64 image cannot link the baseline NRO: its `libvulkan.a` contains
656 AArch64 and 695 x86-64 ELF members. Link fails at operation 349/351 with
`Relocations in generic ELF (EM: 62)` and `file in wrong format`.

The pinned mesa-switch wrapper initializes `RUST_TARGET` only from
`MESA_SWITCH_RUST_TARGET`; the Linux image build did not set it. Thus Mesa's Rust
NVK members were built for the amd64 build host. A rebuild with an explicit Rust
target exposed a second problem: native procedural macro dependencies were mixed
with target dependencies. That experimental rebuild did not succeed.

`build-support/Dockerfile.switch-dev-fixed` instead uses the nine target Mesa
archives linked by Autorun from the official ARM64 variant of the **same** release,
while retaining the amd64 build tools. Its source image is pinned to
`sha256:7aa4c7d8570d4342c2e18344c02ddb8d3c5d84d0d5003ee373f7e89f384f9213`.
No ARM64 host tool is executed. Every copied archive is checked for nonempty ELF
headers and AArch64-only members; the source `libvulkan.a` has 1543 AArch64 members.
The derived image built successfully. This is a build-image repair using the same
Mesa release, not a driver source change. Linking and hardware behavior still
require independent validation.

```sh
docker build -t autorun/switch-dev:2026.09.28.4-fixed \
  -f build-support/Dockerfile.switch-dev-fixed build-support
export WINE_NX_DEVKIT_IMAGE=autorun/switch-dev:2026.09.28.4-fixed
sh build.sh --jobs 4
```

See `documentation/technical.md` and `horizon-wine/README.md` for upstream build
details. Steam architecture and validation gates are tracked in `STEAM.md`.

The Steam download layer additionally links SDK `switch-liblzma` 5.8.2-1 (0BSD)
and reuses existing Zstandard 1.5.7, zlib and minizip. They are already present in
the pinned image. New modules are listed explicitly in CMake; the execution pipeline
and its component versions are unchanged. Host Steam tests need SDL2/SDL2_ttf,
libcurl, wslay, mbedTLS, zlib, minizip, liblzma and Zstandard development packages.
On Ubuntu: `apt-get install libsdl2-dev libsdl2-ttf-dev libcurl4-openssl-dev
libwslay-dev libmbedtls-dev zlib1g-dev libminizip-dev liblzma-dev libzstd-dev
libpng-dev libturbojpeg0-dev`. The artwork module reuses Autorun's existing PNG/JPEG
decoder and SDL UI; no additional Switch dependency was added.

### Baseline artifact record (2026-09-30)

Artifacts and logs are saved in `../artifacts/baseline/` in this workspace.

- `autorun-258.zip`: SHA-256
  `49bfdc1c1f46f623959fe26bae56425f6e3981c14eadee9e55002c425f228faf`.
- `wine-nx-runtime.nro`: SHA-256
  `93e81142f5639b5f0d967c504d7dca0b61537e3ddae904ab336df1d90d82190b`.
- Derived image ID:
  `sha256:8e7dcd7f344ccadb4ac54f3db025a6688488a514b88c9b7087e31b6c526299bc`.
- Linux build copy HEAD: `485f0b2a1d4c5de3e911d5634bd1881692a43470`, plus
  the `stdint.h`/`-lm` fixes recorded in feature commit `3a3bc576`.
- Logs: `autorun-build-baseline-fixed.log`, `autorun-package-baseline.log`.
- The package's `build-manifest.json` is saved alongside them.
- `package-autorun.py` ran `verify-package.py` successfully, then tested ZIP CRCs.

The final successful invocation used `build-amd64-components.sh` with
`WINE_NX_FEX=1 WINE_NX_DXVK=1 WINE_NX_VKD3D=1 WINE_NX_JOBS=4`, the image override
above, and `WINE_NX_LLVM_MINGW` pointing to the downloaded Linux x86-64 LLVM-MinGW
20260505 directory. Packaging used `python3 horizon-wine/tools/package-autorun.py`
with the same LLVM-MinGW variable. Boot payloads were reused from the completed
baseline boot build. No execution source was modified.

Before adding Steam, validate this baseline on the Switch: open the existing
library and launch one manually added game that works with upstream Autorun.
This specifically checks the toolchain repair's native graphics output.
Test instructions are in `../artifacts/baseline/HARDWARE_TEST.md`.
