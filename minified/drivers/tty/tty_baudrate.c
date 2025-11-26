 
 

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/export.h>
#include "tty.h"


 
static const speed_t baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400,
	4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
#ifdef __sparc__
	76800, 153600, 307200, 614400, 921600, 500000, 576000,
	1000000, 1152000, 1500000, 2000000
#else
	500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
#endif
};

static const tcflag_t baud_bits[] = {
	B0, B50, B75, B110, B134, B150, B200, B300, B600, B1200, B1800, B2400,
	B4800, B9600, B19200, B38400, B57600, B115200, B230400, B460800,
#ifdef __sparc__
	B76800, B153600, B307200, B614400, B921600, B500000, B576000,
	B1000000, B1152000, B1500000, B2000000
#else
	B500000, B576000, B921600, B1000000, B1152000, B1500000, B2000000,
	B2500000, B3000000, B3500000, B4000000
#endif
};

static int n_baud_table = ARRAY_SIZE(baud_table);

 

speed_t tty_termios_baud_rate(struct ktermios *termios)
{
	unsigned int cbaud;

	cbaud = termios->c_cflag & CBAUD;

	 
	if (cbaud == BOTHER)
		return termios->c_ospeed;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~CBAUDEX;
		else
			cbaud += 15;
	}
	return cbaud >= n_baud_table ? 0 : baud_table[cbaud];
}

 

speed_t tty_termios_input_baud_rate(struct ktermios *termios)
{
	unsigned int cbaud = (termios->c_cflag >> IBSHIFT) & CBAUD;

	if (cbaud == B0)
		return tty_termios_baud_rate(termios);

	 
	if (cbaud == BOTHER)
		return termios->c_ispeed;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~(CBAUDEX << IBSHIFT);
		else
			cbaud += 15;
	}
	return cbaud >= n_baud_table ? 0 : baud_table[cbaud];
}

/* Stub: tty_termios_encode_baud_rate not used externally */
void tty_termios_encode_baud_rate(struct ktermios *termios,
				  speed_t ibaud, speed_t obaud) { }

/* Stub: tty_encode_baud_rate not used externally */
void tty_encode_baud_rate(struct tty_struct *tty, speed_t ibaud, speed_t obaud) { }
