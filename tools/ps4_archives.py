"""Build the pinned PS4 archive libraries with the OpenOrbis public headers."""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
from pathlib import Path
import os
import shutil
import subprocess
import tarfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    'faad2': ('faad2-2.11.4', 'knik0/faad2', '2.11.4',
              'ee479ccbae4a8387ab696e6f21a481bd83fe3881471cafa81b4ae59d7d3aed43', 'COPYING'),
    'libarchive': ('libarchive-3.8.11', 'libarchive/libarchive', 'v3.8.11',
                   '6e9560b2e0659ff688c5efb7717a68bb06b28ef65a69addcd6c424eaf9c319b1', 'COPYING'),
    'zlib': ('zlib-1.3.2', 'madler/zlib', 'v1.3.2',
             'b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1', 'LICENSE'),
    'xz': ('xz-5.8.3', 'tukaani-project/xz', 'v5.8.3',
           '8ec1767fa517642ecb4cf08b891ce667ba6f143551e382b07c7ef437bda335e2', 'COPYING'),
    'bzip2': ('bzip2-1.0.8', None, None,
              'ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269', 'LICENSE'),
    'zstd': ('zstd-1.5.7', 'facebook/zstd', 'v1.5.7',
             '37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3', 'LICENSE'),
    'mbedtls': ('mbedtls-mbedtls-3.6.7', 'Mbed-TLS/mbedtls', 'mbedtls-3.6.7',
                '807f0a1cc7c63b571180e9efeb6236b677e550869eb0442ace33aa9c69055fcc', 'LICENSE'),
}


def _source(cache, name):
    directory, repo, tag, digest, notice = SOURCES[name]
    packed = cache / f'{name}-{tag or "1.0.8"}.tar.gz'
    if not packed.is_file():
        url = (f'https://codeload.github.com/{repo}/tar.gz/refs/tags/{tag}' if repo
               else 'https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz')
        print(f'Fetch pinned {directory}', flush=True)
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read(32 * 1024 * 1024)
        if sha256(data).hexdigest() != digest:
            raise RuntimeError(f'{name} archive checksum mismatch')
        packed.write_bytes(data)
    elif sha256(packed.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'{packed} checksum mismatch')
    source = cache / directory
    if not (source / '.psmtp-source').is_file():
        with tarfile.open(packed) as archive:
            archive.extractall(cache, filter='data')
        (source / '.psmtp-source').write_text(digest, encoding='ascii')
    notices = (source / notice).read_bytes()
    if name == 'xz':
        notices += b'\n\nliblzma (BSD Zero Clause License)\n\n' + (source / 'COPYING.0BSD').read_bytes()
    return source, notices


def _xz(source, output, cc, target, sdk, ar):
    cmake = shutil.which('cmake') or ROOT / '.tools/cmake/data/bin/cmake.exe'
    ninja = shutil.which('ninja') or ROOT / '.tools/bin/ninja.exe'
    if not Path(cmake).is_file() or not Path(ninja).is_file():
        raise RuntimeError('Install CMake and Ninja to build the PS4 archive libraries.')
    output.mkdir(exist_ok=True)
    toolchain = output / 'ps4-archive-toolchain.cmake'
    ar_path = ar[0]
    ranlib_path = shutil.which('llvm-ranlib') or shutil.which('ranlib')
    if len(ar) > 1:
        if os.name == 'nt':
            ar_path = output / 'archive-ar.cmd'
            ranlib_path = output / 'archive-ranlib.cmd'
            ar_path.write_text(f'@"{ar[0]}" ar %*\n', encoding='utf-8')
            ranlib_path.write_text(f'@"{ar[0]}" ranlib %*\n', encoding='utf-8')
        else:
            ar_path = output / 'archive-ar'
            ranlib_path = output / 'archive-ranlib'
            for tool, command in [(ar_path, 'ar'), (ranlib_path, 'ranlib')]:
                tool.write_text(f'#!/bin/sh\nexec "{ar[0]}" {command} "$@"\n', encoding='utf-8')
                tool.chmod(0o755)
    command = ' '.join([*cc[1:], '-target', target, '-nostdinc', '-isystem', f'"{sdk.as_posix()}/include"', '-D_GNU_SOURCE', '-fPIC'])
    toolchain.write_text(
        'set(CMAKE_SYSTEM_NAME Generic)\nset(CMAKE_SYSTEM_PROCESSOR x86_64)\n'
        'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n'
        f'set(CMAKE_C_COMPILER [=[{Path(cc[0]).as_posix()}]=])\n'
        f'set(CMAKE_C_FLAGS_INIT [=[{command}]=])\n'
        f'set(CMAKE_AR [=[{Path(ar_path).as_posix()}]=])\n'
        + (f'set(CMAKE_RANLIB [=[{Path(ranlib_path).as_posix()}]=])\n' if ranlib_path else ''), encoding='utf-8')
    options = ['NLS', 'DOC', 'TOOL_XZ', 'TOOL_XZDEC', 'TOOL_LZMADEC', 'TOOL_LZMAINFO', 'CLMUL_CRC']
    _run([cmake, '-S', source, '-B', output, '-G', 'Ninja',
          f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DCMAKE_MAKE_PROGRAM={ninja}',
          '-DCMAKE_BUILD_TYPE=Release', '-DXZ_THREADS=no', '-DBUILD_TESTING=OFF',
          *[f'-DXZ_{option}=OFF' for option in options]])
    _run([cmake, '--build', output, '--target', 'liblzma', '--parallel', '8'])
    return output / 'liblzma.a'


