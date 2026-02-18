
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fs_parser.h>
#include <linux/user_namespace.h>
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

int generic_parse_monolithic(struct fs_context *fc, void *data)
{
	return 0;
}

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

void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt,
	   ...)
{
	va_list va;
	struct va_format vaf = { .fmt = fmt, .va = &va };

	va_start(va, fmt);
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

	put_net(fc->net_ns);
	put_user_ns(fc->user_ns);
	put_cred(fc->cred);
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
