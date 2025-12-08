/* --- 2025-12-08 10:34 --- Reduced enums to only used values */
#ifndef _UAPI_LINUX_PERF_EVENT_H
#define _UAPI_LINUX_PERF_EVENT_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/* Only keep used enum values */
#define PERF_TYPE_BREAKPOINT 5

/* SW event IDs used in mm code */
#define PERF_COUNT_SW_PAGE_FAULTS 2
#define PERF_COUNT_SW_PAGE_FAULTS_MIN 5
#define PERF_COUNT_SW_PAGE_FAULTS_MAJ 6

/* PERF_COUNT_SW_MAX used for array size */
#define PERF_COUNT_SW_MAX 12

struct perf_event_attr {
	__u32 type;
	__u32 size;
	__u64 config;

	union {
		__u64 sample_period;
		__u64 sample_freq;
	};

	__u64 sample_type;
	__u64 read_format;

	__u64 disabled       :  1,
	      inherit        :  1,
	      pinned         :  1,
	      exclusive      :  1,
	      exclude_user   :  1,
	      exclude_kernel :  1,
	      exclude_hv     :  1,
	      exclude_idle   :  1,
	      mmap           :  1,
	      comm           :  1,
	      freq           :  1,
	      inherit_stat   :  1,
	      enable_on_exec :  1,
	      task           :  1,
	      watermark      :  1,
	      precise_ip     :  2,
	      mmap_data      :  1,
	      sample_id_all  :  1,
	      exclude_host   :  1,
	      exclude_guest  :  1,
	      exclude_callchain_kernel : 1,
	      exclude_callchain_user   : 1,
	      mmap2          :  1,
	      comm_exec      :  1,
	      use_clockid    :  1,
	      context_switch :  1,
	      write_backward :  1,
	      namespaces     :  1,
	      ksymbol        :  1,
	      bpf_event      :  1,
	      aux_output     :  1,
	      cgroup         :  1,
	      text_poke      :  1,
	      build_id       :  1,
	      inherit_thread :  1,
	      remove_on_exec :  1,
	      sigtrap        :  1,
	      __reserved_1   : 26;

	union {
		__u32 wakeup_events;
		__u32 wakeup_watermark;
	};

	__u32 bp_type;
	union {
		__u64 bp_addr;
		__u64 kprobe_func;
		__u64 uprobe_path;
		__u64 config1;
	};
	union {
		__u64 bp_len;
		__u64 kprobe_addr;
		__u64 probe_offset;
		__u64 config2;
	};
	__u64 branch_sample_type;
	__u64 sample_regs_user;
	__u32 sample_stack_user;
	__s32 clockid;
	__u64 sample_regs_intr;
	__u32 aux_watermark;
	__u16 sample_max_stack;
	__u16 __reserved_2;
	__u32 aux_sample_size;
	__u32 __reserved_3;
	__u64 sig_data;
};

/* PERF_ATTR_SIZE_* removed - unused */

#endif  
