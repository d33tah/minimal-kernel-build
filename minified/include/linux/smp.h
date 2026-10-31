#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

#define raw_smp_processor_id() 0

#define __smp_processor_id(x) raw_smp_processor_id(x)

#define smp_processor_id() __smp_processor_id()

#endif
