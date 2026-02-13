
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
/* magic.h removed - not used */
#include <linux/security.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
/* Inlined from net_namespace.h */
struct net {
	atomic_t count;
	struct user_namespace *user_ns;
};
static inline struct net *get_net(struct net *net)
{
	return net;
}
static inline void put_net(struct net *net)
{
}
#include <asm/sections.h>
#include "mount.h"
#include "internal.h"

/* legacy_fs_context, legacy_fs_param removed - all fs types have init_fs_context */

/* common_set_sb_flag, common_clear_sb_flag tables removed - no mount options during boot */

int vfs_parse_fs_param_source(struct fs_context *fc, struct fs_parameter *param)
{
	if (strcmp(param->key, "source") != 0)
		return -ENOPARAM;

	if (param->type != fs_value_is_string)
		return invalf(fc, "Non-string source");

	if (fc->source)
		return invalf(fc, "Multiple sources");

	fc->source = param->string;
	param->string = NULL;
	return 0;
}

int vfs_parse_fs_param(struct fs_context *fc, struct fs_parameter *param)
{
	int ret;

	if (!param->key)
		return invalf(fc, "Unnamed parameter\n");

	/* vfs_parse_sb_flag removed - no mount options like ro/sync during boot */

	/* security_fs_context_parse_param always returns -ENOPARAM */

	if (fc->ops->parse_param) {
		ret = fc->ops->parse_param(fc, param);
		if (ret != -ENOPARAM)
			return ret;
	}

	ret = vfs_parse_fs_param_source(fc, param);
	if (ret != -ENOPARAM)
		return ret;

	return invalf(fc, "%s: Unknown parameter '%s'", fc->fs_type->name,
		      param->key);
}

int vfs_parse_fs_string(struct fs_context *fc, const char *key,
			const char *value, size_t v_size)
{
	int ret;

	struct fs_parameter param = {
		.key = key,
		.type = fs_value_is_flag,
		.size = v_size,
	};

	if (value) {
		param.string = kmemdup_nul(value, v_size, GFP_KERNEL);
		if (!param.string)
			return -ENOMEM;
		param.type = fs_value_is_string;
	}

	ret = vfs_parse_fs_param(fc, &param);
	kfree(param.string);
	return ret;
}

/* generic_parse_monolithic option-parsing loop removed - data is always NULL */
int generic_parse_monolithic(struct fs_context *fc, void *data)
{
	return 0;
}

/* alloc_fs_context simplified - only FS_CONTEXT_FOR_MOUNT used */
struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
					unsigned int sb_flags)
{
	struct fs_context *fc;
	int ret = -ENOMEM;

	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL_ACCOUNT);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->purpose = FS_CONTEXT_FOR_MOUNT;
	fc->sb_flags = sb_flags;
	fc->sb_flags_mask = 0;
	fc->fs_type = get_filesystem(fs_type);
	fc->cred = get_current_cred();
	fc->net_ns = get_net(current->nsproxy->net_ns);
	fc->log.prefix = fs_type->name;

	mutex_init(&fc->uapi_mutex);

	fc->user_ns = get_user_ns(fc->cred->user_ns);

	/* All fs types have init_fs_context - no legacy fallback needed */
	ret = fc->fs_type->init_fs_context(fc);
	if (ret < 0)
		goto err_fc;
	fc->need_free = true;
	return fc;

err_fc:
	put_fs_context(fc);
	return ERR_PTR(ret);
}

/* fs_context_for_reconfigure removed - never called */
/* fc_drop_locked removed - never called */

void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt,
	   ...)
{
	va_list va;
	struct va_format vaf = { .fmt = fmt, .va = &va };

	va_start(va, fmt);
	/* log buffer path removed - fc->log.log is never set */
	switch (level) {
	case 'w':
		printk(KERN_WARNING "%s%s%pV\n", prefix ? prefix : "",
		       prefix ? ": " : "", &vaf);
		break;
	case 'e':
		printk(KERN_ERR "%s%s%pV\n", prefix ? prefix : "",
		       prefix ? ": " : "", &vaf);
		break;
	default:
		printk(KERN_NOTICE "%s%s%pV\n", prefix ? prefix : "",
		       prefix ? ": " : "", &vaf);
		break;
	}
	va_end(va);
}

void put_fs_context(struct fs_context *fc)
{
	struct super_block *sb;

	if (fc->root) {
		sb = fc->root->d_sb;
		dput(fc->root);
		fc->root = NULL;
		deactivate_super(sb);
	}

	if (fc->need_free && fc->ops && fc->ops->free)
		fc->ops->free(fc);

	/* security_free_mnt_opts - empty stub */
	put_net(fc->net_ns);
	put_user_ns(fc->user_ns);
	put_cred(fc->cred);
	/* put_fc_log removed - log is never allocated */
	put_filesystem(fc->fs_type);
	kfree(fc->source);
	kfree(fc);
}

/* legacy_fs_context_free, legacy_fs_context_dup, legacy_parse_param,
   legacy_parse_monolithic, legacy_get_tree, legacy_reconfigure,
   legacy_fs_context_ops, legacy_init_fs_context all removed -
   all fs types have init_fs_context set */

int parse_monolithic_mount_data(struct fs_context *fc, void *data)
{
	int (*monolithic_mount_data)(struct fs_context *, void *);

	monolithic_mount_data = fc->ops->parse_monolithic;
	if (!monolithic_mount_data)
		monolithic_mount_data = generic_parse_monolithic;

	return monolithic_mount_data(fc, data);
}
