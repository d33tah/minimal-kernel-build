

/*
 * CPU-time accounting was driven entirely by account_process_tick(), which
 * only updated the per-cpu kernel_cpustat.cpustat[] counters. Nothing in this
 * minimal build ever reads those counters (no /proc/stat), so the whole tick
 * accounting path was write-only and has been removed.
 */
