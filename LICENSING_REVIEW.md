# Licensing review

Reviewed and scoped migration applied: 2026-10-26.
**Original Author: nueex — Copyright (C) 2026 nueex** (original work only).

## Decision

**nueex's original work is now licensed GNU GPL v3.0-or-later
(`GPL-3.0-or-later`)**, following the maintainer's explicit ownership confirmation
and instruction. The exact grant, full/mixed/excluded file inventory and preserved
MIT notice are in [LICENSING.md](LICENSING.md),
[LICENSE_SCOPE.json](LICENSE_SCOPE.json) and
[LICENSES/MIT-legacy.txt](LICENSES/MIT-legacy.txt).

**The complete application/source bundle is not cleared for distribution.**
Available records still do not establish libjbc upstream redistribution permission.
Third-party marks, the Bloodborne cover and unidentified demo scenery remain
excluded; the ownership confirmation does not license external intellectual
property. The grant covers original project title/launcher/UI/design contributions
but does not relicense third-party parts of mixed assets. No earlier binary or
release is certified or retroactively relicensed by this source migration.

The pre-migration MIT license text and contributors' copyright notice are preserved.
The nueex credit does not transfer third-party ownership or withdraw any
previous MIT grant. Previously MIT-licensed versions of original project work
remain usable under their original permissions. This does not extend MIT
permissions to components excluded from that grant.

## Scope and authorship evidence

- Initial local review checkout: `4b051d6fae77a3fedb6cc85ed2828ed4fb36d167`, a single
  non-shallow root commit attributed to NUEEX. The pre-existing README edit
  was preserved.
