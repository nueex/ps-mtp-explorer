# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

"""Build PS MTP Explorer with an installed OpenOrbis SDK.

No Sony SDK, kernel patch, device connection or deployment is performed.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from embed_font import embed
from inspect_ps4 import converted_image, elf as inspect_elf, application_exit_imports
from ps4_runtime import stage_runtime, runtime_inventory, PACKAGE_FILES
from package_identity import validate_identity, validate_package_identity

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['src/pkg.c','src/transfer_meter.c','src/ps4/package_install.c','src/ps4/storage_space.c','src/keyboard.c','src/metadata.c','src/ps4/preview.c','src/name.c','src/device_sound.c','src/usb_label.c','src/ps4/device_audio.c','src/device_watch.c','src/locations.c','src/ui_format.c','src/copy_plan.c','src/ps4/ui_font.c','src/ps4/ui_draw.c','src/ps4/file_store.c','src/ps4/file_ops.c','src/copy.c', 'src/sha256.c', 'src/mtp.c', 'src/usb_select.c', 'src/navigator.c', 'src/browser.c', 'src/diagnostic.c',
           'src/diag_log.c', 'src/usb_module.c', 'src/ps4/copy_file.c', 'src/ps4/usb_ps4.c', 'src/ps4/console_render.c', 'src/ps4/screen.c', 'src/ps4/exit.c', 'src/ps4/main.c','src/ps4/process_access.c','third_party/libjbc/jailbreak.c','third_party/libjbc/kernelrw.c']
CONTENT_ID = 'IV0000-NUEX00001_00-NUEEXMTP00000000'
TITLE_ID = 'NUEX00001'
RELEASE = '1.01'

def package_version():
    version = re.search(r'#define PSMTP_VERSION "([^"]+)"', (ROOT/'include/psmtp/version.h').read_text()).group(1)
    if version != RELEASE or not re.fullmatch(r'\d{1,2}\.\d{2}', version):
        raise ValueError('Release and application version must match the PS4 major.minor format')
    major, minor = version.split('.')
    return f'{int(major):02d}.{minor}'

def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source,'sha256').hexdigest()

def read_sfo(path):
    data=Path(path).read_bytes()
    if data[:4]!=b'\x00PSF': raise ValueError('Invalid SFO magic')
    _,_,keys,values,count=struct.unpack_from('<5I',data)
    if count>1024 or 20+count*16>len(data): raise ValueError('Invalid SFO table')
    result={}
    for i in range(count):
        key,kind,size,cap,offset=struct.unpack_from('<HHIII',data,20+i*16)
        end=data.find(b'\0',keys+key)
        if end<0 or size>cap or values+offset+size>len(data): raise ValueError('Invalid SFO entry')
        name=data[keys+key:end].decode('ascii'); value=data[values+offset:values+offset+size]
        result[name]=struct.unpack('<I',value)[0] if kind==0x0404 else value.rstrip(b'\0').decode('utf-8')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', default=os.environ.get('OO_PS4_TOOLCHAIN', str(ROOT / '.tools/sdk/OpenOrbis/PS4Toolchain')))
    parser.add_argument('--cc', help='Clang command or "zig cc"; defaults to local LLVM 18, then Zig, then clang')
    parser.add_argument('--ld', help='standalone ld.lld; otherwise link through compiler driver')
    parser.add_argument('--experimental-usb', action='store_true',
                        help='acknowledge the documented unverified USB ABI')
    parser.add_argument('--production', action='store_true', help='build without diagnostic logging or development metadata')
    parser.add_argument('--package', action='store_true', help='also build and validate experimental PKG')
    parser.add_argument('--output', default=str(ROOT/'build-ps4'))
    args = parser.parse_args()
    validate_identity(TITLE_ID, CONTENT_ID)
    if not args.production and not args.experimental_usb:
        parser.error('select --production or --experimental-usb')
    app_version = package_version()
    sdk = Path(args.sdk).resolve()
    for p in ['include/stdio.h', 'include/orbis/Usbd.h', 'lib/crt1.o', 'lib/libc.a', 'lib/libkernel.so', 'link.x']:
        if not (sdk / p).is_file():
            raise SystemExit('Incomplete OpenOrbis SDK: missing ' + str(sdk / p))
    zig = ROOT / '.tools/ziglang' / ('zig.exe' if os.name == 'nt' else 'zig')
    llvm18 = ROOT/'.tools/llvm18/bin/clang.exe'
    if args.cc:
        cc = [str(Path(args.cc).resolve())] if Path(args.cc).is_file() else shlex.split(args.cc)
    else:
        cc = [str(llvm18)] if llvm18.exists() else ([str(zig), 'cc'] if zig.exists() else ['clang'])
    is_zig = Path(cc[0]).stem.lower() == 'zig'
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    embed()
    source_inputs=(SOURCES+['tools/build_ps4.py','tools/ps4_runtime.py','tools/package_identity.py','distribution/LICENSES.txt','src/ps4/link.x','src/ps4/font_data.inc','src/ps4/reference_assets.inc','assets/reference/icon0.png','assets/reference/provenance.json','assets/fonts/NotoSans-Light.ttf','assets/fonts/OFL.txt']+[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'assets/icons').iterdir()) if p.is_file()]+[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'include').rglob('*.h'))]+
        [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'src/ps4').glob('*.h'))]+[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'third_party/libjbc').iterdir()) if p.is_file() and p.suffix!='.c'])
    report = {'release': RELEASE, 'app_version': app_version, 'production': args.production, 'diagnostic_logging': not args.production, 'experimental': not args.production, 'hardware_tested': False, 'usb_tests_enabled': True,
              'phone_upload': True, 'upload_max_file_bytes': (2**64-1)//8, 'large_phone_upload': True, 'large_upload_requires_partial64_and_size64': True, 'large_upload_sentinel_header': True, 'large_transfer_hardware_tested': False, 'pretransfer_space_check': True, 'verification_status_only': True, 'layered_system_chimes': True, 'diagnostic_render_reentry_guard': True, 'render_timing_committed_before_logging': True, 'fullscreen_settings': True, 'sort_picker': True, 'qwerty_keyboard': True, 'controller_glyphs': True, 'audio_system_main_stereo': True, 'copy_reserve_bytes': 0, 'copy_destination_prefill': False, 'copy_source_content_passes': 1, 'copy_destination_hash_passes': 0,
              'live_transfer_speed':True,'recent_window_eta':True,'direct_pkg_install':True,'phone_pkg_staging':True,'installer_loopback_only':False,'installer_network_required':False,'installer_transport':'native-storage','saved_pkg_retained':False,'phone_install_copy_cleanup_after_success':True,'phone_install_copy_cleanup_after_failure':True,'persistent_paste_slot':True,'non_pkg_properties_only':True,'mtp_session_reopen_recovery':True,'quick_copy_verification':True,'live_directory_refresh':True,'new_folder':True,'settings_disabled':True,'installer_source_preserved':True,
              'rename': True, 'background_metadata_previews': True, 'controller_toolbar': True, 'cached_cpu_rendering': True,
              'device_connection_chimes': True, 'discovery_phone_names': True, 'automatic_device_watch': True, 'device_poll_ms': 100, 'retained_phone_session': True, 'loading_delay_ms': 180,
              'process_filesystem_access': True, 'access_profile': 'HEN syscall 11 / runtime process checks / original Unix identity with libjbc Sony capacity credentials', 'capacity_unknown_policy': 'checked-io', 'cross_firmware_hardware_verified': False, 'libjbc_commit': '835fe016ff0ae5dd89b9249f39cc0fe093fd07dd', 'mtp_tests_enabled': True, 'interactive_browser_enabled': True, 'verified_copy_enabled': True, 'copy_private_directory': True, 'copy_descriptor_checks': True, 'copy_absolute_open': True, 'clipboard_operations': True, 'phone_to_native_paste': True, 'native_copy_move_delete': True, 'phone_delete': True, 'direct_usb_probe': True, 'device_drawer': True, 'mounted_usb_browsing': True, 'internal_browsing': True, 'modern_explorer_ui': True, 'outline_font': 'Noto Sans Light OFL', 'recursive_folder_copy': True, 'folder_shared_reserve': False, 'copy_staging_directory': '/data/psmtp/copies', 'copy_readable_output': True, 'copy_native_stat_abi': True, 'copy_allocated_capacity': False, 'usbd_review_sha256':'9cc9d4b4021b66dab5c09ada373cdd042d68e23b5de1ca515d50b82abec28bff', 'sdk': str(sdk),
              'sdk_files_sha256': {p: sha(sdk/p) for p in ['include/orbis/Usbd.h', 'include/orbis/_types/libusb.h',
                  'include/orbis/libkernel.h', 'include/orbis/AudioOut.h', 'include/orbis/_types/audio_out.h', 'include/orbis/VideoOut.h', 'include/orbis/Pad.h',
                  'include/orbis/UserService.h', 'include/orbis/Sysmodule.h', 'include/orbis/SystemService.h', 'include/orbis/_types/sysmodule.h',
                  'include/orbis/_types/kernel.h', 'include/sys/stat.h', 'include/bits/stat.h', 'include/sys/statfs.h', 'include/bits/statfs.h', 'include/fcntl.h', 'include/bits/fcntl.h', 'include/unistd.h', 'include/bits/alltypes.h', 'include/stb/stb_image.h', 'include/stb/stb_truetype.h', 'lib/crt1.o', 'lib/libc.a',
                  'lib/libkernel.so', 'lib/libSceAudioOut.so', 'lib/libSceVideoOut.so', 'lib/libScePad.so', 'lib/libSceUserService.so', 'lib/libSceSysmodule.so', 'lib/libSceSystemService.so', 'link.x']},
              'sources_sha256': {p: sha(ROOT/p) for p in source_inputs}, 'steps': []}
    env = os.environ.copy()
    env['OO_PS4_TOOLCHAIN'] = str(sdk)
    # PkgTool targets netcoreapp3.0. Use installed .NET 9 locally without modifying it.
    env.setdefault('DOTNET_ROLL_FORWARD', 'Major')
    def run(label, command, cwd=ROOT):
        print(label, flush=True)
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end='', flush=True)
        report['steps'].append({'stage': label, 'command': [str(x) for x in command],
                                'exit_code': result.returncode, 'output': result.stdout})
        (output/'build-report.json').write_text(json.dumps(report, indent=2)+'\n')
        if result.returncode:
            raise SystemExit(f'{label} failed ({result.returncode}); see build-ps4/build-report.json')
        return result.stdout
    target = 'x86_64-freestanding' if is_zig else 'x86_64-pc-freebsd12-elf'
    report['compiler_version'] = run('compiler version',cc+['--version']).strip()
    report['compiler_sha256'] = sha(cc[0]) if Path(cc[0]).is_file() else None
    flags = ['-target', target, '-std=c99', '-D_POSIX_C_SOURCE=200809L', '-DPSMTP_EXPERIMENTAL_PS4_USB=1',
             '-nostdinc', '-I'+str(ROOT/'include'), '-isystem', str(sdk/'include'),
             '-isystem', str(sdk/'include/orbis/_types'), '-isystem', str(sdk/'include/stb'), '-fPIC', '-funwind-tables',
             '-fshort-wchar', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-O2']
    if args.production:
        flags += ['-DPSMTP_PRODUCTION=1','-DNDEBUG','-g0','-fno-ident',
                  '-ffile-prefix-map='+str(ROOT)+'=.', '-fdebug-prefix-map='+str(ROOT)+'=.',
                  '-include',str(ROOT/'src/ps4/release_config.h')]
    objects = []
    active_sources = [s for s in SOURCES if not args.production or s not in ('src/browser.c','src/diagnostic.c')]
    report['compiled_sources'] = active_sources
    for source in active_sources:
        obj = output / (source.replace('/', '_') + '.o')
        compile_flags=flags
        if source.startswith('third_party/libjbc/'):
            compile_flags=flags+['-std=gnu99','-ffreestanding','-mno-red-zone','-mno-sse','-mno-sse2','-mno-mmx','-fno-asynchronous-unwind-tables','-fno-unwind-tables','-Wno-unused-parameter']
        run('compile '+source, cc+compile_flags+['-c', ROOT/source, '-o', obj])
        objects.append(obj)
    elf = output/'psmtp.elf'
    libs = [str(sdk/'lib/libc.a')] + ['-l'+x for x in ['kernel', 'SceAudioOut', 'SceVideoOut', 'ScePad', 'SceUserService', 'SceSysmodule', 'SceSystemService']]
    standalone = args.ld or (str(llvm18.parent/'ld.lld.exe') if llvm18.exists() and not args.cc else shutil.which('ld.lld'))
    if standalone or is_zig:
        linker = [standalone] if standalone else [cc[0], 'ld.lld']
        report['linker_version'] = run('linker version',linker+['--version']).strip()
        report['linker_sha256'] = sha(linker[0]) if Path(linker[0]).is_file() else None
        link = linker+['-m', 'elf_x86_64', '-pie', '--script', ROOT/'src/ps4/link.x', '--eh-frame-hdr',
                '-z','max-page-size=0x4000',
                '-L'+str(sdk/'lib'), sdk/'lib/crt1.o']+objects+libs+['-o', elf]
    else:
        link = cc+['-target', target, '-nostdlib', '-pie', '-Wl,--script='+str(ROOT/'src/ps4/link.x'),
                   '-Wl,--eh-frame-hdr', '-Wl,-z,max-page-size=0x4000', '-L'+str(sdk/'lib'),
                   sdk/'lib/crt1.o']+objects+libs+['-o', elf]
    if args.production:
        link += ['--strip-debug'] if standalone or is_zig else ['-Wl,--strip-debug']
    run('link PS4 ELF', link)
    report['exit_import_check'] = application_exit_imports(inspect_elf(elf,sdk=sdk)['imports'])
    data = elf.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0] != 62:
        raise SystemExit('Output is not little-endian x86-64 ELF')
    if struct.unpack_from('<H',data,16)[0] != 3 or not struct.unpack_from('<Q',data,24)[0]:
        raise SystemExit('Expected PIE/ET_DYN with nonzero entry point')
    report['elf'] = {'sha256': sha(elf), 'bytes': len(data), 'type': 'ELF64 x86-64 ET_DYN'}
    (output/'build-report.json').write_text(json.dumps(report, indent=2)+'\n')
    if args.package:
        package(sdk, output, run, report)
    print('Built application:', elf)

def package(sdk, output, run, report):
    platform = 'windows' if os.name == 'nt' else ('macos' if sys.platform=='darwin' else 'linux')
    ext = '.exe' if os.name=='nt' else ''
    tools = sdk/'bin'/platform
    fself = tools/('create-fself'+ext)
    if platform=='macos' and not fself.is_file(): fself=tools/'create-fself-macos'
    pkgtool = tools/('PkgTool.Core'+ext)
    gp4tool = tools/('create-gp4'+ext)
    for tool in [fself,pkgtool,gp4tool]:
        if not tool.is_file(): raise SystemExit('Missing packaging tool: '+str(tool))
    stage = output/'package'; (stage/'sce_sys/about').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT/'distribution/LICENSES.txt',stage/'LICENSES.txt')
    run('convert ELF to OELF and fake SELF', [fself,'-in','psmtp.elf','-out','psmtp.oelf',
        '-eboot','package/eboot.bin','-paid','0x3800000000000011'],cwd=output)
    data = (stage/'eboot.bin').read_bytes()
    if data[:4] != bytes.fromhex('4f153d1d') or b'\x7fELF' not in data[:1024]:
        raise SystemExit('Unexpected fake SELF structure')
    report['conversion']=converted_image(output/'psmtp.oelf',stage/'eboot.bin')
    sfo = stage/'sce_sys/param.sfo'
    if sfo.exists(): sfo.unlink()  # exact generated build file, never user content
    run('create SFO',[pkgtool,'sfo_new',sfo])
    fields = {
        'APP_TYPE': ('Integer',4,'1'), 'APP_VER': ('Utf8',8,package_version()),
        'ATTRIBUTE': ('Integer',4,'0'), 'CATEGORY': ('Utf8',4,'gd'),
        'CONTENT_ID': ('Utf8',48,CONTENT_ID), 'DOWNLOAD_DATA_SIZE': ('Integer',4,'0'),
        'SYSTEM_VER': ('Integer',4,'0'), 'TITLE': ('Utf8',128,'PS MTP Explorer'),
        'TITLE_ID': ('Utf8',12,TITLE_ID), 'VERSION': ('Utf8',8,package_version())}
    for key,(kind,cap,value) in fields.items():
        run('SFO '+key,[pkgtool,'sfo_setentry',sfo,key,'--type',kind,'--maxsize',str(cap),'--value',value])
    shutil.copyfile(ROOT/'assets/reference/icon0.png',stage/'sce_sys/icon0.png')
    right = sdk/'samples/hello_world/sce_sys/about/right.sprx'
    if not right.is_file(): raise SystemExit('Missing SDK sample about module: '+str(right))
    shutil.copyfile(right,stage/'sce_sys/about/right.sprx')
    # FIOS2/libc are required by process startup even when absent from DT_NEEDED.
    # Use OpenOrbis's documented dummy modules; never copy Sony firmware PRXs.
    report['runtime_modules'] = stage_runtime(sdk, stage)
    run('write GP4',[gp4tool,'-out','diagnostic.gp4','-content-id',CONTENT_ID,'-files',
                    ' '.join(PACKAGE_FILES)],stage)
    pkg = output/(CONTENT_ID+'.pkg')
    if pkg.exists(): pkg.unlink()  # exact generated build artifact
    run('build experimental PKG',[pkgtool,'pkg_build',stage/'diagnostic.gp4',output])
    if not pkg.is_file() or pkg.read_bytes()[:4]!=b'\x7fCNT':
        raise SystemExit('PKG output missing or bad magic')
    validation = run('validate PKG hashes and signatures',[pkgtool,'pkg_validate',pkg])
    if '[ERROR]' in validation or '[WARN]' in validation or '[OK]' not in validation:
        raise SystemExit('PKG validator did not report a clean validation; artifact retained as unverified.')
    run('inspect SFO',[pkgtool,'sfo_listentries',sfo])
    # Extract independently through the package reader, then compare actual payloads.
    extracted = output/'verified-extract'
    run('extract experimental PKG for round-trip check',[pkgtool,'pkg_extract',pkg,extracted])
    # pkg_extract produces an inner-PFS "uroot" directory. SFO/icon live in
    # the PKG metadata table, not that filesystem. Extract those entries separately.
    pkg_bytes = pkg.read_bytes()
    count = struct.unpack_from('>I',pkg_bytes,0x10)[0]
    table = struct.unpack_from('>I',pkg_bytes,0x18)[0]
    if count>1024 or table+count*32>len(pkg_bytes): raise SystemExit('Invalid PKG entry table')
    metadata = {0x1000:'sce_sys/param.sfo',0x1200:'sce_sys/icon0.png'}
    for index in range(count):
        entry = struct.unpack_from('>6I',pkg_bytes,table+index*32)
        if entry[4]+entry[5]>len(pkg_bytes): raise SystemExit('PKG entry out of bounds')
        if entry[0] in metadata:
            target = extracted/'uroot'/metadata[entry[0]]
            target.parent.mkdir(parents=True,exist_ok=True)
            # Despite its argument name, PkgTool expects a table INDEX, not entry ID.
            run('extract PKG metadata '+metadata[entry[0]],
                [pkgtool,'pkg_extractentry',pkg,str(index),target])
    runtime_inventory(extracted/'uroot', expected=report['runtime_modules'])
    payloads = PACKAGE_FILES
    for name in payloads:
        if not (extracted/'uroot'/name).is_file():
            raise SystemExit('PKG round-trip missing file: '+name)
        if name=='sce_sys/param.sfo':
            original=read_sfo(stage/name); actual=read_sfo(extracted/'uroot'/name)
            if any(actual.get(k)!=v for k,v in original.items()): raise SystemExit('PKG changed required SFO fields')
            if set(actual)-set(original)!={'PUBTOOLINFO','PUBTOOLVER'}: raise SystemExit('Unexpected builder SFO additions')
        elif sha(stage/name)!=sha(extracted/'uroot'/name):
            raise SystemExit('PKG round-trip mismatch: '+name)
    report['identity_validation'] = validate_package_identity(pkg,read_sfo(extracted/'uroot/sce_sys/param.sfo'))
    report['package'] = {'content_id': CONTENT_ID, 'title_id': TITLE_ID,
        'sha256': sha(pkg), 'bytes': pkg.stat().st_size,
        'payload_sha256':{p:sha(extracted/'uroot'/p) for p in payloads},
        'input_payload_sha256':{p:sha(stage/p) for p in payloads},
        'round_trip_verified': True, 'validator_ok_checks':validation.count('[OK]'), 'hardware_tested': False,
        'packaged_sfo':read_sfo(extracted/'uroot/sce_sys/param.sfo'),
        'sfo_note':'PkgTool adds PUBTOOLINFO/PUBTOOLVER; every original SFO value was compared and retained.',
        'tool_sha256':{p.name:sha(p) for p in [fself,pkgtool,gp4tool]}}
    (output/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Packaged application:',pkg)

if __name__ == '__main__':
    main()
