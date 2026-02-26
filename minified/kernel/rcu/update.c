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

void wakeme_after_rcu(struct rcu_head *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}

/* Simplified: always called with n=1, checktiny=false */
void __wait_rcu_gp(bool checktiny, int n, call_rcu_func_t *crcu_array,
		   struct rcu_synchronize *rs_array)
{
	init_completion(&rs_array[0].completion);
	(crcu_array[0])(&rs_array[0].head, wakeme_after_rcu);
	wait_for_completion(&rs_array[0].completion);
}
