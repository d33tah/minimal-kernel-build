#include <linux/rcupdate.h>
#include <linux/completion.h>

/* inlined from linux/rcupdate_wait.h */
struct rcu_synchronize {
	struct rcu_head head;
	struct completion completion;
};
void wakeme_after_rcu(struct rcu_head *head);

void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array);

#define _wait_rcu_gp(checktiny, ...)                                         \
	do {                                                                 \
		call_rcu_func_t __crcu_array[] = { __VA_ARGS__ };            \
		struct rcu_synchronize __rs_array[ARRAY_SIZE(__crcu_array)]; \
		__wait_rcu_gp(checktiny, ARRAY_SIZE(__crcu_array),           \
			      __crcu_array, __rs_array);                     \
	} while (0)

#define wait_rcu_gp(...) _wait_rcu_gp(false, __VA_ARGS__)
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname) /* kprobes disabled */
#endif

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "rcupdate."

void wakeme_after_rcu(struct rcu_head *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}

void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array)
{
	int i;
	int j;

	for (i = 0; i < n; i++) {
		if (checktiny && (crcu_array[i] == call_rcu))
			continue;
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i) {
			init_completion(&rs_array[i].completion);
			(crcu_array[i])(&rs_array[i].head, wakeme_after_rcu);
		}
	}

	for (i = 0; i < n; i++) {
		if (checktiny && (crcu_array[i] == call_rcu))
			continue;
		for (j = 0; j < i; j++)
			if (crcu_array[j] == crcu_array[i])
				break;
		if (j == i) {
			wait_for_completion(&rs_array[i].completion);
		}
	}
}
