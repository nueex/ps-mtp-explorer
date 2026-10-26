# PS MTP Explorer licensing scope

**Original Author: nueex — Copyright (C) 2026 nueex**

Effective for this source revision: **2026-10-26**, based on nueex's express
confirmation of authorship and ownership of the project's original code,
project-specific UI designs, icons and artwork. No historical release is changed.

## Grant for original work

PS MTP Explorer's original work by nueex, identified in
[LICENSE_SCOPE.json](LICENSE_SCOPE.json), is free software/work: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version (**GPL-3.0-or-later**).

This work is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE) for details.
A complete, unmodified copy of GPLv3 is in `LICENSE`.

This is a grant of nueex's rights in nueex's original contributions. It does not
grant ownership of, or additional permissions for, any other person's work.
The exclusions below define the scope of this grant; they do not add restrictions
to the GPL rights granted for the covered original work.

## File inventory

The explicit file lists in [LICENSE_SCOPE.json](LICENSE_SCOPE.json) identify:

- **Fully original files:** application C files, application headers, project
  scripts/configuration, original project prose/metadata, independently authored
  geometric SVGs and their raster versions, and the project-specific launcher
  and title artwork. The source headers use `SPDX-License-Identifier:
  GPL-3.0-or-later`. Artwork is identified in the inventory without changing its
  pixels or introducing metadata into the image files.
- **Mixed files:** the grant covers only nueex's original contributions.
  `platform.svg` and its raster/embedded counterpart include third-party PS4
  branding. `assets/explorer.png` combines the original UI with third-party or
  unresolved images. `reference_assets.inc` combines owned graphics with those
  excluded images. Original independently copyrightable local changes to libjbc
  are covered only to the extent nueex owns them; the upstream implementation
  is not. Original changes to the OpenOrbis-derived linker script do not alter
  that file's existing GPL-3.0-only terms. Do not assign a whole-file GPL-3.0-or-later
  identifier to any of these mixed files.
- **Excluded files:** fonts and generated font data, externally owned or
  unresolved artwork, upstream library contents, third-party provenance/legal
  notices, and license texts. They retain their separate terms. The inventory's
  excluded entries apply even if a file is under `src/` or `assets/`.

The project prose in README and licensing documents is covered only as original
prose; quotations, reproduced legal notices, references and third-party material
retain their own rights. The GPL text itself is copyrighted by the Free Software
Foundation, not nueex. Ignored SDKs, local research clones, historical build
artifacts and other unlisted local files are not relicensed by this inventory.
New files need their own appropriate licensing/provenance review.

## Historical MIT permissions

[LICENSES/MIT-legacy.txt](LICENSES/MIT-legacy.txt) preserves the complete
pre-migration MIT license, including both nueex's copyright credit and the
existing `Copyright (c) 2026 PS MTP Explorer contributors` notice. It is a
historical notice, not a blanket MIT grant for new contributions after this
migration or for excluded components.

Previously MIT-licensed versions and material retain their original permissions.
Adding GPL notices does not withdraw earlier MIT grants, including permissions
for unchanged material already received under MIT. Neither the old MIT license
nor this GPL grant supplies missing rights in libjbc or third-party artwork.
Do not rewrite old tags, packages, archives or their notices to suggest otherwise.

## Separate third-party terms and unresolved distribution rights

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and the per-asset
[LICENSING_ASSETS.json](LICENSING_ASSETS.json) record.

- **libjbc:** upstream redistribution permission remains unresolved. Credits to
  sleirsgoevy, LightningMods and bucanero and all existing notices are retained.
  The grant for nueex's local additions cannot license their inherited code.
  No linking exception or upstream permission grant is asserted here.
- **OpenOrbis:** its SDK/runtime notices and the derived linker script's
  `GPL-3.0-only` identifier remain. The original application work can be used
  under GPL version 3 with GPLv3-only components; that does not make those
  components available under later versions.
- **musl/stb:** upstream MIT/permissive/public-domain alternatives and
  component notices remain as supplied; they are not replaced by nueex's GPL.
- **Noto fonts:** SIL OFL 1.1 and both original notices remain, including for
  the generated embedded font data.
- **Branding/demo artwork:** Sony/PS4 marks, the Bloodborne cover and demo
  scenery without an established ownership/license record remain excluded.
  The authorship confirmation for original project design is not permission for
  third-party intellectual property. No trademark rights are granted.

**The complete repository/source bundle and compiled PS4 application have not
been cleared for distribution merely by this original-work migration.**
Obtain the necessary libjbc permissions and resolve excluded artwork, then
verify corresponding-source/runtime obligations before making a new release.
This outstanding work does not suspend the GPL grant for nueex's original work.

## libjbc provenance and permission

