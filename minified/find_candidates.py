#!/usr/bin/env python3
import re
import os
import subprocess

def get_function_lines(filepath, func_name, start_line):
    """Get actual line count of a function"""
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
        
        # Start from the function declaration
        idx = start_line - 1
        if idx >= len(lines):
            return None
        
        # Find opening brace
        while idx < len(lines) and '{' not in lines[idx]:
            idx += 1
        
        if idx >= len(lines):
            return None
        
        start_brace = idx
        brace_count = 0
        
        # Count until we find matching closing brace
        while idx < len(lines):
            brace_count += lines[idx].count('{')
            brace_count -= lines[idx].count('}')
            if brace_count == 0 and '{' in lines[idx]:
                # Count lines in function body (excluding braces)
                body_lines = idx - start_brace + 1
                return body_lines, lines[start_brace:idx+1]
            idx += 1
        
        return None
    except:
        return None

def count_calls(func_name, exclude_file=None):
    """Count calls to a function"""
    calls = []
    for root, dirs, files in os.walk('.'):
        if '.git' in root or '__pycache__' in root:
            continue
        for f in files:
            if f.endswith('.c'):
                fpath = os.path.join(root, f)
                if exclude_file and exclude_file in fpath:
                    continue
                try:
                    with open(fpath, 'r', errors='ignore') as file:
                        content = file.read()
                        # Find all calls - simple pattern
                        pattern = r'\b' + re.escape(func_name) + r'\s*\('
                        for match in re.finditer(pattern, content):
                            # Get line number
                            line_num = content[:match.start()].count('\n') + 1
                            calls.append((fpath, line_num))
                except:
                    pass
    return calls

# Small static functions to analyze
candidates = [
    ('clocksource_find_best', 'kernel/time/clocksource.c', 85),
    ('__clocksource_select', 'kernel/time/clocksource.c', 100),
    ('clocksource_watchdog_lock', 'kernel/time/clocksource.c', 46),
    ('clocksource_watchdog_unlock', 'kernel/time/clocksource.c', 51),
    ('clocksource_max_adjustment', 'kernel/time/clocksource.c', 58),
    ('tick_periodic', 'kernel/time/tick-common.c', 19),
    ('tick_setup_device', 'kernel/time/tick-common.c', 79),
    ('jiffies_read', 'kernel/time/jiffies.c', 9),
    ('__owner_task', 'kernel/locking/mutex.c', 35),
    ('__owner_flags', 'kernel/locking/mutex.c', 45),
    ('__mutex_waiter_is_first', 'kernel/locking/mutex.c', 117),
    ('rwsem_set_owner', 'kernel/locking/rwsem.c', 57),
    ('rwsem_test_oflags', 'kernel/locking/rwsem.c', 62),
    ('is_rwsem_reader_owned', 'kernel/locking/rwsem.c', 82),
    ('__down_read', 'kernel/locking/rwsem.c', 541),
    ('__down_read_killable', 'kernel/locking/rwsem.c', 546),
    ('__down_write', 'kernel/locking/rwsem.c', 578),
    ('__down_write_killable', 'kernel/locking/rwsem.c', 583),
    ('empty_dir_lookup', 'fs/libfs.c', 224),
    ('empty_dir_setattr', 'fs/libfs.c', 232),
    ('always_delete_dentry', 'fs/libfs.c', 20),
]

print("Inline Candidates Analysis")
print("=" * 100)
print(f"{'Function':<35} {'File':<35} {'Lines':<6} {'Callers':<8} {'Status':<20}")
print("-" * 100)

for func_name, fpath, line_num in candidates:
    result = get_function_lines(fpath, func_name, line_num)
    if result:
        lines, body = result
        calls = count_calls(func_name, exclude_file=fpath)
        caller_count = len(calls)
        
        # Determine status
        if caller_count == 0:
            status = "UNUSED/POINTER"
        elif caller_count == 1:
            status = "SINGLE CALLER **"
        elif caller_count == 2:
            status = "TWO CALLERS *"
        else:
            status = f"{caller_count} callers"
        
        if lines <= 6 and caller_count <= 2 and caller_count > 0:
            print(f"{func_name:<35} {fpath:<35} {lines:<6} {caller_count:<8} {status:<20}")

print("\n** = Strong candidate (1 caller, 1-6 lines)")
print("*  = Good candidate (2 callers, 1-6 lines)")