def _run(command, cwd=None):
    result = subprocess.run([str(part) for part in command], cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def _archive_cryptor(path, output):
    """Set the Mbed TLS key once per WinZip AES member, rather than per block."""
    text = path.read_text(encoding='utf-8')
    before = '''	mbedtls_aes_init(&ctx->ctx);
	ctx->key_len = key_len;
	memcpy(ctx->key, key, key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	ctx->encr_pos = AES_BLOCK_SIZE;
	return 0;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	if (mbedtls_aes_setkey_enc(&ctx->ctx, ctx->key,
	    ctx->key_len * 8) != 0)
		return (-1);'''
    after = '''	mbedtls_aes_init(&ctx->ctx);
	if (mbedtls_aes_setkey_enc(&ctx->ctx, key, key_len * 8) != 0) {
		mbedtls_aes_free(&ctx->ctx);
		return (-1);
	}
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	ctx->encr_pos = AES_BLOCK_SIZE;
	return 0;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{'''
    if text.count(before) != 1:
        raise RuntimeError('The pinned libarchive Mbed TLS AES backend has changed.')
    source = output / path.name
    source.write_text(text.replace(before, after, 1), encoding='utf-8')
    return source


def build_archives(output, cc, target, sdk, ar):
    cache = ROOT / '.tools/downloads/ps4-archive'
    cache.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=len(SOURCES)) as pool:
        pairs = dict(zip(SOURCES, pool.map(lambda name: _source(cache, name), SOURCES)))
    paths = {name: pair[0] for name, pair in pairs.items()}
    output.mkdir(exist_ok=True)
    port = ROOT / 'src/ps4'
    la = paths['libarchive'] / 'libarchive'
    zlib = paths['zlib']
    bzip = paths['bzip2']
    mbed = paths['mbedtls']
    zstd = paths['zstd'] / 'lib'
    xz = paths['xz']
    flags = [*cc, '-target', target, '-std=gnu99', '-nostdinc', '-isystem', sdk / 'include',
             '-fPIC', '-O2', '-g0', '-fshort-wchar', '-fstack-protector-strong', '-D_GNU_SOURCE', '-DNO_ASM',
             f'-ffile-prefix-map={ROOT}=.']
    la_flags = [f'-I{path}' for path in [port, zlib, bzip, zstd, xz / 'src/liblzma/api', mbed / 'include']]
    la_flags += ['-DPLATFORM_CONFIG_H="archive_ps4_config.h"', '-DMBEDTLS_CONFIG_FILE="archive_crypto_config.h"']
    names = ('archive_acl archive_blake2s_ref archive_blake2sp_ref archive_check_magic archive_cmdline archive_crc32 archive_cryptor archive_digest archive_entry '
             'archive_entry_copy_stat archive_entry_link_resolver archive_entry_sparse archive_entry_stat archive_entry_xattr '
             'archive_hmac archive_match archive_options archive_ppmd7 archive_ppmd8 archive_rb '
             'archive_read archive_read_add_passphrase archive_read_set_options archive_read_support_filter_program '
             'archive_read_support_filter_bzip2 archive_read_support_filter_gzip archive_read_support_filter_xz '
             'archive_read_support_filter_zstd archive_read_support_format_7zip archive_read_support_format_cpio '
             'archive_read_support_format_rar archive_read_support_format_rar5 archive_read_support_format_raw '
             'archive_read_support_format_empty '
             'archive_read_support_format_tar archive_read_support_format_zip archive_string '
             'archive_string_sprintf archive_time archive_util archive_virtual archive_write archive_write_set_format_zip '
             'archive_write_set_format_pax archive_write_set_format_ustar archive_write_set_format_raw '
             'archive_write_set_format_cpio_newc archive_write_add_filter_gzip '
             'archive_write_add_filter_bzip2 archive_write_add_filter_xz archive_write_add_filter_zstd '
             'archive_write_set_options archive_write_set_passphrase filter_fork_posix').split()
    sources = [(la / (name + '.c'), la_flags) for name in names]
    faad = paths['faad2']
    aac_flags = [f'-I{faad / "include"}', f'-I{faad / "libfaad"}', '-DHAVE_INTTYPES_H=1', '-DHAVE_MEMCPY=1',
                 '-DHAVE_STRING_H=1', '-DHAVE_STRINGS_H=1', '-DHAVE_LRINTF=1',
                 '-DAPPLY_DRC', '-DPACKAGE_VERSION="2.11.4"']
    sources.extend((path, aac_flags) for path in sorted((faad / 'libfaad').glob('*.c')))
    # The 7-Zip writer needs a seekable spool. Use PS MTP's checked data
    # directory through the platform adapter instead of the SDK's /tmp.
    sources.append((la / 'archive_write_set_format_7zip.c',
                    [*la_flags, '-D__archive_mktemp=psmtp_archive_scratch']))
    for name in ('adler32 crc32 deflate inflate inffast inftrees trees zutil compress uncompr').split():
        sources.append((zlib / (name + '.c'), []))
    for name in ('blocksort huffman crctable randtable compress decompress bzlib').split():
        sources.append((bzip / (name + '.c'), []))
    for directory in ('common', 'decompress', 'compress'):
        for source in sorted((zstd / directory).glob('*.c')):
            sources.append((source, ['-DZSTD_DISABLE_ASM=1', '-DZSTD_NO_INTRINSICS=1', '-DZSTD_TRACE=0']))
    for name in ('aes aesni md sha1 sha256 pkcs5 platform_util constant_time').split():
        sources.append((mbed / 'library' / (name + '.c'), [f'-I{mbed / "include"}', f'-I{port}',
                        '-DMBEDTLS_CONFIG_FILE="archive_crypto_config.h"']))
    print('Build archive formats and compression backends', flush=True)
    xz_lib = _xz(xz, output / 'xz', cc, target, sdk, ar)
    config = (Path(__file__).read_bytes() + (port / 'archive_ps4_config.h').read_bytes() +
              (port / 'archive_crypto_config.h').read_bytes() + str(flags).encode())
    fingerprint = sha256(config).hexdigest()
    stamp = output / 'sources.sha256'
    cached = stamp.is_file() and stamp.read_text(encoding='ascii') == fingerprint

    def compile_source(item):
        path, extra_flags = item
        obj = output / ('_'.join(path.relative_to(cache).parts) + '.o')
        if not cached or not obj.is_file():
            if path == la / 'archive_cryptor.c':
                path = _archive_cryptor(path, output)
                extra_flags = [*extra_flags, f'-I{la}']
            _run([*flags, *extra_flags, '-c', path, '-o', obj])
        return obj.name

    with ThreadPoolExecutor(max_workers=min(12, os.cpu_count() or 4)) as pool:
        objects = list(pool.map(compile_source, sources))
    library = output / 'libpsmtp_archive.a'
    library.unlink(missing_ok=True)
    _run([*ar, 'rcs', library.name, *objects], cwd=output)
    stamp.write_text(fingerprint, encoding='ascii')
    notices = b''.join(f'\n\n{SOURCES[name][0]}\n\n'.encode() + notice for name, (_, notice) in pairs.items())
    notices += b'\nCode from FAAD2 is copyright (c) Nero AG, www.nero.com\n'
    return [la, faad / 'include'], [library, xz_lib], notices, {name: item[3] for name, item in SOURCES.items()}
