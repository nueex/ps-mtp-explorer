# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Embed the pinned OFL Noto Sans font; no runtime font-file dependency."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
FONT=ROOT/'assets/fonts/NotoSans-Light.ttf'
EXPECTED='74ffdd438f2ae232371ceb00444dae93e2590d263a4ff4c5e2c8385aa8012fdb'
def embed():
 data=FONT.read_bytes()
 if hashlib.sha256(data).hexdigest()!=EXPECTED: raise ValueError('Bundled font changed; review before embedding')
 out='/* Generated from the bundled OFL font. See assets/fonts/OFL.txt. */\nstatic const unsigned char psmtp_font_data[]={\n'
 out+=''.join(','.join(str(v) for v in data[i:i+32])+',\n' for i in range(0,len(data),32))+'};\n'
 target=ROOT/'src/ps4/font_data.inc'
 if not target.exists() or target.read_text()!=out: target.write_text(out,encoding='ascii')
 return target
if __name__=='__main__':print(embed())
