# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Bounded structural checks for generated ELF/OELF/SELF; never executes them."""
import argparse
import json
from pathlib import Path
import struct

def application_exit_imports(imports):
    """Reject the observed crashing runtime import; not a runtime/API proof."""
    names=set(imports)
    forbidden=names & {'_exit','_Exit','exit','quick_exit'}
    if forbidden:
        raise ValueError('Forbidden dynamic process-exit imports: '+str(sorted(forbidden)))
    if not {'sceSystemServiceLoadExec','sceSysmoduleLoadModuleInternal'} <= names:
        raise ValueError('Missing declared SystemService exit/load imports')
    return {'raw_exit_import_absent':True,'system_service_exit_import_present':True}

def exported_symbols(path):
    data=Path(path).read_bytes(); shoff=struct.unpack_from('<Q',data,40)[0]
    shsize,shcount=struct.unpack_from('<HH',data,58)
    if shsize!=64 or shcount>4096 or shoff+shsize*shcount>len(data): raise ValueError('Bad stub section table')
    sections=[struct.unpack_from('<II4QII2Q',data,shoff+i*64) for i in range(shcount)]
    names=set()
    for s in sections:
        if s[1]!=11: continue
        strings=sections[s[6]]; text=data[strings[4]:strings[4]+strings[5]]
        if s[5]%24 or s[4]+s[5]>len(data): raise ValueError('Bad stub symbol table')
        for off in range(s[4],s[4]+s[5],24):
            name,info,_,index,_,_=struct.unpack_from('<IBBHQQ',data,off)
            if name and index:
                end=text.find(b'\0',name)
                if end<0: raise ValueError('Bad stub symbol name')
                names.add(text[name:end].decode('ascii'))
    return names

def elf(path, sony=False, sdk=None):
    data=Path(path).read_bytes()
    if len(data)<64 or data[:6]!=b'\x7fELF\x02\x01': raise ValueError('Not ELF64 little-endian')
    kind,machine=struct.unpack_from('<HH',data,16)
    if machine!=62 or kind not in ([0xfe10,0xfe00,0xfe18] if sony else [3]): raise ValueError('Unexpected ELF type/machine')
    entry,phoff,shoff=struct.unpack_from('<QQQ',data,24)
    phsize,phcount,shsize,shcount=struct.unpack_from('<HHHH',data,54)
    if phsize!=56 or phcount>128 or phoff+phcount*56>len(data): raise ValueError('Bad program headers')
    loads=[]; executable_entry=False
    for i in range(phcount):
        t,flags,off,vaddr,_,filesz,memsz,align=struct.unpack_from('<II6Q',data,phoff+i*56)
        if filesz and off+filesz>len(data): raise ValueError('Segment exceeds file')
        if t==1:
            if filesz>memsz: raise ValueError('Bad load size')
            if flags&1 and vaddr<=entry<vaddr+memsz: executable_entry=True
            loads.append(dict(offset=off,vaddr=vaddr,filesz=filesz,memsz=memsz,flags=flags,align=align))
    if not loads or not executable_entry: raise ValueError('No executable entry in load segments')
    needed=[]; imports=[]
    if not sony:
        if shsize!=64 or shcount>4096 or shoff+shcount*64>len(data): raise ValueError('Bad section headers')
        sections=[struct.unpack_from('<II4QII2Q',data,shoff+i*64) for i in range(shcount)]
        def section(index):
            s=sections[index]; start,size=s[4:6]
            if start+size>len(data): raise ValueError('Section exceeds file')
            return data[start:start+size]
        def cstr(buf,at):
            if at>=len(buf): raise ValueError('String offset out of bounds')
            end=buf.find(b'\0',at)
            if end<0: raise ValueError('Unterminated string')
            return buf[at:end].decode('ascii')
        for i,s in enumerate(sections):
            if s[1] not in (6,11): continue
            buf=section(i); strings=section(s[6]); size=16 if s[1]==6 else 24
            if len(buf)%size: raise ValueError('Bad dynamic/symbol table size')
            for at in range(0,len(buf),size):
                if s[1]==6:
                    tag,value=struct.unpack_from('<qQ',buf,at)
                    if tag==1: needed.append(cstr(strings,value))
                else:
                    name,info,_,index,value,sz=struct.unpack_from('<IBBHQQ',buf,at)
                    if index==0 and name: imports.append(cstr(strings,name))
        allowed={'libkernel.so','libSceVideoOut.so','libScePad.so','libSceUserService.so','libSceSysmodule.so'}
        if set(needed) not in (allowed,allowed|{'libSceSystemService.so'},allowed|{'libSceSystemService.so','libSceAudioOut.so'},allowed|{'libSceSystemService.so','libSceAudioOut.so','libSceNet.so'}):
            raise ValueError('Unexpected or missing dynamic dependencies: '+str(needed))
        if sdk is not None:
            exports=set().union(*(exported_symbols(Path(sdk)/'lib'/name) for name in needed))
            missing=set(imports)-exports
            if missing: raise ValueError('Imports absent from linked SDK stubs: '+str(sorted(missing)))
    return dict(path=str(path),elf_type=hex(kind),entry=hex(entry),load_segments=loads,needed=needed,imports=imports)

