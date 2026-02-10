
#ifndef _TRACE_FPU_H
#define _TRACE_FPU_H

/* Tracing disabled - all trace_x86_fpu_* are empty inline stubs */

struct fpu;

static inline void trace_x86_fpu_before_save(struct fpu *fpu) { }
static inline void trace_x86_fpu_after_save(struct fpu *fpu) { }
static inline void trace_x86_fpu_before_restore(struct fpu *fpu) { }
static inline void trace_x86_fpu_after_restore(struct fpu *fpu) { }
static inline void trace_x86_fpu_regs_activated(struct fpu *fpu) { }
static inline void trace_x86_fpu_regs_deactivated(struct fpu *fpu) { }
static inline void trace_x86_fpu_init_state(struct fpu *fpu) { }
static inline void trace_x86_fpu_dropped(struct fpu *fpu) { }
static inline void trace_x86_fpu_copy_src(struct fpu *fpu) { }
static inline void trace_x86_fpu_copy_dst(struct fpu *fpu) { }
static inline void trace_x86_fpu_xstate_check_failed(struct fpu *fpu) { }

#endif
