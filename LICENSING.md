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

See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md),
[LICENSING_REVIEW.md](LICENSING_REVIEW.md) and the per-asset
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
