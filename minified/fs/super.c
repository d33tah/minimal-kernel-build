#include <linux/fs.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>

/*
 * Each mount gets its own superblock and nothing is ever unmounted, so
 * there is no superblock list, active count, anon dev number or kill_sb
 * teardown. The only filesystem (ramfs, as rootfs) is mounted once.
 */
int get_tree_nodev(struct fs_context *fc,
		   int (*fill_super)(struct super_block *sb,
				     struct fs_context *fc))
{
	struct super_block *s = kzalloc(sizeof(struct super_block), GFP_USER);
	int err;

	if (!s)
		return -ENOMEM;

	s->s_user_ns = get_user_ns(fc->user_ns);
	INIT_HLIST_BL_HEAD(&s->s_roots);
	s->s_maxbytes = MAX_NON_LFS;

	err = fill_super(s, fc);
	if (err)
		return err;

	fc->root = dget(s->s_root);
	return 0;
}
