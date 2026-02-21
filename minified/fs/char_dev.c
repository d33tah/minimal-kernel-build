#include <linux/fs.h>

typedef struct kobject *kobj_probe_t(dev_t, int *, void *);
struct kobj_map;
int kobj_map(struct kobj_map *, dev_t, unsigned long, struct module *,
	     kobj_probe_t *, int (*)(dev_t, void *), void *);
struct kobject *kobj_lookup(struct kobj_map *, dev_t, int *);
struct kobj_map *kobj_map_init(kobj_probe_t *, struct mutex *);
/* end kobj_map.h */
/* cdev.h inlined */
#include <linux/kdev_t.h>
#include <linux/device.h>
struct file_operations;
struct inode;
struct module;
struct cdev {
	struct kobject kobj;
	struct module *owner;
	const struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
} __randomize_layout;
void cdev_put(struct cdev *p);
void cd_forget(struct inode *);

static struct kobj_map *cdev_map;

static DEFINE_MUTEX(chrdevs_lock);

static DEFINE_SPINLOCK(cdev_lock);

static struct kobject *cdev_get(struct cdev *p)
{
	struct module *owner = p->owner;
	struct kobject *kobj;

	kobj = kobject_get_unless_zero(&p->kobj);
	if (!kobj)
		module_put(owner);
	return kobj;
}

void cdev_put(struct cdev *p)
{
	if (p) {
		struct module *owner = p->owner;
		kobject_put(&p->kobj);
		module_put(owner);
	}
}

static int chrdev_open(struct inode *inode, struct file *filp)
{
	const struct file_operations *fops;
	struct cdev *p;
	struct cdev *new = NULL;
	int ret = 0;

	spin_lock(&cdev_lock);
	p = inode->i_cdev;
	if (!p) {
		struct kobject *kobj;
		int idx;
		spin_unlock(&cdev_lock);
		kobj = kobj_lookup(cdev_map, inode->i_rdev, &idx);
		if (!kobj)
			return -ENXIO;
		new = container_of(kobj, struct cdev, kobj);
		spin_lock(&cdev_lock);

		p = inode->i_cdev;
		if (!p) {
			inode->i_cdev = p = new;
			list_add(&inode->i_devices, &p->list);
			new = NULL;
		} else if (!cdev_get(p))
			ret = -ENXIO;
	} else if (!cdev_get(p))
		ret = -ENXIO;
	spin_unlock(&cdev_lock);
	cdev_put(new);
	if (ret)
		return ret;

	ret = -ENXIO;
	fops = fops_get(p->ops);
	if (!fops)
		goto out_cdev_put;

	replace_fops(filp, fops);
	if (filp->f_op->open) {
		ret = filp->f_op->open(inode, filp);
		if (ret)
			goto out_cdev_put;
	}

	return 0;

out_cdev_put:
	cdev_put(p);
	return ret;
}

void cd_forget(struct inode *inode)
{
	spin_lock(&cdev_lock);
	list_del_init(&inode->i_devices);
	inode->i_cdev = NULL;
	inode->i_mapping = &inode->i_data;
	spin_unlock(&cdev_lock);
}

const struct file_operations def_chr_fops = {
	.open = chrdev_open,
};

static struct kobject *base_probe(dev_t dev, int *part, void *data)
{
	return NULL;
}

void __init chrdev_init(void)
{
	cdev_map = kobj_map_init(base_probe, &chrdevs_lock);
}
