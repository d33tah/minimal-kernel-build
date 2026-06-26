 
 

#ifndef _ORC_TYPES_H
#define _ORC_TYPES_H

#include <linux/types.h>
#include <linux/compiler.h>

 
/* ORC_REG_UNDEFINED/PREV_SP/R13/BP_INDIRECT/MAX removed - 0-ref */
#define ORC_REG_DX			2
#define ORC_REG_DI			3
#define ORC_REG_BP			4
#define ORC_REG_SP			5
#define ORC_REG_R10			6
#define ORC_REG_SP_INDIRECT		9

#endif
