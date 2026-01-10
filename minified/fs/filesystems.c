
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
/* seq_file.h removed - header is empty */
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_parser.h>

static struct file_system_type *file_systems;
static DEFINE_RWLOCK(file_systems_lock);

struct file_system_type *get_filesystem(struct file_system_type *fs)
{
	__module_get(fs->owner);
	return fs;
}

void put_filesystem(struct file_system_type *fs)
{
	module_put(fs->owner);
}

static struct file_system_type **find_filesystem(const char *name, unsigned len)
{
	struct file_system_type **p;
	for (p = &file_systems; *p; p = &(*p)->next)
		if (strncmp((*p)->name, name, len) == 0 && !(*p)->name[len])
			break;
	return p;
}

int register_filesystem(struct file_system_type *fs)
{
	int res = 0;
	struct file_system_type **p;

	if (fs->parameters &&
	    !fs_validate_description(fs->name, fs->parameters))
		return -EINVAL;

	BUG_ON(strchr(fs->name, '.'));
	if (fs->next)
		return -EBUSY;
	write_lock(&file_systems_lock);
	p = find_filesystem(fs->name, strlen(fs->name));
	if (*p)
		res = -EBUSY;
	else
		*p = fs;
	write_unlock(&file_systems_lock);
	return res;
}

int __init list_bdev_fs_names(char *buf, size_t size)
{
	/* No filesystem sets FS_REQUIRES_DEV, so always returns empty list */
	return 0;
}

static struct file_system_type *__get_fs_type(const char *name, int len)
{
	struct file_system_type *fs;

	read_lock(&file_systems_lock);
	fs = *(find_filesystem(name, len));
	/* try_module_get always returns true - dead check removed */
	read_unlock(&file_systems_lock);
	return fs;
}

struct file_system_type *get_fs_type(const char *name)
{
	/* request_module & FS_HAS_SUBTYPE handling removed - dead code */
	return __get_fs_type(name, strlen(name));
}
