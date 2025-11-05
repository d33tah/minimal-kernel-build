SECTIONS {
	.initcall0.init : {
		*(.initcall0.init..kmod_min_addr__182_46_init_mmap_min_addr) ;
	}
	.initcall1.init : {
		*(.initcall1.init..kmod_reboot__232_509_reboot_init) ;
		*(.initcall1.init..kmod_ksysfs__189_116_ksysfs_init) ;
		*(.initcall1.init..kmod_update__222_84_rcu_set_runtime_mode) ;
		*(.initcall1.init..kmod_jiffies__150_69_init_jiffies_clocksource) ;
		*(.initcall1.init..kmod_binfmt_elf__220_1353_init_elf_binfmt) ;
	}
	.initcall2.init : {
		*(.initcall2.init..kmod_backing_dev__244_148_bdi_class_init) ;
		*(.initcall2.init..kmod_mm_init__219_32_mm_sysfs_init) ;
		*(.initcall2.init..kmod_page_alloc__379_6569_init_per_zone_wmark_min) ;
		*(.initcall2.init..kmod_tty_io__209_3247_tty_class_init) ;
		*(.initcall2.init..kmod_vt__233_4018_vtconsole_class_init) ;
		*(.initcall2.init..kmod_core__308_575_devlink_class_init) ;
		*(.initcall2.init..kmod_swnode__159_1167_software_node_init) ;
	}
	.initcall3.init : {
		*(.initcall3.init..kmod_bts__213_625_bts_init) ;
		*(.initcall3.init..kmod_pt__231_1787_pt_init) ;
		*(.initcall3.init..kmod_bootflag__191_99_sbf_init) ;
		*(.initcall3.init..kmod_kdebugfs__182_32_arch_kdebugfs_init) ;
		*(.initcall3.init..kmod_i8253__91_65_init_pit_clocksource) ;
		*(.initcall3.init..kmod_intel_pconfig__9_82_intel_pconfig_init) ;
	}
	.initcall4.init : {
		*(.initcall4.init..kmod_core__232_6489_fixup_ht_bug) ;
		*(.initcall4.init..kmod_topology__158_55_topology_init) ;
		*(.initcall4.init..kmod_user__132_237_uid_cache_init) ;
		*(.initcall4.init..kmod_ucount__135_271_user_namespace_sysctl_init) ;
		*(.initcall4.init..kmod_oom_kill__250_662_oom_init) ;
		*(.initcall4.init..kmod_backing_dev__246_158_default_bdi_init) ;
		*(.initcall4.init..kmod_percpu__255_2714_percpu_enable_async) ;
		*(.initcall4.init..kmod_mmap__274_3440_init_user_reserve) ;
		*(.initcall4.init..kmod_mmap__278_3461_init_admin_reserve) ;
		*(.initcall4.init..kmod_mmap__280_3531_init_reserve_notifier) ;
		*(.initcall4.init..kmod_misc__199_260_misc_init) ;
		*(.initcall4.init..kmod_serio__162_1006_serio_init) ;
		*(.initcall4.init..kmod_input_core__250_2331_input_init) ;
	}
	.initcall5.init : {
		*(.initcall5.init..kmod_nmi__216_102_nmi_warning_debugfs) ;
		*(.initcall5.init..kmod_resource__208_1542_iomem_init_inode) ;
		*(.initcall5.init..kmod_clocksource__162_1014_clocksource_done_booting) ;
		*(.initcall5.init..kmod_pipe__245_1393_init_pipe_fs) ;
		*(.initcall5.init..kmod_anon_inodes__191_270_anon_inode_init) ;
		*(.initcall5.init..kmod_ramfs__218_299_init_ramfs_fs) ;
		*(.initcall5.init..kmod_mem__231_693_chr_dev_init) ;
	}
	.initcall6.init : {
		*(.initcall6.init..kmod_setup__241_1073_register_kernel_offset_dumper) ;
		*(.initcall6.init..kmod_i8259__168_434_i8259A_init_ops) ;
		*(.initcall6.init..kmod_tsc__193_1320_init_tsc_clocksource) ;
		*(.initcall6.init..kmod_rtc__205_184_add_rtc_cmos) ;
		*(.initcall6.init..kmod_i8237__87_76_i8237A_init_ops) ;
		*(.initcall6.init..kmod_umwait__225_238_umwait_init) ;
		*(.initcall6.init..kmod_timekeeping__208_1762_timekeeping_init_ops) ;
		*(.initcall6.init..kmod_alarmtimer__194_363_alarmtimer_init) ;
		*(.initcall6.init..kmod_core__487_12360_perf_event_sysfs_init) ;
		*(.initcall6.init..kmod_vmscan__297_4156_kswapd_init) ;
		*(.initcall6.init..kmod_workingset__224_627_workingset_init) ;
		*(.initcall6.init..kmod_fcntl__236_839_fcntl_init) ;
		*(.initcall6.init..kmod_fs_writeback__253_1391_start_dirtytime_writeback) ;
		*(.initcall6.init..kmod_libblake2s__97_45_blake2s_mod_init) ;
		*(.initcall6.init..kmod_n_null__199_63_n_null_init) ;
		*(.initcall6.init..kmod_topology__199_194_topology_sysfs_init) ;
		*(.initcall6.init..kmod_cacheinfo__157_510_cacheinfo_sysfs_init) ;
		*(.initcall6.init..kmod_i8042__251_1505_i8042_init) ;
		*(.initcall6.init..kmod_serport__207_286_serport_init) ;
		*(.initcall6.init..kmod_atkbd__209_1894_atkbd_init) ;
	}
	.initcall7.init : {
		*(.initcall7.init..kmod_tsc_sync__127_119_start_sync_check_timer) ;
		*(.initcall7.init..kmod_tlb__241_1237_create_tlb_single_page_flush_ceiling) ;
		*(.initcall7.init..kmod_build_utility__237_240_sched_clock_init_late) ;
		*(.initcall7.init..kmod_printk__216_1359_printk_late_init) ;
		*(.initcall7.init..kmod_early_ioremap__219_98_check_early_ioremap_leak) ;
		*(.initcall7.init..kmod_core__313_1109_sync_state_resume_initcall) ;
		*(.initcall7.init..kmod_dd__201_366_deferred_probe_initcall) ;
	}
	.con_initcall.init : {
		*(.con_initcall.init..kmod_vt__227_3472_con_init) ;
	}
	.initcallearly.init : {
		*(.initcallearly.init..kmod_core__233_2168_init_hw_perf_events) ;
		*(.initcallearly.init..kmod_init__185_154_init_real_mode) ;
		*(.initcallearly.init..kmod_aperfmperf__158_125_bp_init_aperfmperf) ;
		*(.initcallearly.init..kmod_softirq__209_634_spawn_ksoftirqd) ;
		*(.initcallearly.init..kmod_irq_work__177_267_irq_work_init_threads) ;
		*(.initcallearly.init..kmod_memory__238_166_init_zero_pfn) ;
	}
	.initcallrootfs.init : {
		*(.initcallrootfs.init..kmod_initramfs__212_651_populate_rootfs) ;
		*(.initcallrootfs.init..kmod_pci_dma__206_126_pci_iommu_init) ;
	}
}
