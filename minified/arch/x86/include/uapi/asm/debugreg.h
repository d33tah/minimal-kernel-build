
#ifndef _UAPI_ASM_X86_DEBUGREG_H
#define _UAPI_ASM_X86_DEBUGREG_H





#define DR6_RESERVED	(0xFFFF0FF0)

#define DR_TRAP0	(0x1)
#define DR_TRAP1	(0x2)
#define DR_TRAP2	(0x4)
#define DR_TRAP3	(0x8)
#define DR_TRAP_BITS	(DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)

#define DR_STEP		(0x4000)






#endif
