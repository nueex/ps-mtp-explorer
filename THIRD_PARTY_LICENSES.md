# Third-party notices

**Original-work migration (2026-10-26):** Original Author: nueex — Copyright (C) 2026 nueex. Only nueex's original work is now offered under GPL-3.0-or-later; see [LICENSING.md](LICENSING.md) and [LICENSE_SCOPE.json](LICENSE_SCOPE.json). Third-party terms remain unchanged. The complete application is not cleared for redistribution: libjbc and excluded third-party artwork/branding still require resolution. See [LICENSING.md](LICENSING.md). Attribution and source availability alone do not establish redistribution permission. The [historical MIT grant](LICENSES/MIT-legacy.txt) remains effective for previously licensed material.

PS MTP Explorer includes third-party components with their own terms. This file does not relicense those components. License texts and corresponding OpenOrbis runtime materials are provided in `PS-MTP-Explorer-2.0.0-Third-Party.zip` alongside the compiled release. Application source is not included in that archive.

| Component | Attribution and terms |
| :--- | :--- |
| OpenOrbis PS4 Toolchain runtime modules | OpenOrbis contributors. SDK v0.5.4, commit `b458dfd9d2f5c40aa249b127c9b9b4488afdc686`. The dummy libc/FIOS2 modules and about module retain the upstream GPL v3 terms. Corresponding module sources, runtime support, build files and license are included. |
| PS4 musl libc | Rich Felker and contributors; MIT and component notices. Statically linked runtime and startup code from the OpenOrbis SDK. |
| stb_image / stb_truetype | Sean Barrett and contributors; MIT or public-domain options as stated in the bundled upstream headers. |
| Noto Sans Light | Google and Noto Project contributors; SIL Open Font License 1.1. Font notices are retained. |
| libjbc | sleirsgoevy, with modifications by bucanero. The pinned bucanero/ps4-libjbc snapshot is `835fe016ff0ae5dd89b9249f39cc0fe093fd07dd`. That upstream snapshot does not contain a standalone license file; no MIT/GPL license is assigned to it here. |
| OpenOrbis-derived linker script | `src/ps4/link.x` retains its existing `GPL-3.0-only` identifier and upstream derivation notice. It is not covered by the project's MIT grant, and is not changed to GPL-3.0-or-later. |
| Artwork and wordmarks | Project-specific original launcher/title artwork and authored UI graphics are covered by nueex's scoped GPL grant following the ownership confirmation. The PS4/Sony logo, Bloodborne cover, unresolved demo scenery and other third-party elements remain excluded. The platform SVG and screenshot are mixed works. See [assets/reference/NOTICE.txt](assets/reference/NOTICE.txt) and [LICENSING_ASSETS.json](LICENSING_ASSETS.json); no project MIT/GPL grant is asserted for external elements. |

**libjbc - PS4 Jailbreak C library by sleirsgoevy, with modifications by bucanero. Provided and used with full author attribution for PS4 homebrew execution.**

The compiled integration uses the kernel-access and process-credential portions of libjbc. The release's third-party archive retains the pinned upstream snapshot, attribution and integration sources. Its upstream repository is [bucanero/ps4-libjbc](https://github.com/bucanero/ps4-libjbc), forked from [sleirsgoevy/ps4-libjbc](https://github.com/sleirsgoevy/ps4-libjbc).

Additional upstream contributor: **LightningMods**, whose merged [PR #1](https://github.com/sleirsgoevy/ps4-libjbc/pull/1) adds Sony credential/capability handling used by this integration. Permission review must cover the relevant contributions of sleirsgoevy, LightningMods and bucanero; this credit is not a license grant. The [artwork inventory](LICENSING_ASSETS.json) identifies supplied assets and their embedded/documentation copies needing clearance. These complete-distribution issues do not prevent nueex from separately licensing original work nueex owns under GPL-3.0-or-later.

OpenOrbis packaging/conversion tools and LLVM are build-time tools; their executables are not shipped in the application distribution. No proprietary Sony SDK or console firmware module is bundled.

## stb_image pin for T12-A1 (2026-10-29)

**Upstream branch: `dev` (development snapshot, not a numbered release).**
The compiled image decoder is now the repository-owned
`third_party/stb/stb_image.h`; `stb_truetype` still comes from the SDK.

- Upstream: https://github.com/nothings/stb
- Commit: `2838bd5823c095dd1d5b5d8fac0b4fc8aa72a36d`
- Exact source: https://raw.githubusercontent.com/nothings/stb/2838bd5823c095dd1d5b5d8fac0b4fc8aa72a36d/stb_image.h
- Header version string: `stb_image - v2.30`; the SHA identifies the later fixes.
- SHA-256: `06d445a53ee6172fa41ac382dfed9648a7719943dbe76fc18ae11ec161898416`
- Both original MIT/public-domain license alternatives remain intact in the
  exact upstream header. No local edits or line-ending conversions were made.
- Previous decoder: SDK v2.25, SHA-256
  `281c4f378083e1f195cd06fb98f1ec7eb9749353eb19c915f17c2af46abbafee`.
  It exactly matches the header in OpenOrbis commit
  `b458dfd9d2f5c40aa249b127c9b9b4488afdc686`; no local patches were found.
- `src/metadata.c` preserves static linkage, C99, JPEG/PNG-only selection,
  SIMD/stdio/HDR/linear exclusions and the existing 8192 dimension limit.
  The development header accidentally excludes `stbi__mad4sizes_valid` for
  this combination. The integration supplies that exact upstream helper,
  using the header's checked arithmetic, without editing the header or
  enabling extra decoders. Remove the integration helper when upstream fixes
  its definition guard.

