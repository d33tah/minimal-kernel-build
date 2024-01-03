#!/usr/bin/env python3

# equivalent of exec gcc $@

import sys
import os
import subprocess

in_fname = [x for x in sys.argv if x.endswith('.c')]
out_fname = [x for x in sys.argv if x.endswith('.o')]

if in_fname and out_fname:
    new_ofname = out_fname[0] + '.preprocessed'
    new_ifname = in_fname[0] + '.rewritten.c'

    PLACEHOLDER = '/* d33tah */\n'

    # add a placeholder and an include marker for each of the files
    with open(new_ifname, 'w') as f_out, open(in_fname[0], 'r') as f_in:
        f_out.write(PLACEHOLDER)
        for n, line in enumerate(f_in.readlines()):
            if line.lstrip().startswith('#include'):
                # FIXME: if line contains the beginning of a comment,
                # we can lose it. detect this and remove the comment
                # the current workaround is to change those files manually.
                f_out.write(f'int include_starts_{n}; // {line}')
                f_out.write(line)
                f_out.write(f'int include_ends_{n};\n')
            else:
                f_out.write(line)

    argv_rewritten = []
    for arg in sys.argv[1:]:
        if arg == in_fname[0]:
            argv_rewritten.append(new_ifname)
        elif arg == out_fname[0]:
            argv_rewritten.append(new_ofname)
        else:
            argv_rewritten.append(arg)

    subprocess.check_call(['gcc', '-E', '-P', '-fdirectives-only', '-dI'] + argv_rewritten)

    with open(in_fname[0]) as f:
        first_input_line = next(f)
        try:
            second_line = next(f)
            has_second_line = True
        except StopIteration:
            has_second_line = False

    with open(new_ofname, 'r') as f_in:
        lines = f_in.readlines()

    if has_second_line:
        with open(in_fname[0], 'w') as f_out:
            i = iter(lines)
            while not line == PLACEHOLDER:
                line = next(i)
            while True:
                try:
                    line = next(i)
                except StopIteration:
                    break
                if line.startswith('int include_starts_'):
                    f_out.write(next(i))
                    while not line.startswith('int include_ends_'):
                        line = next(i)
                else:
                    f_out.write(line)

    os.unlink(new_ifname)
    os.unlink(new_ofname)
os.execvp("gcc", ["gcc"] + sys.argv[1:])
