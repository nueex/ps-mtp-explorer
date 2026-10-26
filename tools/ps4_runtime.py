# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Required OpenOrbis package runtime inventory, independent of ELF imports."""
import hashlib
from pathlib import Path
import shutil
import struct
from inspect_ps4 import fake_self

RUNTIME_FILES = ('sce_module/libSceFios2.prx', 'sce_module/libc.prx')
PACKAGE_FILES = ('eboot.bin', 'LICENSES.txt', 'sce_sys/param.sfo', 'sce_sys/icon0.png',
                 'sce_sys/about/right.sprx') + RUNTIME_FILES


def runtime_inventory(directory, expected=None):
    """Require nonempty runtime files; compare exact hashes after PKG extraction."""
    result = {}
    for name in RUNTIME_FILES:
        path = Path(directory)/name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError('Missing or empty required package runtime: ' + name)
        data = path.read_bytes()
        result[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    if expected is not None and result != expected:
        raise ValueError('Required runtime payload differs from staged SDK modules')
    return result


def stage_runtime(sdk, stage):
    """Copy the SDK's open-source dummy PRXs, never a console/system module."""
    source_root = Path(sdk)/'samples/hello_world'
    for name in RUNTIME_FILES:
        source = source_root/name
        if not source.is_file():
            raise ValueError('Incomplete SDK: missing sample runtime ' + str(source))
        fake_self(source)
        data = source.read_bytes()
        count = struct.unpack_from('<H', data, 24)[0]
        if struct.unpack_from('<H', data, 32 + count*32 + 16)[0] != 0xfe18:
            raise ValueError('Expected SDK dynamic-library fake SELF: ' + name)
        # The release contains a second copy alongside its corresponding sources.
        source_copy = Path(sdk)/'src/modules'/Path(name).name
        if source_copy.is_file() and source_copy.read_bytes() != data:
            raise ValueError('SDK sample/source runtime copies disagree: ' + name)
        destination = Path(stage)/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    return runtime_inventory(stage)
