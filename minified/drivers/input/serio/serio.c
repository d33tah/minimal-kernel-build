/* Stubbed serio - CONFIG_INPUT disabled, exports return -ENODEV */
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

/* Stub all 11 exported symbols */
void serio_rescan(struct serio *serio) { }
EXPORT_SYMBOL(serio_rescan);

void serio_reconnect(struct serio *serio) { }
EXPORT_SYMBOL(serio_reconnect);

void __serio_register_port(struct serio *serio, struct module *owner) { }
EXPORT_SYMBOL(__serio_register_port);

void serio_unregister_port(struct serio *serio) { }
EXPORT_SYMBOL(serio_unregister_port);

void serio_unregister_child_port(struct serio *serio) { }
EXPORT_SYMBOL(serio_unregister_child_port);

int __serio_register_driver(struct serio_driver *drv, struct module *owner, const char *mod_name) { return -ENODEV; }
EXPORT_SYMBOL(__serio_register_driver);

void serio_unregister_driver(struct serio_driver *drv) { }
EXPORT_SYMBOL(serio_unregister_driver);

int serio_open(struct serio *serio, struct serio_driver *drv) { return -ENODEV; }
EXPORT_SYMBOL(serio_open);

void serio_close(struct serio *serio) { }
EXPORT_SYMBOL(serio_close);

irqreturn_t serio_interrupt(struct serio *serio, unsigned char data, unsigned int dfl) { return IRQ_NONE; }
EXPORT_SYMBOL(serio_interrupt);

struct bus_type serio_bus = { .name = "serio" };
EXPORT_SYMBOL(serio_bus);
