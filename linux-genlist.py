#!/usr/bin/env python3

import subprocess
import os
import pathlib

# a .tar.gz containing /init and /dev/console
buf = '''H4sIAAAAAAAAA+3aTWrbQBQH8OdPkhaMCyUESkGGQAstrmRL0QkausgdjFyLxiB7iqWk7
ao5QhY5gI7RJmcI5B5ZZZdVkvekGVd1UrcFq6X0/4NhZjxP8khPG6EXRuoVlcxmvudlPVvss7Hj+rb
vu/a265Lt2H3PJssre2NiP06CmWXRTKlkWdzP1v9RIed/PB2XemnL89/jtW/5dz3Jv9Pjx8Cyy9yU8
Z/n//Pr3Z1KpTKfV6lG2eyovuZyd2Xlv3M6eOU5NalB8+jD+pq0Yx5Kq8tvbcrX2/k5pG3yUFqjuC7
nTXmdW4uH0prmOAAAAAAAAABYqRN58f4qb+Gn8lr+Rd7hzw4vrv/ytgAAAAAAAABghd6EUaReWh/UL
Bp1HtxdrxTGdbq8ecj9hnzX16vZ5/rUzKv0mPJv+bnLm6fS6fg2x0sdQCs18yptLcxfSF+Y055ssBv
EE4qUek+T+B1F4ZQGg2EcD6R2I6FBOAqSgLvpiKgbf5okwZD7ZJb3e2aUhB8T6maxv+yJvgdNcx3tv
JbBMOUKHR1XM/fDKt4HXRuhY6rF4znuSK83dC/nXDfn0lKOK9bDmLw8Wog757hn98T9iNT/jMKDUmv
Afqf+S9f/OD0P9V9/gsn/WzWNVRSW8h+S4O27eV+S/77vOmT1S9nNglXlP78O85A7q90kAAAAAAAAA
AAAAAAAAAAAAAAAwPduAUxnQDAAUAAA'''

p = subprocess.Popen('base64 -d | tar zxvf -', shell=True, encoding='utf8', stdin=subprocess.PIPE)
p.stdin.write(buf)
p.stdin.close()
p.wait()

with open('kernel/configs/tiny.config', 'w') as f:
    f.write('''
CONFIG_BLK_DEV_INITRD=y
CONFIG_INITRAMFS_SOURCE="elo"
CONFIG_BINFMT_ELF=y
CONFIG_TTY=y
CONFIG_INITRAMFS_COMPRESSION_NONE=y
CONFIG_CC_OPTIMIZE_FOR_SIZE=y
CONFIG_BASE_SMALL=1
# CONFIG_KERNEL_GZIP is not set
CONFIG_KERNEL_XZ=y
# CONFIG_RD_GZIP is not set
# CONFIG_RD_BZIP2 is not set
# CONFIG_RD_LZMA is not set
# CONFIG_RD_XZ is not set
# CONFIG_RD_LZO is not set
# CONFIG_RD_LZ4 is not set
# CONFIG_RD_ZSTD is not set
# CONFIG_UNIX98_PTYS is not set
# CONFIG_LEGACY_PTYS is not set
# CONFIG_LDISC_AUTOLOAD is not set
# CONFIG_CRC32 is not set
# CONFIG_XZ_DEC is not set
CONFIG_LTO=y
CONFIG_LTO_CLANG=y
CONFIG_ARCH_SUPPORTS_LTO_CLANG=y
CONFIG_ARCH_SUPPORTS_LTO_CLANG_THIN=y
CONFIG_HAS_LTO_CLANG=y
# CONFIG_LTO_NONE is not set
CONFIG_LTO_CLANG_FULL=y
''')

subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.allnoconfig '
    'make LLVM=1 tinyconfig', shell=True)

subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.make '
    'make LLVM=1 -j $(( $( nproc ) + 2 ))', shell=True)
subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.mrproper '
    'make mrproper', shell=True)

cwd = os.getcwd()

tokens = ['stat', 'lstat', 'execve']
b = set()
pdir = pathlib.Path('.')
for p in pdir.glob('strace*'):
    with open(str(p)) as f:
        for line in f:
            if line.endswith('>\n'):
                rev = line[::-1]
                x = rev.split('<')[0][::-1][:-2]
                b.add(x)
            else:
                for token in tokens:
                    if line.startswith(token + '("'):
                        x = pathlib.Path(line.split('"')[1])
                        b.add(str(x.absolute()))


a = {x.strip() for x in subprocess.check_output(
    'find `pwd` -type f', shell=True, encoding='utf8').splitlines()}
for x in a.difference(b):
    if x:
        os.unlink(x)
