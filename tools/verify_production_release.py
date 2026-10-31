# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Audit the extracted production payload without executing console code."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from build_ps4 import read_sfo, package_version, RELEASE, TITLE_ID, CONTENT_ID
from inspect_ps4 import headers_and_sections, application_exit_imports
from ps4_runtime import PACKAGE_FILES
from package_identity import validate_package_identity, validate_dialog_controls

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
base = args.directory.resolve()
report = json.loads((base/'build-report.json').read_text())
assert report['production'] and not report['diagnostic_logging'] and not report['experimental']
assert not report['persistent_shutdown_journal'] and not report['credential_snapshot_experiment']
assert report['shutdown_source_baseline'] == '20261027-startup-repair-r2'
required_sources = {'src/ps4/lifecycle.c','src/ps4/exit.c','src/ps4/preview.c',
                    'src/ps4/device_audio.c','src/ps4/usb_ps4.c','src/ps4/package_install.c',
                    'src/ps4/process_access.c','third_party/libjbc/jailbreak.c'}
assert required_sources <= set(report['compiled_sources'])
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for name, digest in report['sources_sha256'].items():
    assert sha(ROOT/name) == digest, name
assert not any(name.startswith('tests/') or name in ('src/browser.c','src/diagnostic.c') for name in report['compiled_sources'])
payload = base/'verified-extract/uroot'
names = {p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file()}
assert names == set(PACKAGE_FILES) | {'sce_sys/keystone'}, names
sfo = read_sfo(payload/'sce_sys/param.sfo')
dialog_controls = validate_dialog_controls(sfo)
assert sfo['TITLE_ID'] == TITLE_ID
assert sfo['CONTENT_ID'] == CONTENT_ID and sfo['TITLE'] == 'PS MTP Explorer'
assert sfo['APP_VER'] == sfo['VERSION'] == package_version()
assert report['release'] == RELEASE and sfo['APP_VER'] == package_version()
_, _, sections = headers_and_sections(base/'psmtp.elf')
assert not any(name.startswith(('.debug', '.zdebug')) for name,_ in sections)
patterns = [rb'stage=', rb'(?i)[a-z]:[\\/]Users[\\/]', rb'(?i)/(?:Users|home)/',
            rb'psmtp-(?:scan|diag|shutdown)\.log', rb'run-start-build-',
            rb'EXPERIMENTAL USB ABI', rb'PSMTP_(?:TEST_|PS4_EXIT_TEST_ONLY|PS4_DISCOVERY_ONLY)',
            rb'AddressSanitizer', rb'UndefinedBehaviorSanitizer', rb'HOST PREVIEW', rb'QA phone', rb'MOCK phone']
scanned = {}
for name in PACKAGE_FILES:
    path = payload/name
    data = path.read_bytes()
    for pattern in patterns:
        assert not re.search(pattern, data), (name, pattern)
    scanned[name] = sha(path)
pkg = base/(CONTENT_ID+'.pkg')
identity = validate_package_identity(pkg,sfo)
assert sha(pkg) == report['package']['sha256'] and report['package']['round_trip_verified']
imports = json.loads((base/'structure-report.json').read_text())['elf']['imports']
exit_check = application_exit_imports(imports)
assert not any('sceNet' in name for name in imports)
assert not {'fopencookie', 'fprintf', 'fputs', 'fputc'} & set(imports)
program = (payload/'eboot.bin').read_bytes()
assert RELEASE.encode()+b'\0' in program
native_symbols = ['sigaction','sigprocmask','scePthreadTimedjoin','sceUsbdExit',
                  'sceUsbdReleaseInterface','sceUsbdClose','sceBgftServiceIntTerm',
                  'sceAppInstUtilTerminate']
for symbol in native_symbols:
    assert symbol.encode()+b'\0' in program, symbol
credential_object = (base/'third_party_libjbc_jailbreak.c.o').read_bytes()
assert b'psmtp_jbc_setuid\0' not in credential_object and b'k_prison_ref\0' not in credential_object
result = {'release': RELEASE, 'title_id': TITLE_ID, 'content_id': CONTENT_ID,
          'app_version': sfo['APP_VER'], 'metadata_matches_display_version': True,
          'production': True, 'diagnostic_logging': False, 'debug_sections_absent': True,
          'private_paths_absent': True, 'test_payloads_absent': True, 'network_imports_absent': True,
          'payload_sha256': scanned, 'package_sha256': sha(pkg),
          'package_round_trip_verified': True, 'identity_validation': identity,
          'dialog_controls_validation': dialog_controls,
          'shutdown_components_present': True, 'shutdown_native_symbols': native_symbols,
          'exit_import_check': exit_check, 'credential_copy_experiment_absent': True,
          'persistent_diagnostic_paths_absent': True, 'hardware_tested': False}
(base/'production-audit.json').write_text(json.dumps(result, indent=2)+'\n')
print('PASS: production identity/version, payload allowlist, hashes, no debug sections/logs/personal paths/test payloads/network imports')
