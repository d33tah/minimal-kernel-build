#ifndef IOPRIO_H
#define IOPRIO_H

/* IOPRIO_DEFAULT = (IOPRIO_CLASS_BE << 13) | IOPRIO_NORM = (2 << 13) | 4 = 16388 */
#define IOPRIO_DEFAULT	16388
#define get_current_ioprio() IOPRIO_DEFAULT

#endif
