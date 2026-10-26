# Scoped licensing publication validation

2026-10-26 — Original Author: nueex — Copyright (C) 2026 nueex.
This original report is covered by [LICENSING.md](LICENSING.md).

## Integration base and scope

The publication candidate is based on GitHub main
`164e3fb007154b2ca817fd944f49205b255326ce`, retaining its reproducible-build
fixes, BUILD.md, source-availability documentation, and removal of obsolete
tools and an unused font. The earlier local checkout and its audit materials
were preserved separately; obsolete files were not restored for publication.

[LICENSE_SCOPE.json](LICENSE_SCOPE.json) explicitly classifies all 152 files
in this revision: 126 original, 11 mixed and 15 excluded. The original-work
GPL-3.0-or-later grant does not grant additional rights in excluded components.
No release tag or historical binary/license archive is changed.

## Source, notices and assets

- 84 surviving original source/build/config files receive license comments.
  Removing the added comment prefixes reproduces the latest-main file bodies
  exactly, allowing existing newline conventions. This includes all 74 original
  application C/header files. Python ASTs remain identical.
- All inherited libjbc code/headers and its provenance record are unchanged.
  Its notice explicitly limits nueex's grant to owned original additions/changes;
  upstream redistribution permission is still unresolved.
- `src/ps4/link.x` retains its exact bytes and GPL-3.0-only marker. Generated
  font/mixed artwork includes, PNG/SVG/TTF files and font notices are unchanged.
- The root LICENSE is the complete unmodified GPLv3 text. The historical MIT
  notice preserves every original permission/copyright line, including the
  existing contributors' notice and nueex's previously added author credit.
- The previous combined distribution/legal text is retained verbatim after
  the new current-scope notice; no third-party legal terms are replaced.
- Inventory coverage, ownership/exclusion boundaries, asset hashes, internal
  document links and Git whitespace checks are verified before committing.

## Fresh production compile

The latest-main baseline and licensing candidate were each built locally using
`tools/build_ps4.py --production`, with the existing external OpenOrbis v0.5.4
SDK and Clang/LLD 18.1.8, without `--package`.

All 35 production object files and the linked ELF are byte-identical before
and after the licensing comments. ELF SHA-256:

```text
12ccb5315cbe29e84e4e6454af9afe0f5d90c4b9787db8159bf5fff3fbd04b66
```

This establishes unchanged compiled output for the production build. All
artwork/font bytes and mixed rendering data also match latest main. No console
hardware test was performed and hardware compatibility claims are not expanded.

The earlier local audit compared 15 preview scenarios with identical PPM/PNG
output. Its remaining `media-pkg` checks were blocked by a missing old fixture,
not a licensing regression. That preview harness was subsequently removed by
the upstream cleanup and is not restored or claimed as a full passing suite
for this publication candidate. Earlier tests of removed archive helpers likewise
refer to that initial local checkout, not tools in this current source tree.

## Publication boundaries and remaining issues

Only original-source license comments, attribution, licensing documentation,
file/asset inventories and the combined legal notice are committed. The
production builder's behavior and package file list remain unchanged. Matching
source distributions must retain the scope documents, inventories, historical
MIT notice and applicable third-party materials; this technical requirement
does not supply missing permissions.

The commit/push is authorized by the maintainer. No new binaries, packages,
release assets or tags are published. Local compiled verification outputs and
research files are ignored and excluded from the commit.

[LICENSING_REVIEW.md](LICENSING_REVIEW.md) records the outstanding complete-
application issues: libjbc permissions, excluded external artwork/branding and
corresponding-source/runtime requirements. The scoped original-work GPL grant
is effective independently of those unresolved issues. This publication is
not a claim that libjbc or the complete application is cleared for redistribution.
