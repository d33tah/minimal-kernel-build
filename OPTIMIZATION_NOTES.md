# FS Code Reduction Analysis

## Current State (commit 7dc7146)
- bzImage: 494KB  
- Total minified lines: 310,985
- FS directory lines: 35,328
- Status: ✅ Fully working (make vm passes)

## Files Successfully Stubbed (from cb79361)
These are safe and working:
- d_path.c (451 → minimal)
- fcntl.c (835 → minimal)
- ioctl.c (838 → minimal) 
- readdir.c (384 → minimal)
- remap_range.c (547 → minimal)
- select.c (1109 → minimal)
- seq_file.c (1093 → minimal)
- splice.c (1718 → minimal)
- stat.c (729 → minimal)
- utimes.c (229 → minimal)
- xattr.c (1183 → minimal)

## Files That CANNOT Be Fully Stubbed (break boot)
- fs/read_write.c (1554 lines) - Critical for I/O syscalls used by init
- kernel/notifier.c - Critical for notifier chain functionality
- kernel/resource.c - Critical for resource management

## Largest Remaining Unstubbed FS Files
1. namei.c (4857 lines) - Path resolution, partially stubbed
2. namespace.c (4559 lines) - Mount operations
3. dcache.c (3209 lines) - Dentry caching
4. inode.c (2251 lines) - Inode operations, partially stubbed
5. exec.c (1861 lines) - Binary execution (needed for init)
6. fs-writeback.c (1813 lines) - Writeback operations
7. super.c (1566 lines) - Superblock operations
8. read_write.c (1554 lines) - CRITICAL, cannot stub
9. libfs.c (1495 lines) - Library functions
10. open.c (1442 lines) - File open operations (needed for init)

## Reduction Opportunities

### Low Risk (likely safe to stub)
- namespace.c - Mount operations not used by minimal init
- fs-writeback.c - Writeback not critical for read-only init
- super.c - Can minimize superblock complexity for ramfs-only

### Medium Risk (needs testing)
- libfs.c - Helper functions, some may be used
- pipe.c - Pipe operations, init doesn't use pipes  
- file.c - File descriptor operations, partially needed

### High Risk (likely to break boot)
- namei.c - Path resolution needed for init execution
- dcache.c - Dentry caching likely needed
- inode.c - Inode operations likely needed
- exec.c - Binary execution absolutely needed
- open.c - File opening needed for init
- read_write.c - Already confirmed critical

## Recommendation
Focus on safely stubbing namespace.c, fs-writeback.c, and super.c with
careful testing after each change using `make vm`.
