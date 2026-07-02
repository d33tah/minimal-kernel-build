 
 

#ifndef _TTY_INTERNAL_H
#define _TTY_INTERNAL_H

#define tty_msg(fn, tty, f, ...) \
	fn("%s %s: " f, tty_driver_name(tty), tty_name(tty), ##__VA_ARGS__)

#define tty_warn(tty, f, ...)	tty_msg(pr_warn, tty, f, ##__VA_ARGS__)
#define tty_err(tty, f, ...)	tty_msg(pr_err, tty, f, ##__VA_ARGS__)

#define tty_info_ratelimited(tty, f, ...) \
		tty_msg(pr_info_ratelimited, tty, f, ##__VA_ARGS__)

 
enum {
	TTY_LOCK_NORMAL = 0,
	TTY_LOCK_SLAVE,
};

 
/* TTY_THROTTLE_SAFE, TTY_UNTHROTTLE_SAFE removed - never referenced */

/* __tty_set_flow_change, tty_set_flow_change removed - unused */

int tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout);
void tty_ldisc_unlock(struct tty_struct *tty);

/* __stop_tty, __start_tty removed - unused */
void tty_buffer_init(struct tty_port *port);
/* tty_buffer_flush_work removed - unused */
speed_t tty_termios_input_baud_rate(struct ktermios *termios);
int tty_ldisc_reinit(struct tty_struct *tty, int disc);
struct tty_struct *alloc_tty_struct(struct tty_driver *driver, int idx);
void tty_free_file(struct file *file);
int tty_release(struct inode *inode, struct file *filp);


int tty_ldisc_setup(struct tty_struct *tty, struct tty_struct *o_tty);
int __must_check tty_ldisc_init(struct tty_struct *tty);

/* tty_audit_add_data, tty_audit_tiocsti removed - unused */

ssize_t redirected_tty_write(struct kiocb *, struct iov_iter *);

/* tty_insert_flip_string_and_push_buffer removed - unused */

#endif
