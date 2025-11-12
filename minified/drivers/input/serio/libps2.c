/* Stubbed libps2 - CONFIG_INPUT disabled, exports return -ENODEV */
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/errno.h>

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("PS/2 driver library");
MODULE_LICENSE("GPL");

/* Stub all 12 exported symbols */
int ps2_sendbyte(struct ps2dev *ps2dev, u8 byte, unsigned int timeout) { return -ENODEV; }
EXPORT_SYMBOL(ps2_sendbyte);

void ps2_begin_command(struct ps2dev *ps2dev) { }
EXPORT_SYMBOL(ps2_begin_command);

void ps2_end_command(struct ps2dev *ps2dev) { }
EXPORT_SYMBOL(ps2_end_command);

void ps2_drain(struct ps2dev *ps2dev, size_t maxbytes, unsigned int timeout) { }
EXPORT_SYMBOL(ps2_drain);

bool ps2_is_keyboard_id(u8 id_byte) { return false; }
EXPORT_SYMBOL(ps2_is_keyboard_id);

int __ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command) { return -ENODEV; }
EXPORT_SYMBOL(__ps2_command);

int ps2_command(struct ps2dev *ps2dev, u8 *param, unsigned int command) { return -ENODEV; }
EXPORT_SYMBOL(ps2_command);

int ps2_sliced_command(struct ps2dev *ps2dev, u8 command) { return -ENODEV; }
EXPORT_SYMBOL(ps2_sliced_command);

void ps2_init(struct ps2dev *ps2dev, struct serio *serio) { }
EXPORT_SYMBOL(ps2_init);

bool ps2_handle_ack(struct ps2dev *ps2dev, u8 data) { return false; }
EXPORT_SYMBOL(ps2_handle_ack);

bool ps2_handle_response(struct ps2dev *ps2dev, u8 data) { return false; }
EXPORT_SYMBOL(ps2_handle_response);

void ps2_cmd_aborted(struct ps2dev *ps2dev) { }
EXPORT_SYMBOL(ps2_cmd_aborted);
