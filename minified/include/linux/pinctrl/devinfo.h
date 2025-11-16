 
 

#ifndef PINCTRL_DEVINFO_H
#define PINCTRL_DEVINFO_H


struct device;

 

static inline int pinctrl_bind_pins(struct device *dev)
{
	return 0;
}

static inline int pinctrl_init_done(struct device *dev)
{
	return 0;
}

#endif  
