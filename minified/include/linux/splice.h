#ifndef SPLICE_H
#define SPLICE_H

#include <linux/pipe_fs_i.h>

/* SPLICE_F_* macros, struct splice_desc removed - unused */
/* splice_actor typedef, splice_from_pipe removed - never called */

extern const struct pipe_buf_operations page_cache_pipe_buf_ops;
extern const struct pipe_buf_operations default_pipe_buf_ops;
#endif
