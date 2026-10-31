#include <linux/fs.h>
#include <linux/cdev.h>

/*
 * Character devices: drivers add a cdev for a range of device numbers;
 * chrdev_open, the open method of every character-special inode, looks the
 * inode's device number up here and hands the file to the driver's
 * file_operations. Devices are never removed.
 */
static LIST_HEAD(cdev_map);
static DEFINE_MUTEX(cdev_lock);

static struct cdev *cdev_lookup(dev_t dev)
{
	struct cdev *p;

	list_for_each_entry(p, &cdev_map, list)
		if (dev >= p->dev && dev - p->dev < p->count)
			return p;
	return NULL;
}

static int chrdev_open(struct inode *inode, struct file *filp)
{
	struct cdev *p;

	mutex_lock(&cdev_lock);
	p = inode->i_cdev;
	if (!p)
		p = inode->i_cdev = cdev_lookup(inode->i_rdev);
	mutex_unlock(&cdev_lock);
	if (!p)
		return -ENXIO;

	replace_fops(filp, p->ops);
	if (filp->f_op->open)
		return filp->f_op->open(inode, filp);
	return 0;
}

const struct file_operations def_chr_fops = {
	.open = chrdev_open,
};

void cdev_init(struct cdev *cdev, const struct file_operations *fops)
{
	memset(cdev, 0, sizeof *cdev);
	INIT_LIST_HEAD(&cdev->list);
	cdev->ops = fops;
}

int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	struct cdev *q;
	int ret = 0;

	if (!count)
		return -EINVAL;

	mutex_lock(&cdev_lock);
	list_for_each_entry(q, &cdev_map, list)
		if (dev < q->dev + q->count && q->dev < dev + count)
			ret = -EBUSY;
	if (!ret) {
		p->dev = dev;
		p->count = count;
		list_add(&p->list, &cdev_map);
	}
	mutex_unlock(&cdev_lock);
	return ret;
}
