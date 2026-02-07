
/* All tick-common functions and globals removed - the entire file was dead.
   tick_check_new_device was only called from clockevents_register_device
   (now removed), making tick_setup_periodic also dead.
   Removed:
   - tick_setup_periodic (~27 LOC)
   - tick_check_new_device (~33 LOC)
   - tick_cpu_device, tick_next_period, tick_do_timer_cpu globals
*/