- The [public main history](https://github.com/nueex/ps-mtp-explorer/commits/164e3fb007154b2ca817fd944f49205b255326ce/)
  observed during review contains three commits through
  `164e3fb007154b2ca817fd944f49205b255326ce`, all attributed to nueex,
  with no additional coauthor trailers. The public root license is MIT.
  The initial local checkout was preserved. For the authorized publication, the
  licensing change is integrated in a separate worktree based on that newer
  revision, retaining its build fixes, BUILD.md and removal of obsolete tools/font.
- Reviewed tracked source/header notices, vendored libjbc and its provenance,
  font licenses, artwork notices/provenance, build/link/package scripts, the
  installed SDK license and component files, and distribution notices.
  Local development notes and stored musl/tool license texts were also consulted.
- Commit authorship supports the original-author credit requested by the
  maintainer, but does not prove exclusive ownership of every imported work.
  No separate assignment, contributor agreement or libjbc permission grant was
  found in the reviewed project records. Existing third-party credits remain.
- The maintainer subsequently expressly confirmed ownership of all original
  application code, UI designs, icons and project-specific artwork, excluding
  external libraries and third-party IP, and authorized this scoped GPL grant.
  That confirmation is the ownership basis for the original-work migration;
  it is not evidence of permission from libjbc contributors or external artists.

## Component findings

| Component | Evidence and implication for migration |
| :--- | :--- |
| Original application code, project design and authored artwork | Now offered as GPL-3.0-or-later to the extent owned by nueex, as explicitly inventoried. Historical MIT permissions and copyright/permission text are preserved separately; no prior grant is withdrawn. |
| libjbc | The complete [pinned upstream tree](https://github.com/bucanero/ps4-libjbc/tree/835fe016ff0ae5dd89b9249f39cc0fe093fd07dd) contains 11 files, with no LICENSE/COPYING file or license/permission notice in its source, headers, README or Makefile. The original [sleirsgoevy repository](https://github.com/sleirsgoevy/ps4-libjbc) also has no standalone license in the root inspected. No issues were returned by the fork's public issue listing. Local [NOTICE.txt](third_party/libjbc/NOTICE.txt) expressly disclaims established redistribution permission. `jailbreak.c` and `kernelrw.c` are compiled and linked into the application. Attribution to sleirsgoevy and bucanero is preserved, but is not a substitute for permission. **Blocks clearance of the complete application.** |
| OpenOrbis SDK v0.5.4 | Project notices pin `b458dfd9d2f5c40aa249b127c9b9b4488afdc686`; the installed SDK includes the [GPLv3 text](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/b458dfd9d2f5c40aa249b127c9b9b4488afdc686/LICENSE). Application linking uses SDK startup/runtime code and stubs; packaging copies the dummy libc/FIOS2 modules and `right.sprx`. These uses must be assessed separately from merely running a GPL compiler or packager. Retain upstream notices and provide the complete corresponding source/build inputs required for distributed GPL-covered components. A root GPL text alone does not establish an upstream "or later" grant. |
| Derived linker script | [src/ps4/link.x](src/ps4/link.x) already declares `GPL-3.0-only`. Preserve that identifier and its derivation notice. GPL-3.0-or-later original code can be used under version 3 alongside GPLv3-only components; that does not make the components available under later versions. Do not claim an unrestricted later-version option for a combination without reviewing these constraints. |
| musl libc | Linked statically from the SDK. Existing notices identify Rich Felker and contributors and MIT plus component-specific notices. The locally retained COPYRIGHT text includes BSD/permissive component terms. Preserve those notices; do not replace them with nueex's copyright. Exact SDK runtime/compiler builtin provenance and notices still need verification for a future release. |
| stb_image and stb_truetype | Installed SDK headers include Sean Barrett's 2017 copyright and the full MIT/public-domain alternatives. The MIT option is GPL-compatible with notice preservation. Keep the upstream headers and notices. |
| Noto Sans fonts | Both bundled notices are SIL OFL 1.1: Noto Sans credits the 2022 Noto Project Authors; Light credits Google LLC, 2015-2021. Preserve both notices. The font remains under OFL, including its embedding/bundling requirements; do not apply the application license to the font data or generated font include. |
| Supplied artwork | The owner confirmation supports the scoped GPL grant for project-specific original launcher/title/UI artwork. External PS4 branding, Bloodborne cover and unidentified demo scenery remain excluded. `assets/icons/platform.svg`, generated mixed arrays and the screenshot receive no whole-file first-party GPL label. Historical provenance notices remain in [assets/reference/NOTICE.txt](assets/reference/NOTICE.txt); current status is recorded in [LICENSING_ASSETS.json](LICENSING_ASSETS.json). |
| Build tools and researched libraries | LLVM/Clang/LLD, Zig, create-fself, create-gp4 and LibOrbisPkg are used as tools; their executables are not listed as application payloads. Any runtime code they inject needs separate review. The source review notes identify libmtp and AOSP as protocol references rather than copied/linked implementations; neither appears in the application's compile/link inputs. Their licenses must be assessed if code is imported later. |

## Follow-up: original code versus complete distribution

The following distinctions correct an overly broad reading of the initial
decision and its "once cleared" sequence:

- **Own code:** A copyright holder can offer their own original contributions
  under GPL-3.0-or-later now. No permission from libjbc's authors is needed to
  license unrelated code nueex owns. The grant must expressly exclude imported
  code, fonts and artwork. Existing contributors' rights and historical MIT
  permissions remain; do not claim exclusive ownership from commit names alone.
- **Source distribution:** A repository/archive containing someone else's
  implementation or artwork still needs permission for those contents.
  Publishing a GPL label does not fix missing redistribution rights in libjbc.
  A separate export of only owned/licensed materials is possible, but would not
  represent clearance of the complete, buildable application. No files were
  removed to create such an export in this investigation.
- **Binary distribution:** libjbc's two implementation units are linked into
  the application; the unresolved rights apply to the PKG as well. GPL-covered
  runtime components, their source obligations and separately licensed fonts
  must also be handled. Merely putting a notice beside an unlicensed library,
  dynamically linking it, or shipping it separately is not a permission grant.
- **Artwork:** It can retain a separate license; it need not all become GPL.
  It does need an adequate permission basis for the intended distribution.
  Copyright permission and permission to use branding/trademarks are distinct.
- **Version scope:** The explicit `GPL-3.0-only` marker in the derived linker
  script stays. Original GPL-3.0-or-later code can be used under version 3 with
  GPLv3-only material; do not promise that every component can move to a later
  version. A GitHub `GPL-3.0` badge or the date on the GPL document alone does
  not determine an author's version-selection grant; inspect the actual notices
  and GPL section 14 for each component.

The initial investigation deferred a grant. The maintainer has now authorized
and this checkout applies the scoped original-work GPL grant. The remaining
complete-distribution requirements below do not condition that original-work
permission; the historical MIT grant also remains effective for old material.

## Follow-up: libjbc permissions and contributors

Upstream checks on 2026-10-26 found GitHub license metadata `null` for both
repositories. Neither inspected current tree contains a license file; the pinned
fork's source/header/README files contain no express grant. The original branch
head is `4dd18714fa7af7e15813d6a11537b60bb8c6f79c`; bucanero's head is still the
project's pinned `835fe016ff0ae5dd89b9249f39cc0fe093fd07dd`.
Each repository's branch listing contains only `master`; read-only
`git ls-remote --tags` checks returned no tags for either repository. No
alternative licensed branch or tagged libjbc release was identified.

The [complete upstream history](https://github.com/sleirsgoevy/ps4-libjbc/commits/4dd18714fa7af7e15813d6a11537b60bb8c6f79c/)
and [fork history](https://github.com/bucanero/ps4-libjbc/commits/835fe016ff0ae5dd89b9249f39cc0fe093fd07dd/)
identify **three relevant contributor identities**, not just the two credited
in the original notice. These are evidence of contributions, not a legal
determination of current copyright ownership or assignments:

| Contributor / prospective permission contact | Relevant evidence and scope |
| :--- | :--- |
| [Sergey Lisov / sleirsgoevy](https://github.com/sleirsgoevy/ps4-libjbc) | Initial implementation (`192917a`), process/prison/vnode logic, kernel access and later reference-count fixes. Permission for the original implementation is essential unless an existing applicable grant is produced. |
| [LightningMods](https://github.com/LightningMods/ps4-libjbc) | Merged [PR #1](https://github.com/sleirsgoevy/ps4-libjbc/pull/1): credential/authentication changes in `jailbreak.c` and `jailbreak.h`. Commits `058cdc6`, `128e56c`, `40d81a9` and `7bbd245` add/read/write/order Sony credentials and capabilities. Those fields remain in the local integration and are used by the current access profile. The PR explains AppInstUtil module-loading failures without the changes; its body and empty comment listing supply no explicit license grant. |
| [Damián Parrino / bucanero](https://github.com/bucanero/ps4-libjbc) | [Commit `730cdae`](https://github.com/bucanero/ps4-libjbc/commit/730cdae2900e86fea163b294764106ee916c7844) adds the OpenOrbis static-library build and public `libjbc.h`; later merges incorporate upstream changes. These changes do not establish authority to license the other contributors' code. The local public header has been adapted; preserve the existing integration credit and cover any retained contributions in the permission request. |

The merged PR in an unlicensed repository is not treated here as a substitute
for a clear GPL-compatible grant. The fork's public issue listing is empty;
the original's listing contains PR #1, whose comments were also checked.

A repository search returned these additional forks, whose recursive current
`master` trees were checked for LICENSE/COPYING/NOTICE filenames:
`cy33hc`, `cualquiercosa327`, `ItsJokerZz`, `xfangfang`, `illusionyy`,
`freetushkan`, `LightningMods`, `Map987`, `jusdaveman`, `turbo420`, `OSM-Made`
(each under `https://github.com/<owner>/ps4-libjbc`). No standalone license file
was found in those eleven trees. This filename search is not an exhaustive audit
of every fork's header/history or private permissions. Even a newly added fork
license would need authority over inherited code; it cannot cure that gap by
itself. No compatible licensed libjbc drop-in was established by this search.

### Preferred resolution: permission without changing functionality

Ask the relevant rights holders to publish an upstream license covering the
pinned implementation, or retain explicit written grants for their respective
contributions. **MIT is a practical request** because it permits GPL-compatible
use and minimizes later-version constraints. GPL-3.0-or-later is another suitable
request. GPLv3-only can work for a version-3 combination, but must remain marked
as such. An informal "homebrew use is fine", attribution request, no-commercial
restriction, or permission only to run binaries is insufficient for the planned
GPL source/binary redistribution.

Request wording, prepared for the maintainer to send; **not sent**:

> PS MTP Explorer incorporates a modified version of ps4-libjbc pinned at
> 835fe016ff0ae5dd89b9249f39cc0fe093fd07dd. We want to distribute its source and
> PS4 binaries with our original application code under GPL-3.0-or-later while
> preserving your credits. Would you publish an MIT license, or explicitly grant
> the MIT license for the contributions you own in that snapshot, permitting
> modification and source/binary redistribution, including commercial use?
> Please identify the covered files/contributions, the applicable copyright
> names/years, and any other rights holders whose permission is needed. If you
> prefer GPL-3.0-or-later, please state that exact alternative. We will retain
> the grant, complete license text and attribution with our source and releases.

Record the response URL or permission document, identity, covered revision/files,
license text and any limitations. A contributor can grant only rights they hold;
obtain separate grants or evidence of authority for the others. Apply the grants
only to their stated scope. This is preferable to changing the tested access
implementation purely to change its license status.

### Alternatives investigated; none installed

| Route | Licensing evidence | Functional limitation / next step |
| :--- | :--- | :--- |
| Keep current integration with documented permission | The permission route above | Best chance of preserving existing behavior. Local modifications and provenance remain; verify source/runtime notice delivery after the grant. |
| [ps4dev/ps4sdk privilege routines](https://github.com/ps4dev/ps4sdk/blob/4df9d001b66ae4ec07d9a51b62d1e4c5e270eecc/common/kernel/source/kernel/privilege.c) | Its [LICENSE](https://github.com/ps4dev/ps4sdk/blob/4df9d001b66ae4ec07d9a51b62d1e4c5e270eecc/LICENSE) is the Unlicense/public-domain dedication with a permissive fallback and expressly excepts BSD/MIT third-party components. This offers a potentially compatible source basis, subject to file/dependency provenance review. | The inspected routine uses kernel symbol lookup for `prison0`/`rootvnode`, changes Unix identity and lacks this app's Sony credential/capability handling and validated prison/vnode lifetime management. It is not an OpenOrbis/GoldHEN drop-in or a verified multi-firmware replacement. Port only with a full behavior specification and hardware validation. |
| [Payload Guest](https://github.com/Al-Azif/ps4-payload-guest/tree/f73b7fc701ff1b91e40369486325b440b5816c9e) approach | Source files carry GPL-3.0 notices and the repository includes GPLv3. Its [v0.97 release](https://github.com/Al-Azif/ps4-payload-guest/releases/tag/v0.97) explicitly says libjbc was removed and USB access uses a LightningMods method. | Evidence that a different USB-access workflow exists, not proof of equivalent filesystem/installer privileges. Its application scope is payload loading, and inspected code does not implement this app's full libjbc credential profile. Further trace its access path before any port. Plain USB access is insufficient evidence for /data writes, capacity queries and AppInstUtil/BGFT access here. |
| Independent access implementation | New original code can use an explicit compatible grant; independently licensed kernel/SDK references can be considered with their notices | Last resort: implement from a behavior specification and independently usable sources. Reformatting/renaming libjbc is not independent authorship. Because this project has already inspected libjbc, do not describe a rewrite by the same author as a verified clean-room implementation. It requires a separate provenance and regression review. |
| Other SDK/HEN projects | Scene-Collective/ps4-payload-sdk's inspected tree and GitHub metadata do not establish a root license. Mira's root GPL text alone does not establish every imported component's rights. | No blanket recommendation to copy code or add a whole SDK/HEN. Investigate individual licensed files only if the preferred permission route fails. |

Any replacement must preserve the current runtime GoldHEN syscall-11 probe,
current-process and layout checks, original Unix UID/GID ownership, Sony
credential/capability profile, correct prison/vnode references, global filesystem
access and fail-closed behavior. The current startup stops normal operation
when `psmtp_process_access()` fails. Removing that gate or the library would
break behavior; it is not a licensing fix.

Before replacing it, run existing process-access/libjbc host models and then
test each supported real firmware/HEN combination: startup, existing /data
directories, free-space queries, internal/USB/phone read-write and copy/move/
rename/delete, large transfers, AppInstUtil/BGFT module initialization and PKG
installation from all three sources, cancellation, error paths and shutdown.
Host tests alone cannot establish kernel ABI or console stability.

## Follow-up: artwork clearance and replacement map

[LICENSING_ASSETS.json](LICENSING_ASSETS.json) records the reviewed files' SHA-256
identities, source records and disposition. The inventory is evidence of what
was inspected, **not a grant of rights**. Visual inspection identified a
Bloodborne cover in `thumb_pkg.png`, scenery in `thumb_image.png` and
`thumb_video.png`, and PS4 branding in the platform mark. No precise ownership
claim is made for the anonymous scenery. The maintainer now confirms ownership
of the original project-specific launcher/title artwork; that grant does not
extend to other rights holders' content or fonts.

| Material needing permission or replacement | Where it is distributed / used | Practical resolution |
| :--- | :--- | :--- |
| `assets/reference/icon0.png`; identical `assets/icon.png` | Packager copies `icon0.png` to `sce_sys/icon0.png`; README uses `icon.png`. Provenance points to supplied `app icon.jpg`. | Original project-specific artwork is now covered by nueex's ownership confirmation and scoped GPL grant. No visual replacement is needed for that original work. Any independent third-party elements would still retain their own rights; preserve editable originals and font provenance for future releases. |
| `assets/reference/wordmark.png` | Supplied title crop from `1792455114194.jpg`; its exact RGBA bytes are embedded as `ui_wordmark` in `src/ps4/reference_assets.inc` and displayed in the main header. | Original project-title artwork is now covered on the maintainer's ownership confirmation. Font/typeface rights are not reassigned; the font files retain their existing terms. The embedded owned title pixels are covered without relicensing the rest of the mixed include. |
| `assets/icons/ps4-wordmark.png`, `assets/icons/platform.svg`, `assets/reference/platform.png` | `platform.svg` embeds the supplied PS4 logo; its raster is embedded as `ui_platform` and displayed in the header. | Retain source/branding permission if available, or use a plain-text platform label in a licensed font alongside the existing authored gamepad. Do not label the whole SVG as original merely because its gamepad is authored. Preserve non-affiliation/trademark context. |
| `assets/reference/thumb_pkg.png` | Bloodborne cover excerpt, embedded as `ui_thumb_pkg`; included in the README screenshot. | Permission from the relevant rights holder, or replace with an original generic package illustration. A user supplying a screenshot does not by itself license the game artwork. |
| `assets/reference/thumb_image.png` and `thumb_video.png` | Anonymous scenery crops, embedded as `ui_thumb_image`/`ui_thumb_video`; included in the README screenshot. | Establish creators, source and a suitable grant, or replace with original/licensed demo scenery. Video-play glyph can be independently drawn; that alone does not clear its background. |
| `src/ps4/reference_assets.inc` | Generated C arrays contain all the above UI crops (except the launcher). | Regenerate from cleared replacements; changing PNGs without rebuilding this include leaves the old images in the executable. Exact pixel equality of the five disputed UI PNGs with their arrays was verified. |
| `assets/explorer.png` | README screenshot includes supplied title/PS4 mark and all three demo thumbnails. | Re-render after replacements, or obtain rights adequate for this documentation use. Do not treat a rendered app screenshot as automatically clearing the artwork displayed inside it. |

**Correction to the historical "QA-only" notice:** all three demo thumbnail
arrays occur in the generated include and are referenced by `ui_draw.c`'s asset
table. `console_render.c` initializes live item thumbnails to `-1`, but injected
display items can select the demo assets. They are embedded inputs to the
executable, even if ordinary browsing does not display them. Do not infer
redistribution safety from the QA label or assume compiler/linker elimination.

The independently authored geometric SVGs (`folder`, `document`, `phone`, `usb`,
`drive`, `settings`, `home`, `copy`, `move`, `rename`, `delete`, `info`, `gamepad`)
already have an application-MIT attribution basis in the existing asset notice;
their generated raster counterparts need not be replaced solely for GPL use.
The composite `platform.svg` is the exception above. Noto Sans/Light retain
their OFL notices; no font replacement is needed merely to use GPL original code.
No reason was found to remove live user-file previews or PKG artwork extraction:
those capabilities are separate from redistributing fixed demo images.

The owner confirmation and scoped grant now document the original project
artwork. Permission or replacement remains necessary for the excluded external
elements; replacing fixed decorative/demo elements need not remove file
management, USB/MTP transfers, previews or installation functionality. No
artwork pixels or features were removed or replaced by this migration.

## Apollo comparison

[Apollo Save Tool's license section](https://github.com/bucanero/apollo-ps4/blob/main/README.md#license)
credits Damian Parrino and explicitly offers GNU GPL version 3 or any later
version. Its README separately credits contributors and lists dependencies,
including libjbc. That is an appropriate model for the future first-party
notice, but does not establish a license grant from libjbc's rights holders to
PS MTP Explorer. Apollo's author notices must not be copied onto nueex's work,
and its GPL grant must not be treated as relicensing this separate dependency.

## Remaining complete-distribution work and migration record

1. Obtain and retain a written GPL-compatible license/permission grant covering
   the pinned libjbc code and relevant upstream contributions (including
   sleirsgoevy, LightningMods and bucanero), or replace the implementation with code whose
   provenance and license permit the intended distribution. A linking exception
   from nueex alone cannot grant rights to libjbc.
2. Document permissions and ownership for supplied artwork, or replace it with
   suitably licensed assets. Preserve third-party and trademark notices.
3. **Completed for owned original work:** maintainer ownership confirmation,
   exact file/contribution scope and complete historical MIT text/notices are
   recorded. Do not rewrite historical tags, archives or their permissions.
4. **Completed for this revision:** `LICENSE` now contains the unmodified GPLv3 text;
   README, scope documentation and original source headers explicitly designate
   GPL-3.0-or-later. Third-party/mixed files receive no blanket license header.
   The effective date is 2026-10-26; no commit/release identifier is invented.
5. Update release notices and source packaging together. Verify that recipients
   receive the complete corresponding application and required runtime source,
   editable inputs and build scripts. The old third-party archive alone is not
   complete corresponding source for a newly GPL-covered application.
6. The full author/copyright, scope and warranty details are now in README and
   legal notices. Keep the visible "by nueex" and OpenOrbis About module intact
   to satisfy the instruction not to change UI appearance. A future About change
   must retain upstream credits and receive layout/package validation.

For a complete release, resolve the outstanding parts of steps 1-2 and verify
the updated legal notices/source packaging. `distribution/LICENSES.txt` includes
the current scoped notice before all retained old legal text. Current source
includes the scope, inventory and historical MIT notice. The initial local
review also tested legacy archive helpers, but those helpers were removed
upstream and are not restored in the published licensing change.
Regenerate the asset include/launcher/screenshot only if assets change, package
the full corresponding source and applicable third-party licenses, and validate
a fresh production build and independently extracted PKG. Source must match
the exact binary revision, including modified libjbc and required runtime/build
inputs; include applicable GPLv3 installation information if required by the
actual distribution circumstances. Do not assert a blanket system-library
exception for libjbc. Keep previous tags/releases and permissions intact.

The original-work migration is complete for this revision; distribution clearance for the
whole application remains unresolved. No permission request was sent and no
upstream grant was fabricated. The original-work grant and legal notices do not authorize redistribution of
excluded materials without the necessary rights. Publishing this scoped notice
does not assert that inherited libjbc code has redistribution permission.

## Reference guidance

- [GNU license compatibility list](https://www.gnu.org/licenses/license-list.html#Expat)
  describes MIT/Expat as GPL-compatible.
- [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html#GPLIncompatibleLibs)
  explains why exceptions require the relevant copyright holders' authority.
- [Choose a License: no license](https://choosealicense.com/no-permission/)
  explains why a public repository without a license does not by itself grant
  general redistribution and modification rights.
- [SIL Open Font License](https://openfontlicense.org/)
  provides the font-specific licensing and bundling guidance.

Application source/header and build-script edits add license comments only.
The newer upstream build behavior is retained. No image/font bytes, inherited
libjbc code, linker script or historical distribution artifacts are changed by
this licensing commit. The maintainer explicitly authorized committing and
pushing the scoped source/documentation update, without new binaries, releases
or claims of libjbc permission. Verification results are recorded in
[LICENSING_VALIDATION.md](LICENSING_VALIDATION.md).