The integration is based on [bucanero/ps4-libjbc at
835fe016ff0ae5dd89b9249f39cc0fe093fd07dd](https://github.com/bucanero/ps4-libjbc/tree/835fe016ff0ae5dd89b9249f39cc0fe093fd07dd),
forked from [sleirsgoevy/ps4-libjbc](https://github.com/sleirsgoevy/ps4-libjbc).
The pinned source tree has no explicit license file or express source/header
license grant established by the review dated 2026-10-26. Public availability,
attribution, inclusion of source and use by other GPL projects do not by
themselves establish redistribution permission for this dependency.

The relevant upstream contributors identified in its history are:

- **Sergey Lisov / sleirsgoevy:** original implementation, kernel access,
  process/prison/vnode handling and reference-count fixes.
- **LightningMods:** Sony credential/capability handling in the merged
  [PR #1](https://github.com/sleirsgoevy/ps4-libjbc/pull/1), including fields used
  by this application's access profile and system-module loading.
- **Damián Parrino / bucanero:** OpenOrbis static-library/public-header
  integration in [commit 730cdae](https://github.com/bucanero/ps4-libjbc/commit/730cdae2900e86fea163b294764106ee916c7844)
  and subsequent upstream merges.

These contributions identify prospective permission contacts, not proof of
current ownership or authority to license other contributors' work. Keep the
input hashes in [third_party/libjbc/provenance.json](third_party/libjbc/provenance.json)
and integration-change record in [NOTICE.txt](third_party/libjbc/NOTICE.txt).
Only `jailbreak.c` and `kernelrw.c` are compiled into the application.

The preferred resolution is an upstream GPL-compatible license or retained
written grants covering all relevant rights in that pinned code. MIT or
GPL-3.0-or-later are suitable requests; GPLv3-only material can be used under
version 3 without becoming available under later versions. Grants must permit
the intended modification and source/binary redistribution; informal homebrew
approval or attribution alone is insufficient. Retain the grant, identity,
covered revision/files and complete license terms. A grant or linking exception
from nueex cannot supply rights held by libjbc's upstream authors.

The [ps4sdk privilege routines](https://github.com/ps4dev/ps4sdk/blob/4df9d001b66ae4ec07d9a51b62d1e4c5e270eecc/common/kernel/source/kernel/privilege.c)
and [Payload Guest's removal of libjbc](https://github.com/Al-Azif/ps4-payload-guest/releases/tag/v0.97)
were investigated as alternatives; neither is a verified drop-in for this app's
complete filesystem/installer credential profile. Do not remove the library or
bypass the startup access checks merely to change licensing status. Any
replacement needs independent provenance/license review and must preserve the
GoldHEN runtime probe, original Unix ownership, Sony credentials/capabilities,
prison/vnode lifetime handling, failure checks and all transfer/install functions.
Host checks alone do not establish console kernel ABI compatibility.

[Apollo Save Tool's GPL notice](https://github.com/bucanero/apollo-ps4/blob/main/README.md#license)
provides an original-author/version-3-or-later model; it does not grant rights
to this separate libjbc dependency.

## Artwork and complete-release requirements

The maintainer's ownership confirmation covers original project launcher/title
artwork and UI/design, as inventoried. It does not license external intellectual
property. The remaining fixed-asset issues are:

- `assets/icons/ps4-wordmark.png`, the composite `platform.svg` and its raster
  contain PS4 branding; only the authored gamepad/design portions are covered.
- `assets/reference/thumb_pkg.png` contains a Bloodborne cover excerpt;
  `thumb_image.png` and `thumb_video.png` contain scenery without an established
  ownership/license record. All three are embedded in `reference_assets.inc`,
  despite their historical QA-only description. No compiler elimination or
  non-distribution is assumed.
- `assets/explorer.png` depicts the mixed UI and demo imagery. The generated
  include and screenshot need corresponding updates if excluded assets change;
  replacing a PNG alone leaves its old embedded pixels in the executable.

Document adequate permissions for external elements or replace them with cleared
artwork. Preserve third-party copyright and trademark notices. No live user-file
preview, transfer or installation capability needs removal to replace fixed
demo graphics. Current pixels and functionality remain unchanged.

Before distributing a complete source bundle or PS4 release, resolve libjbc and
external-asset rights, retain all applicable notices, and provide corresponding
source matching the exact binary revision. Include modified library code,
required runtime sources, editable inputs and build scripts, plus applicable
GPLv3 installation information when required. The older third-party archive
alone is not complete corresponding source for the application. Verify a fresh
production build, package extraction and supported console behavior if code or
assets change. Do not assume a system-library exception for libjbc, rewrite
historical releases, or treat successful compilation as distribution clearance.

## Packaging and appearance

The root GPL text, this scope document, inventory, historical MIT notice and
third-party notices must travel with source distributions of this revision.
The production builder retains the existing package file list and copies the
combined `distribution/LICENSES.txt` notice into its local output. The repository
contains the full scope documents and historical MIT notice for matching source
distributions. Removed upstream preview/delivery/collector helpers are not
restored by this licensing update. A future distributor must provide the full
applicable source and notices; technical packaging is not legal clearance.
The current combined notice retains the older third-party/legal texts before
which the scoped original-work notice is added. Old release artifacts are untouched.

No UI strings, renderer behavior, artwork pixels or PS4 access implementation
are changed by the migration. The visible `by nueex` credit and OpenOrbis About
module remain; the fuller author/copyright and licensing details are in README
and the distributed legal notices. A future About redesign must preserve
upstream credits and be reviewed separately.
