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

subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.allnoconfig '
    'make allnoconfig', shell=True)

diff = '''
--- .config     2024-10-28 18:15:29.674697949 +0100
+++ .config_    2024-10-28 18:15:24.265751337 +0100
@@ -140,7 +140,18 @@
 # CONFIG_SCHED_AUTOGROUP is not set
 # CONFIG_SYSFS_DEPRECATED is not set
 # CONFIG_RELAY is not set
-# CONFIG_BLK_DEV_INITRD is not set
+CONFIG_BLK_DEV_INITRD=y
+CONFIG_INITRAMFS_SOURCE="elo"
+CONFIG_INITRAMFS_ROOT_UID=0
+CONFIG_INITRAMFS_ROOT_GID=0
+# CONFIG_RD_GZIP is not set
+# CONFIG_RD_BZIP2 is not set
+# CONFIG_RD_LZMA is not set
+# CONFIG_RD_XZ is not set
+# CONFIG_RD_LZO is not set
+# CONFIG_RD_LZ4 is not set
+# CONFIG_RD_ZSTD is not set
+CONFIG_INITRAMFS_COMPRESSION_NONE=y
 # CONFIG_BOOT_CONFIG is not set
 # CONFIG_INITRAMFS_PRESERVE_MTIME is not set
 CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE=y
@@ -527,7 +538,9 @@
 #
 # Executable file formats
 #
-# CONFIG_BINFMT_ELF is not set
+CONFIG_BINFMT_ELF=y
+CONFIG_ELFCORE=y
+CONFIG_CORE_DUMP_DEFAULT_ELF_HEADERS=y
 # CONFIG_BINFMT_SCRIPT is not set
 # CONFIG_BINFMT_MISC is not set
 CONFIG_COREDUMP=y
'''

with open('.config.diff', 'w') as f:
    f.write(diff)

subprocess.check_call('patch  < .config.diff', shell=True)

subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.make '
    'make -j $(( $( nproc ) + 2 ))', shell=True)
subprocess.check_call('make mrproper', shell=True)

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
