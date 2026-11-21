# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Embed the pinned OFL fonts for the native renderer."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
def write_font(filename, digest, notice, symbol, output):
    data=(ROOT/'assets/fonts'/filename).read_bytes()
    if hashlib.sha256(data).hexdigest()!=digest:
        raise ValueError(f'Bundled font changed: {filename}')
    out=f'/* Generated from the bundled OFL font. See assets/fonts/{notice}. */\nstatic const unsigned char {symbol}[]={{\n'
    out+=''.join(','.join(str(v) for v in data[i:i+32])+',\n' for i in range(0,len(data),32))+'};\n'
    target=ROOT/'src/ps4'/output
    if not target.exists() or target.read_text()!=out:
        target.write_text(out,encoding='ascii')
    return target

def embed():
    classic=write_font('NotoSans-Light.ttf','74ffdd438f2ae232371ceb00444dae93e2590d263a4ff4c5e2c8385aa8012fdb',
                       'OFL.txt','psmtp_font_data','font_data.inc')
    write_font('Urbanist-Regular.ttf','bef5cf82ac62ebe7764fe854d66e8d5282688f10359e51a0bc28a97ac3a26514',
               'Urbanist-OFL.txt','psmtp_theme_font_data','theme_font_data.inc')
    return classic
if __name__=='__main__':print(embed())
