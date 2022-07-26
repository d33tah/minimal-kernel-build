#!/usr/bin/env python3

import subprocess
import os
import pathlib


s = '''
CONFIG_DUMMY_CONSOLE=y
CONFIG_EARLY_PRINTK=y
CONFIG_PRINTK=y
CONFIG_VGA_CONSOLE=y
CONFIG_VT=y
'''

with open('kernel/configs/tiny.config', 'a') as f:
    f.write(s)
subprocess.check_call(
    'strace -fff -e trace=file -y -o strace.allnoconfig '
    'make tinyconfig', shell=True)
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
