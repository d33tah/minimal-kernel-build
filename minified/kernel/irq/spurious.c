/* Stub: Spurious IRQ handling - simplified for minimal kernel */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

#include "internals.h"

bool noirqdebug __read_mostly;

