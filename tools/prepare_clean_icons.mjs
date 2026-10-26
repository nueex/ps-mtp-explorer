// PS MTP Explorer
// Original Author: nueex
// Copyright (C) 2026 nueex
// SPDX-License-Identifier: GPL-3.0-or-later
// GNU GPL version 3 or later; see LICENSE and LICENSING.md.
// WITHOUT ANY WARRANTY; see LICENSE.
// Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

// Rasterize editable local SVGs with the bundled sharp dependency.
// Call prepareCleanIcons({sharp: require('sharp')}) from node_repl.
import { readFile, writeFile, mkdir, access } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
const root = fileURLToPath(new URL('../', import.meta.url));
const sha = data => createHash('sha256').update(data).digest('hex');
export async function prepareCleanIcons({sharp}) {
  const icons = new URL('../assets/icons/', import.meta.url);
  const output = new URL('../assets/reference/', import.meta.url);
  await mkdir(icons, {recursive:true});
  const brand = new URL('ps4-wordmark.png', icons);
  try { await access(brand); } catch {
    await sharp(await readFile(new URL('platform.png',output))).extract({left:0,top:0,width:112,height:39}).png().toFile(fileURLToPath(brand));
  }
  const gamepad = (await readFile(new URL('gamepad.svg', icons),'utf8')).replace('<svg ', '<svg x="128" y="1" width="46" height="35" ');
  await writeFile(new URL('platform.svg',icons), `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 176 39"><image width="112" height="39" href="data:image/png;base64,${(await readFile(brand)).toString('base64')}"/>${gamepad}</svg>\n`);
  const sizes={platform:[176,39],folder:[192,156],phone:[34,46],usb:[34,44],drive:[40,40],settings:[40,40],home:[34,35],copy:[36,39],move:[36,38],rename:[36,38],delete:[36,39],info:[39,40],document:[81,110]};
  const provenance=JSON.parse(await readFile(new URL('provenance.json',output),'utf8'));
  for (const [name,[width,height]] of Object.entries(sizes)) {
    const svg=await readFile(new URL(`${name}.svg`,icons));
    // Supersample curves and downsample once. Native rendering also preserves
    // proportions and interpolates premultiplied alpha when a size changes.
    const large=await sharp(svg,{density:384}).resize(width*4,height*4,{fit:'contain',background:'#0000'}).png().toBuffer();
    const png=await sharp(large).resize(width,height,{kernel:'lanczos3'}).png().toBuffer();
    await writeFile(new URL(`${name}.png`,output),png);
    provenance.assets[name]={source:`assets/icons/${name}.svg`,size:[width,height],mode:'clean-vector',svg_sha256:sha(svg),sha256:sha(png)};
  }
  const names=['wordmark','platform','folder','phone','usb','drive','settings','home','copy','move','rename','delete','info','document','thumb_pkg','thumb_image','thumb_video'];
  let header='/* Embedded local artwork; editable vector icons in assets/icons/. See assets/reference/provenance.json. */\ntypedef struct {int w,h;const unsigned char *rgba;} psmtp_ui_asset;\n';
  for(const name of names) {
    const {data,info}=await sharp(await readFile(new URL(`${name}.png`,output))).ensureAlpha().raw().toBuffer({resolveWithObject:true});
    header+=`static const unsigned char ui_${name}_bytes[]={\n`;
    for(let i=0;i<data.length;i+=40) header+=Array.from(data.subarray(i,i+40)).join(',')+',\n';
    header+=`};\nstatic const psmtp_ui_asset ui_${name}={${info.width},${info.height},ui_${name}_bytes};\n`;
  }
  await writeFile(new URL('../src/ps4/reference_assets.inc',import.meta.url),header);
  await writeFile(new URL('provenance.json',output),JSON.stringify(provenance,null,2)+'\n');
  return {root,icons:Object.keys(sizes),supersampling:4};
}
