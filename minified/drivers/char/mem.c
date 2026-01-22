/* mem.c - All device operations removed - devlist was never used.
 * The null/zero/full/random/urandom device fops were only referenced by devlist,
 * which was never accessed. chr_dev_init removed - tty_init hangs with low memory.
 * Hello World uses direct VGA writes, doesn't need TTY */
