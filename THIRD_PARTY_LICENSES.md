# Third-party notices

PS MTP Explorer includes third-party components with their own terms. This file does not relicense those components. License texts and corresponding OpenOrbis runtime materials are provided in `PS-MTP-Explorer-1.01-Third-Party.zip` alongside the compiled release. Application source is not included in that archive.

| Component | Attribution and terms |
| :--- | :--- |
| OpenOrbis PS4 Toolchain runtime modules | OpenOrbis contributors. SDK v0.5.4, commit `b458dfd9d2f5c40aa249b127c9b9b4488afdc686`. The dummy libc/FIOS2 modules and about module retain the upstream GPL v3 terms. Corresponding module sources, runtime support, build files and license are included. |
| PS4 musl libc | Rich Felker and contributors; MIT and component notices. Statically linked runtime and startup code from the OpenOrbis SDK. |
| stb_image / stb_truetype | Sean Barrett and contributors; MIT or public-domain options as stated in the bundled upstream headers. |
| Noto Sans Light | Google and Noto Project contributors; SIL Open Font License 1.1. Font notices are retained. |
| libjbc | sleirsgoevy, with modifications by bucanero. The pinned bucanero/ps4-libjbc snapshot is `835fe016ff0ae5dd89b9249f39cc0fe093fd07dd`. That upstream snapshot does not contain a standalone license file; no MIT/GPL license is assigned to it here. |

**libjbc - PS4 Jailbreak C library by sleirsgoevy, with modifications by bucanero. Provided and used with full author attribution for PS4 homebrew execution.**

The compiled integration uses the kernel-access and process-credential portions of libjbc. The release's third-party archive retains the pinned upstream snapshot, attribution and integration sources. Its upstream repository is [bucanero/ps4-libjbc](https://github.com/bucanero/ps4-libjbc), forked from [sleirsgoevy/ps4-libjbc](https://github.com/sleirsgoevy/ps4-libjbc).

OpenOrbis packaging/conversion tools and LLVM are build-time tools; their executables are not shipped in the application distribution. No proprietary Sony SDK or console firmware module is bundled.
