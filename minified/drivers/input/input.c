/* Stubbed input core - CONFIG_INPUT disabled, exports return -ENODEV */
#include <linux/module.h>
#include <linux/input.h>
#include <linux/errno.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

/* Stub all 33 exported symbols */
void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value) { }
EXPORT_SYMBOL(input_event);

void input_inject_event(struct input_handle *handle, unsigned int type, unsigned int code, int value) { }
EXPORT_SYMBOL(input_inject_event);

void input_alloc_absinfo(struct input_dev *dev) { }
EXPORT_SYMBOL(input_alloc_absinfo);

void input_set_abs_params(struct input_dev *dev, unsigned int axis, int min, int max, int fuzz, int flat) { }
EXPORT_SYMBOL(input_set_abs_params);

void input_copy_abs(struct input_dev *dst, unsigned int dst_axis, const struct input_dev *src, unsigned int src_axis) { }
EXPORT_SYMBOL(input_copy_abs);

int input_grab_device(struct input_handle *handle) { return -ENODEV; }
EXPORT_SYMBOL(input_grab_device);

void input_release_device(struct input_handle *handle) { }
EXPORT_SYMBOL(input_release_device);

int input_open_device(struct input_handle *handle) { return -ENODEV; }
EXPORT_SYMBOL(input_open_device);

int input_flush_device(struct input_handle *handle, struct file *file) { return 0; }
EXPORT_SYMBOL(input_flush_device);

void input_close_device(struct input_handle *handle) { }
EXPORT_SYMBOL(input_close_device);

int input_scancode_to_scalar(const struct input_keymap_entry *ke, unsigned int *scancode) { return -EINVAL; }
EXPORT_SYMBOL(input_scancode_to_scalar);

int input_get_keycode(struct input_dev *dev, struct input_keymap_entry *ke) { return -ENODEV; }
EXPORT_SYMBOL(input_get_keycode);

int input_set_keycode(struct input_dev *dev, const struct input_keymap_entry *ke) { return -ENODEV; }
EXPORT_SYMBOL(input_set_keycode);

bool input_match_device_id(const struct input_dev *dev, const struct input_device_id *id) { return false; }
EXPORT_SYMBOL(input_match_device_id);

void input_reset_device(struct input_dev *dev) { }
EXPORT_SYMBOL(input_reset_device);

struct class input_class = { .name = "input" };
EXPORT_SYMBOL_GPL(input_class);

struct input_dev *input_allocate_device(void) { return NULL; }
EXPORT_SYMBOL(input_allocate_device);

struct input_dev *devm_input_allocate_device(struct device *dev) { return NULL; }
EXPORT_SYMBOL(devm_input_allocate_device);

void input_free_device(struct input_dev *dev) { }
EXPORT_SYMBOL(input_free_device);

void input_set_timestamp(struct input_dev *dev, ktime_t timestamp) { }
EXPORT_SYMBOL(input_set_timestamp);

ktime_t *input_get_timestamp(struct input_dev *dev) { return NULL; }
EXPORT_SYMBOL(input_get_timestamp);

void input_set_capability(struct input_dev *dev, unsigned int type, unsigned int code) { }
EXPORT_SYMBOL(input_set_capability);

void input_enable_softrepeat(struct input_dev *dev, int delay, int period) { }
EXPORT_SYMBOL(input_enable_softrepeat);

bool input_device_enabled(struct input_dev *dev) { return false; }
EXPORT_SYMBOL_GPL(input_device_enabled);

int input_register_device(struct input_dev *dev) { return -ENODEV; }
EXPORT_SYMBOL(input_register_device);

void input_unregister_device(struct input_dev *dev) { }
EXPORT_SYMBOL(input_unregister_device);

int input_register_handler(struct input_handler *handler) { return -ENODEV; }
EXPORT_SYMBOL(input_register_handler);

void input_unregister_handler(struct input_handler *handler) { }
EXPORT_SYMBOL(input_unregister_handler);

int input_handler_for_each_handle(struct input_handler *handler, void *data, int (*fn)(struct input_handle *, void *)) { return -ENODEV; }
EXPORT_SYMBOL(input_handler_for_each_handle);

int input_register_handle(struct input_handle *handle) { return -ENODEV; }
EXPORT_SYMBOL(input_register_handle);

void input_unregister_handle(struct input_handle *handle) { }
EXPORT_SYMBOL(input_unregister_handle);

int input_get_new_minor(int legacy_base, unsigned int legacy_num, bool allow_dynamic) { return -ENODEV; }
EXPORT_SYMBOL(input_get_new_minor);

void input_free_minor(unsigned int minor) { }
EXPORT_SYMBOL(input_free_minor);
