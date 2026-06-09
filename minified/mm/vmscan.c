
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

void unregister_shrinker(struct shrinker *shrinker)
{

}

int prealloc_shrinker(struct shrinker *shrinker)
{
	return 0;   
}

void free_prealloced_shrinker(struct shrinker *shrinker)
{
	 
}

void register_shrinker_prepared(struct shrinker *shrinker)
{
	 
}


long remove_mapping(struct address_space *mapping, struct folio *folio)
{
	return 0;
}