def fake_self(path):
    data=Path(path).read_bytes()
    if len(data)<32 or data[:4]!=bytes.fromhex('4f153d1d'): raise ValueError('Bad SELF magic')
    size=struct.unpack_from('<Q',data,16)[0]; count=struct.unpack_from('<H',data,24)[0]
    if size!=len(data) or count>128 or 32+count*32+64>len(data): raise ValueError('Bad SELF table/size')
    for i in range(count):
        _,off,filesz,memsz=struct.unpack_from('<4Q',data,32+i*32)
        if off+filesz>len(data): raise ValueError('SELF segment exceeds file')
    if data[32+count*32:36+count*32]!=b'\x7fELF': raise ValueError('Missing embedded ELF header')
    return dict(path=str(path),bytes=size,entries=count,structural_check=True)

def headers_and_sections(path):
    data=Path(path).read_bytes()
    phoff,shoff=struct.unpack_from('<QQ',data,32)
    phsize,phcount,shsize,shcount,names_index=struct.unpack_from('<5H',data,54)
    if phsize!=56 or shsize!=64 or phcount>128 or shcount>4096:
        raise ValueError('Unexpected ELF table sizes')
    if phoff+phcount*56>len(data) or shoff+shcount*64>len(data): raise ValueError('ELF table out of bounds')
    programs=[struct.unpack_from('<II6Q',data,phoff+i*56) for i in range(phcount)]
    sections=[struct.unpack_from('<II4QII2Q',data,shoff+i*64) for i in range(shcount)]
    string_section=sections[names_index]
    names=data[string_section[4]:string_section[4]+string_section[5]]
    named=[]
    for s in sections:
        end=names.find(b'\0',s[0])
        if end<0: raise ValueError('Unterminated section name')
        named.append((names[s[0]:end].decode('ascii'),s))
    return data,programs,named

def converted_image(oelf_path, self_path=None):
    """PS4 memory coverage and SELF payload checks missing from the first build."""
    data,programs,sections=headers_and_sections(oelf_path)
    mapped=[p for p in programs if p[0] in (1,0x61000010)]
    previous_end=0
    for p in sorted(mapped,key=lambda p:p[3]):
        _,flags,off,addr,_,filesz,memsz,align=p
        if align!=0x4000 or off%0x4000 or addr%0x4000:
            raise ValueError('PS4 mapped segment is not 16 KiB aligned')
        if filesz>memsz or addr<previous_end: raise ValueError('Overlapping or invalid mapped segments')
        previous_end=addr+((memsz+0x3fff)&~0x3fff)
    checked=[]
    for name,s in sections:
        # The converter replaces dynamic tables; verify actual code/data sections.
        if not s[5] or s[2]&0x400 or not name.startswith(('.text','.plt','.rodata','.eh_frame','.data','.got','.bss')):
            continue
        candidates=[p for p in mapped if p[3]<=s[3] and s[3]+s[5]<=p[3]+p[6]]
        if not candidates: raise ValueError('Section outside converted memory image: '+name)
        p=candidates[0]
        if s[1]!=8 and (s[4]-p[2]!=s[3]-p[3] or s[4]+s[5]>p[2]+p[5]):
            raise ValueError('Section file/address mapping mismatch: '+name)
        checked.append(name)
    verified=[]
    if self_path is not None:
        raw=Path(self_path).read_bytes(); count=struct.unpack_from('<H',raw,24)[0]
        embedded=32+count*32
        phoff=struct.unpack_from('<Q',data,32)[0]; phcount=struct.unpack_from('<H',data,56)[0]
        if raw[embedded:embedded+64]!=data[:64] or raw[embedded+64:embedded+64+phcount*56]!=data[phoff:phoff+phcount*56]:
            raise ValueError('SELF embedded ELF/program headers differ from OELF')
        for i in range(count):
            props,off,size,_=struct.unpack_from('<4Q',raw,32+i*32)
            if not props&(1<<11): continue
            index=(props>>20)&0xffff
            if index>=len(programs): raise ValueError('SELF data entry references invalid segment')
            p=programs[index]
            if size!=p[5] or raw[off:off+size]!=data[p[2]:p[2]+p[5]]:
                raise ValueError('SELF segment payload mismatch')
            verified.append(index)
        expected=[i for i,p in enumerate(programs) if p[0] in (1,0x61000010,0x61000000)]
        if sorted(verified)!=expected: raise ValueError('Missing/duplicate SELF data segments')
    return {'mapped_sections_checked':checked,'self_segments_verified':verified}

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('directory',nargs='?',default='build-ps4')
    parser.add_argument('--sdk',default='.tools/sdk/OpenOrbis/PS4Toolchain')
    args=parser.parse_args(); base=Path(args.directory)
    result={'elf':elf(base/'psmtp.elf',sdk=args.sdk),'oelf':elf(base/'psmtp.oelf',True),'self':fake_self(base/'package/eboot.bin'),
            'conversion':converted_image(base/'psmtp.oelf',base/'package/eboot.bin'),
            'hardware_tested':False}
    (base/'structure-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: ELF/OELF entry points, SDK imports, mapped section/BSS coverage, 16 KiB alignment, SELF headers and payloads.')
