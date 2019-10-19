#define EXTOOLS_SUCCESS	"gucci"
#define EXTOOLS_FAILED	"pain"
#define GLOBAL_PROC		"magic BS"

/*
	Core - Provides necessary functionality for other modules.

	Must be initialized first before initializing any modules!

*/

/proc/extools_initialize()
	return call("byond-extools.dll", "core_initialize")() == EXTOOLS_SUCCESS

/*
	TFFI - Threaded FFI

	All DLL calls are automatically threaded off.
	Black magic is used to suspend (sleep) the currently executing proc, allowing non-blocking FFI.

	You may call a DLL function and sleep until it returns, pass a callback to be called with the result,
	or call resolve() on the /datum/promise to receive the return value at any time.

	Example:


	var/x = call_wait("sample.dll", "do_work", "arg1", "arg2", "arg3")

	 - Calls the do_work function from sample.dll with 3 arguments. The proc sleeps until do_work returns.



	var/datum/promise/P = call_async("sample.dll", "do_work", "arg1")
	... do something else ...
	var/result = P.resolve()

	 - Calls do_work with 1 argument. Returns a promise object. Runs some other code before calling P.resolve() to obtain the result.



	/proc/print_result(result)
		world << result

	call_cb("sample.dll", "do_work", /proc/print_result, "arg1", "arg2")

	 - Calls do_work with 2 arguments. The callback is invoked with the result as the single argument. Execution resumes immediately.


*/

/proc/tffi_initialize()
	call("byond-extools.dll", "tffi_initialize")() == EXTOOLS_SUCCESS

var/fallback_alerted = FALSE
var/next_promise_id = 0

/datum/promise
	var/completed = FALSE
	var/result = ""
	var/callback_obj = GLOBAL_PROC
	var/callback_proc = null
	var/__id = 0

/datum/promise/New()
	__id = next_promise_id++ //please don't create more than 10^38 promises in a single tick

//This proc's bytecode is overwritten to allow suspending and resuming on demand.
//None of the code here should run.
/datum/promise/proc/__internal_resolve(ref, id)
	if(!fallback_alerted && world.system_type != UNIX) // the rewriting is currently broken on Linux.
		world << "<b>TFFI: __internal_resolve has not been rewritten, the TFFI DLL was not loaded correctly.</b>"
		world.log << "<b>TFFI: __internal_resolve has not been rewritten, the TFFI DLL was not loaded correctly.</b>"
		fallback_alerted = TRUE
	while(!completed)
		sleep(1)
		//It might be better to just fail and notify the user that something went wrong.

/datum/promise/proc/__resolve_callback()
	__internal_resolve("\ref[src]", __id)
	if(callback_obj == GLOBAL_PROC)
		call(callback_proc)(result)
	else
		call(callback_obj, callback_proc)(result)

/datum/promise/proc/resolve()
	__internal_resolve("\ref[src]", __id)
	return result

/proc/call_async()
	var/list/arguments = args.Copy()
	var/datum/promise/P = new
	arguments.Insert(1, "\ref[P]")
	call("byond-extools.dll", "call_async")(arglist(arguments))
	return P

/proc/call_cb()
	var/list/arguments = args.Copy()
	var/callback = arguments[3]
	arguments.Cut(3, 4)
	var/datum/promise/P = new
	P.callback_obj = callback
	arguments.Insert(1, "\ref[P]")
	call("byond-extools.dll", "call_async")(arglist(arguments))
	spawn(0)
		P.__resolve_callback()

/proc/call_wait()
	return call_async(arglist(args)).resolve()