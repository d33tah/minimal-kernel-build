#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

#define MMAP_LOCK_INITIALIZER(name) \
	.mmap_lock = __RWSEM_INITIALIZER((name).mmap_lock),

static inline void mmap_init_lock(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_lock);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	int ret;

	ret = down_write_killable(&mm->mmap_lock);
	return ret;
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	up_write(&mm->mmap_lock);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_lock);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	int ret;

	ret = down_read_killable(&mm->mmap_lock);
	return ret;
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	bool ret;

	ret = down_read_trylock(&mm->mmap_lock) != 0;
	return ret;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_lock);
}

#endif
