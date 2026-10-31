
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/user_namespace.h>

/* No mount options are ever passed: there is no option parser. */
struct fs_context *fs_context_for_mount(struct file_system_type *fs_type)
{
	struct fs_context *fc;
	int ret;

	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL_ACCOUNT);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->fs_type = fs_type;
	fc->cred = get_current_cred();
	fc->user_ns = get_user_ns(fc->cred->user_ns);

	ret = fs_type->init_fs_context(fc);
	if (ret < 0) {
		put_fs_context(fc);
		return ERR_PTR(ret);
	}
	return fc;
}

void put_fs_context(struct fs_context *fc)
{
	dput(fc->root);
	put_cred(fc->cred);
	kfree(fc);
}
