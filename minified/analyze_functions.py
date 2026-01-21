import re
import os

# List of small static functions to analyze
functions_to_check = [
    # fs/super.c
    ('set_anon_super_fc', 'fs/super.c', 377, 3),  # lines 377-379: 3 lines
    ('test_keyed_super', 'fs/super.c', 382, 2),   # lines 382-384: 2 lines
    ('test_single_super', 'fs/super.c', 387, 2),  # lines 387-389: 2 lines
    
    # fs/libfs.c
    ('empty_dir_lookup', 'fs/libfs.c', 224, 2),   # lines 224-227: 2 lines
    ('empty_dir_setattr', 'fs/libfs.c', 232, 2),  # lines 232-235: 2 lines
    ('simple_read_folio', 'fs/libfs.c', 152, 4),  # lines 152-157: 4 lines
    
    # kernel/locking/rwsem.c - small inline functions
    ('__down_read', 'kernel/locking/rwsem.c', 541, 2),      # lines 541-543: 2 lines
    ('__down_read_killable', 'kernel/locking/rwsem.c', 546, 2),  # lines 546-548: 2 lines
    ('__down_write', 'kernel/locking/rwsem.c', 578, 2),     # lines 578-580: 2 lines
    ('__down_write_killable', 'kernel/locking/rwsem.c', 583, 2), # lines 583-585: 2 lines
    
    # kernel/locking/mutex.c - small inline functions
    ('__mutex_owner', 'kernel/locking/mutex.c', 29, 2),     # lines 29-32: 3 lines
    ('__owner_task', 'kernel/locking/mutex.c', 35, 2),      # lines 35-37: 2 lines
    ('__owner_flags', 'kernel/locking/mutex.c', 45, 2),     # lines 45-47: 2 lines
    ('__mutex_trylock', 'kernel/locking/mutex.c', 94, 2),   # lines 94-96: 2 lines
    ('__mutex_waiter_is_first', 'kernel/locking/mutex.c', 117, 2),  # 2 lines body
    
    # kernel/time/clocksource.c
    ('clocksource_watchdog_lock', 'kernel/time/clocksource.c', 46, 2),   # 2 lines
    ('clocksource_watchdog_unlock', 'kernel/time/clocksource.c', 51, 2), # 2 lines
]

# Count calls to each function across the codebase
def count_callers(func_name, exclude_file=None):
    """Count number of callers for a given function"""
    count = 0
    files_with_calls = []
    
    # Search in all C files
    for root, dirs, filenames in os.walk('.'):
        # Skip certain directories
        if '.git' in root or '__pycache__' in root:
            continue
        
        for fname in filenames:
            if fname.endswith('.c') or fname.endswith('.h'):
                filepath = os.path.join(root, fname)
                
                # Skip the file where function is defined
                if exclude_file and exclude_file in filepath:
                    continue
                
                try:
                    with open(filepath, 'r', errors='ignore') as f:
                        content = f.read()
                        # Look for calls - not in comments and not in function definition
                        # Simple pattern: word boundary + func_name + (
                        pattern = r'\b' + func_name + r'\s*\('
                        matches = re.finditer(pattern, content)
                        for match in matches:
                            # Check if it's not in a comment
                            line_start = content.rfind('\n', 0, match.start()) + 1
                            line = content[line_start:content.find('\n', match.start())]
                            if '//' not in line[:line.find(func_name)] and '/*' not in line[:line.find(func_name)]:
                                count += 1
                                if filepath not in files_with_calls:
                                    files_with_calls.append(filepath)
                except:
                    pass
    
    return count, files_with_calls

print("Function Analysis - Call Counts and Inlining Candidates")
print("=" * 80)

for func_name, file_path, line_num, approx_lines in functions_to_check:
    count, callers = count_callers(func_name, exclude_file=file_path)
    print(f"\n{func_name:30} {file_path}:{line_num}")
    print(f"  Lines: {approx_lines}, Callers: {count}")
    if count > 0 and count <= 2:
        print(f"  CANDIDATE - Called from: {', '.join([c.replace('./','').replace('.c','') for c in callers[:3]])}")
    elif count <= 0:
        print(f"  WARNING - No callers found!")
    
