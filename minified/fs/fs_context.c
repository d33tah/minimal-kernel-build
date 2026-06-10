
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include <linux/security.h>
#include <linux/mnt_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <net/net_namespace.h>
#include <asm/sections.h>
#include "mount.h"
#include "internal.h"

static const struct constant_table common_set_sb_flag[] = {
	{ "dirsync",	SB_DIRSYNC },
	{ "lazytime",	SB_LAZYTIME },
	{ "mand",	SB_MANDLOCK },
	{ "ro",		SB_RDONLY },
	{ "sync",	SB_SYNCHRONOUS },
	{ },
};

static const struct constant_table common_clear_sb_flag[] = {
	{ "async",	SB_SYNCHRONOUS },
	{ "nolazytime",	SB_LAZYTIME },
	{ "nomand",	SB_MANDLOCK },
	{ "rw",		SB_RDONLY },
	{ },
};

static int vfs_parse_sb_flag(struct fs_context *fc, const char *key)
{
	unsigned int token;

	token = lookup_constant(common_set_sb_flag, key, 0);
	if (token) {
		fc->sb_flags |= token;
		fc->sb_flags_mask |= token;
		return 0;
	}

	token = lookup_constant(common_clear_sb_flag, key, 0);
	if (token) {
		fc->sb_flags &= ~token;
		fc->sb_flags_mask |= token;
		return 0;
	}

	return -ENOPARAM;
}

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

	ret = vfs_parse_sb_flag(fc, param->key);
	if (ret != -ENOPARAM)
		return ret;

	ret = security_fs_context_parse_param(fc, param);
	if (ret != -ENOPARAM)
		 
		return ret;

	if (fc->ops->parse_param) {
		ret = fc->ops->parse_param(fc, param);
		if (ret != -ENOPARAM)
			return ret;
	}

	 
	ret = vfs_parse_fs_param_source(fc, param);
	if (ret != -ENOPARAM)
		return ret;

	return invalf(fc, "%s: Unknown parameter '%s'",
		      fc->fs_type->name, param->key);
}

int vfs_parse_fs_string(struct fs_context *fc, const char *key,
			const char *value, size_t v_size)
{
	int ret;

	struct fs_parameter param = {
		.key	= key,
		.type	= fs_value_is_flag,
		.size	= v_size,
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

int generic_parse_monolithic(struct fs_context *fc, void *data)
{
	char *options = data, *key;
	int ret = 0;

	if (!options)
		return 0;

	while ((key = strsep(&options, ",")) != NULL) {
		if (*key) {
			size_t v_len = 0;
			char *value = strchr(key, '=');

			if (value) {
				if (value == key)
					continue;
				*value++ = 0;
				v_len = strlen(value);
			}
			ret = vfs_parse_fs_string(fc, key, value, v_len);
			if (ret < 0)
				break;
		}
	}

	return ret;
}

static struct fs_context *alloc_fs_context(struct file_system_type *fs_type,
				      struct dentry *reference,
				      unsigned int sb_flags,
				      unsigned int sb_flags_mask,
				      enum fs_context_purpose purpose)
{
	int (*init_fs_context)(struct fs_context *);
	struct fs_context *fc;
	int ret = -ENOMEM;

	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL_ACCOUNT);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->purpose	= purpose;
	fc->sb_flags	= sb_flags;
	fc->sb_flags_mask = sb_flags_mask;
	fc->fs_type	= get_filesystem(fs_type);
	fc->cred	= get_current_cred();
	fc->net_ns	= get_net(current->nsproxy->net_ns);
	fc->log.prefix	= fs_type->name;

	mutex_init(&fc->uapi_mutex);

	switch (purpose) {
	case FS_CONTEXT_FOR_MOUNT:
		fc->user_ns = get_user_ns(fc->cred->user_ns);
		break;
	case FS_CONTEXT_FOR_SUBMOUNT:
		fc->user_ns = get_user_ns(reference->d_sb->s_user_ns);
		break;
	case FS_CONTEXT_FOR_RECONFIGURE:
		atomic_inc(&reference->d_sb->s_active);
		fc->user_ns = get_user_ns(reference->d_sb->s_user_ns);
		fc->root = dget(reference);
		break;
	}

	init_fs_context = fc->fs_type->init_fs_context;
	ret = init_fs_context(fc);
	if (ret < 0)
		goto err_fc;
	fc->need_free = true;
	return fc;

err_fc:
	put_fs_context(fc);
	return ERR_PTR(ret);
}

struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
					unsigned int sb_flags)
{
	return alloc_fs_context(fs_type, NULL, sb_flags, 0,
					FS_CONTEXT_FOR_MOUNT);
}

void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf = {.fmt = fmt, .va = &va};

	va_start(va, fmt);
	if (!log) {
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
	} else {
		unsigned int logsize = ARRAY_SIZE(log->buffer);
		u8 index;
		char *q = kasprintf(GFP_KERNEL, "%c %s%s%pV\n", level,
						prefix ? prefix : "",
						prefix ? ": " : "", &vaf);

		index = log->head & (logsize - 1);
		BUILD_BUG_ON(sizeof(log->head) != sizeof(u8) ||
			     sizeof(log->tail) != sizeof(u8));
		if ((u8)(log->head - log->tail) == logsize) {
			 
			if (log->need_free & (1 << index))
				kfree(log->buffer[index]);
			log->tail++;
		}

		log->buffer[index] = q ? q : "OOM: Can't store error string";
		if (q)
			log->need_free |= 1 << index;
		else
			log->need_free &= ~(1 << index);
		log->head++;
	}
	va_end(va);
}

static void put_fc_log(struct fs_context *fc)
{
	struct fc_log *log = fc->log.log;
	int i;

	if (log) {
		if (refcount_dec_and_test(&log->usage)) {
			fc->log.log = NULL;
			for (i = 0; i <= 7; i++)
				if (log->need_free & (1 << i))
					kfree(log->buffer[i]);
			kfree(log);
		}
	}
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

	put_net(fc->net_ns);
	put_user_ns(fc->user_ns);
	put_cred(fc->cred);
	put_fc_log(fc);
	put_filesystem(fc->fs_type);
	kfree(fc->source);
	kfree(fc);
}

int parse_monolithic_mount_data(struct fs_context *fc, void *data)
{
	int (*monolithic_mount_data)(struct fs_context *, void *);

	monolithic_mount_data = fc->ops->parse_monolithic;
	if (!monolithic_mount_data)
		monolithic_mount_data = generic_parse_monolithic;

	return monolithic_mount_data(fc, data);
}


