
/* All clockevents functions removed - clockevents_register_device was
   never called, making the entire chain dead:
   - clockevents_switch_state (~53 LOC)
   - clockevents_program_min_delta (~41 LOC)
   - clockevents_program_event (~35 LOC)
   - clockevents_register_device (~32 LOC)
   - clockevents_exchange_device (~17 LOC)
   Plus globals: clockevent_devices, clockevents_released,
   clockevents_lock, clockevents_mutex
*/
