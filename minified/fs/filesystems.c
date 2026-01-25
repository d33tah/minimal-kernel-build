
#include <linux/syscalls.h>
#include <linux/fs.h>
/* proc_fs.h removed - empty header */
/* seq_file.h, kmod.h removed - headers empty */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

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

	/* fs_validate_description() always returns true - condition removed */

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

/* list_bdev_fs_names moved to fs.h as static inline */

/* __get_fs_type inlined into get_fs_type (~6 LOC) */
struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;
	read_lock(&file_systems_lock);
	fs = *(find_filesystem(name, strlen(name)));
	read_unlock(&file_systems_lock);
	return fs;
}
