
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/module.h>

struct device {
	struct kobject kobj;
};

#endif
