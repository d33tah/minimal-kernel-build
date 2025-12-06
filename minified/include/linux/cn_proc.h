#ifndef CN_PROC_H
#define CN_PROC_H

#define PROC_EVENT_UID  0x00000004
#define PROC_EVENT_GID  0x00000040

static inline void proc_fork_connector(struct task_struct *task)
{}

static inline void proc_exec_connector(struct task_struct *task)
{}

static inline void proc_id_connector(struct task_struct *task,
				     int which_id)
{}

static inline void proc_sid_connector(struct task_struct *task)
{}

static inline void proc_comm_connector(struct task_struct *task)
{}

static inline void proc_ptrace_connector(struct task_struct *task,
					 int ptrace_id)
{}

static inline void proc_coredump_connector(struct task_struct *task)
{}

static inline void proc_exit_connector(struct task_struct *task)
{}
#endif	 
