/world/New()
	world.log << "byond-extools testing initialized!"
	if(!extools_initialize())
		world.log << "Core failed to initialize!"
		shutdown()
		return
	if(!tffi_initialize())
		world.log << "TFFI failed to initialize!"
		shutdown()
		return
	if(!profiling_initialize())
		world.log << "Profiler failed to initialize!"
		shutdown()
		return
	if(call(EXTOOLS, "proxy_initialize")() != EXTOOLS_SUCCESS)
		world.log << "Proxy failed to initialize!"
		shutdown()
		return
	var/datum/promise/P = new
	P.callback_context = GLOBAL_PROC
	P.callback_proc = .proc/finish
	if(call(EXTOOLS, "unit_tests")("\ref[P]") != EXTOOLS_SUCCESS)
		world.log << "Unit test thread failed to initialize!"
		shutdown()
		return
	spawn(0)
		P.__resolve_callback()
	spawn(50)
		testing()

/proc/finish(results)
	world.log << "Unit tests done!"
	world.log << "Results: [results]"
	shutdown()

/proc/testing()
	world.log << "Hi! I'm a testing proc!"
	var/a = 1 + 5 * 3 / 2 ^ 1
	world.log << "Breakpoint goes here."