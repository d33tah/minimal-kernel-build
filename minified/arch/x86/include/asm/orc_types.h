 
 

#ifndef _ORC_TYPES_H
#define _ORC_TYPES_H

#include <linux/types.h>
#include <linux/compiler.h>

/* ORC_REG_UNDEFINED, ORC_REG_PREV_SP removed - unused */
#define ORC_REG_DX			2
#define ORC_REG_DI			3
#define ORC_REG_BP			4
#define ORC_REG_SP			5
#define ORC_REG_R10			6
/* ORC_REG_R13, ORC_REG_BP_INDIRECT removed - unused */
#define ORC_REG_SP_INDIRECT		9
/* ORC_REG_MAX removed - unused */

#ifndef __ASSEMBLY__
#include <asm/byteorder.h>
#endif

#endif  
