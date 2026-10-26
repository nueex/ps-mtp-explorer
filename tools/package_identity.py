# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Validate install identifiers before packaging and after independent extraction."""
import re

def validate_identity(title_id, content_id):
    if not re.fullmatch(r'[A-Z]{4}[0-9]{5}', title_id):
        raise ValueError('TITLE_ID must contain four uppercase letters followed by five digits')
    if not re.fullmatch(r'[A-Z]{2}[0-9]{4}-[A-Z]{4}[0-9]{5}_00-[A-Z0-9]{16}', content_id):
        raise ValueError('Invalid application CONTENT_ID format')
    if content_id[7:16] != title_id:
        raise ValueError('CONTENT_ID and TITLE_ID identify different applications')
    return {'title_id_format_valid': True, 'content_id_format_valid': True, 'title_content_id_match': True}

def validate_package_identity(pkg, sfo):
    data = pkg.read_bytes()
    if len(data) < 0x70 or data[:4] != b'\x7fCNT':
        raise ValueError('Invalid PKG header')
    header_id = data[0x40:0x70].split(b'\0', 1)[0].decode('ascii')
    result = validate_identity(sfo['TITLE_ID'], sfo['CONTENT_ID'])
    if header_id != sfo['CONTENT_ID']:
        raise ValueError('PKG header CONTENT_ID differs from packaged PARAM.SFO')
    result['pkg_header_sfo_identity_match'] = True
    return result
