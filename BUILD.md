# Build PS MTP Explorer

The repository includes the source, libjbc integration, fonts, and artwork
for version **1.01**, with title ID `NUEX00001` and PS4 SFO version `01.01`.
You can build an installable PKG using the GitHub Actions workflow or build
locally on your computer.

## Build with GitHub Actions (no local setup)

The [Build PKG workflow](.github/workflows/build-pkg.yml) installs all required
tools and builds the production package on GitHub. You do not need to install
Python, LLVM, .NET, or the OpenOrbis SDK on your computer for this method.

1. Fork this repository to your GitHub account, or use a repository where you
   have permission to run workflows. Make sure the workflow file is committed
   to the default branch.
2. Open the repository's **Actions** tab. If workflows are disabled in your
   fork, enable them.
3. Select **Build PKG** in the workflow list.
4. Click **Run workflow**, select the branch to build, and confirm
   **Run workflow**.
5. Open the new run and wait for **Build and validate PS4 PKG** to succeed.
6. Under the run's **Artifacts** section, download **PS-MTP-Explorer-PKG**.
7. Extract the downloaded ZIP to get
   `IV0000-NUEX00001_00-NUEEXMTP00000000.pkg`, ready to install using GoldHEN.

The workflow also runs automatically on pushes and pull requests. It uses
Python 3.12, LLVM 18.1.8, .NET 9, and the hash-verified OpenOrbis v0.5.4 SDK
on a Windows runner, then builds, inspects, and audits the production package.

Build and validation reports are available as
**PS-MTP-Explorer-build-reports**, including available reports after a failure.
Artifacts are retained for 30 days.

## Local build requirements

For a local build, install the external compiler and SDK binaries separately:

- **Python 3.12 or later**, available as `python` (or `python3` on Unix).
- **OpenOrbis PS4 Toolchain v0.5.4**, including its headers, libraries,
  packaging tools, and `samples/hello_world` runtime/about modules.
- **Clang and LLD 18.1.8**, the release compiler/linker. Install LLVM
  separately; the SDK bootstrap below does not install LLVM.
- For **Windows packaging**, an x64 .NET runtime. The SDK's PkgTool targets
  .NET Core 3.0; the build enables major-version roll-forward and has been
  verified with .NET 9. Check with `dotnet --list-runtimes`.

Downloads: [Python](https://www.python.org/downloads/),
[OpenOrbis v0.5.4](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/tag/v0.5.4),
[LLVM 18.1.8](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8),
and [.NET](https://dotnet.microsoft.com/download/dotnet).

This build audit verified Windows. Linux/macOS builds need working versions
of the SDK's platform-specific conversion/packaging tools and their runtime
dependencies; they were not executed in this audit.
No proprietary Sony SDK, Node.js, Pillow, test fixtures, sibling resource
folders, or previous build output is required to compile the application.

## Clone and install the SDK

```sh
git clone https://github.com/nueex/ps-mtp-explorer.git
cd ps-mtp-explorer
python tools/bootstrap_openorbis.py
```

The script downloads the official SDK archive, checks its pinned SHA-256,
and extracts it under `.tools/sdk/OpenOrbis/PS4Toolchain`. Network access
is required for the download; builds can then use the local SDK offline.
Downloaded SDK/compiler binaries and build outputs are excluded from Git.

Alternatively, use an existing SDK by setting `OO_PS4_TOOLCHAIN` or passing
`--sdk "/path/to/PS4Toolchain"`. Keep the SDK's sample runtime modules:
packaging uses its open-source dummy libc/FIOS2 modules and about module.

## Compile and package

From the repository root, with `clang` and `ld.lld` on PATH:

```sh
python tools/build_ps4.py --production --package --cc clang --ld ld.lld --output build-ps4
python tools/inspect_ps4.py build-ps4
python tools/verify_production_release.py build-ps4
```

Explicit paths are also supported, including paths containing spaces:

```sh
python tools/build_ps4.py --production --package --sdk "/path/to/PS4Toolchain" --cc "/path/to/clang" --ld "/path/to/ld.lld" --output build-ps4
python tools/inspect_ps4.py build-ps4 --sdk "/path/to/PS4Toolchain"
python tools/verify_production_release.py build-ps4
```

On Windows use `.exe` executable names; for example PowerShell accepts
`--cc "C:/Program Files/LLVM/bin/clang.exe"` and
`--ld "C:/Program Files/LLVM/bin/ld.lld.exe"`.
Without `--cc`, the build detects `.tools/llvm18/bin/clang.exe`, then a local
`.tools/ziglang/zig[.exe]`, then `clang` on PATH. Pass `--ld` explicitly
when selecting a particular linker. Otherwise the build detects LLD beside
the local LLVM installation or as `ld.lld` on PATH.

The installable file is:

```text
build-ps4/IV0000-NUEX00001_00-NUEEXMTP00000000.pkg
```

The output directory also contains the ELF, OELF, `package/eboot.bin`, an
independently extracted PKG payload, and JSON build/inspection/audit reports.
Packaging checks hashes/signatures, extracts the PKG independently, and
compares its payload and identifiers with the staged inputs. The final audit
checks production metadata and the absence of debug/test payloads and private
paths. Run the inspection command before the production audit.

Omit `--package` to compile/link only. Use `--experimental-usb` instead of
`--production` for the diagnostic build with logging. Production builds
disable diagnostic logging. The scripts do not deploy to a console; install
the resulting PKG manually using GoldHEN.

GNU Make is an optional wrapper (`PYTHON` defaults to `python3`):

```sh
make -f Makefile.ps4 PYTHON=python package
```

Set `OO_PS4_TOOLCHAIN` for an external SDK. Add `EXPERIMENTAL_USB=1` for a
diagnostic build. The Python commands above do not require Make.

## Optional host library build

CMake builds the portable core as a static library, not a desktop app or
PS4 package. Install the SDK first for its stb headers, plus CMake 3.16+,
a C99 compiler, and a supported build tool. With Clang and Ninja on PATH:

```sh
cmake -S . -B build-host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-host
```

For an external SDK, add `-DOO_PS4_TOOLCHAIN="/path/to/PS4Toolchain"` during
configuration, or set the `OO_PS4_TOOLCHAIN` environment variable.
Test harnesses are not required by this target.

## Assets and notices

Normal builds regenerate the embedded font from the bundled, hash-checked
`assets/fonts/NotoSans-Light.ttf`. Artwork is already embedded in the
committed `src/ps4/reference_assets.inc`; no asset-generation step is needed.
Editable SVGs and PNGs plus provenance remain in `assets/`.

For optional artwork editing, `tools/prepare_clean_icons.mjs` exports
`prepareCleanIcons({sharp})`. It rasterizes the SVGs and rebuilds the embedded
artwork from the bundled PNGs. This editing workflow requires Node.js and
the `sharp` package; those dependencies are unnecessary for application builds.

Retain [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md),
`distribution/LICENSES.txt`, and the notices under `assets/` and
`third_party/libjbc/` when redistributing applicable materials.

Successful compilation and package checks do not establish compatibility
with every firmware, GoldHEN version, phone, or USB device. Console behavior
still requires testing on the intended hardware.
