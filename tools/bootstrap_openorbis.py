# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Download the pinned official OpenOrbis SDK locally; no system installation."""
import hashlib
from pathlib import Path
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
URL='https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz'
EXPECTED='3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526'
archive=ROOT/'.tools/downloads/toolchain-llvm-18.tar.gz'
archive.parent.mkdir(parents=True,exist_ok=True)
if not archive.exists():
    print('Downloading official OpenOrbis v0.5.4 (about 159 MB)...',flush=True)
    with urllib.request.urlopen(URL,timeout=60) as response, archive.open('wb') as output:
        while chunk:=response.read(1024*1024): output.write(chunk)
with archive.open('rb') as source:
    digest=hashlib.file_digest(source,'sha256').hexdigest()
if digest!=EXPECTED: raise SystemExit('SDK archive hash mismatch; no extraction performed')
destination=ROOT/'.tools/sdk'
destination.mkdir(parents=True,exist_ok=True)
with tarfile.open(archive) as tar:
    for member in tar.getmembers():
        resolved=(destination/member.name).resolve()
        if not resolved.is_relative_to(destination.resolve()): raise SystemExit('Archive path outside SDK directory')
    tar.extractall(destination,filter='data')
print('Verified SDK:',destination/'OpenOrbis/PS4Toolchain')
