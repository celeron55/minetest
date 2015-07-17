// http://www.apache.org/licenses/LICENSE-2.0
// Copyright 2014 Perttu Ahola <celeron55@gmail.com>
#include "bdebug/debug.h"
#include "bdebug/cmem.h"
#include "jthread/jmutex.h"
#include "jthread/jmutexautolock.h"
#include "log.h"
#include <strfnd.h>
#include <stdexcept>
#include <iostream>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <execinfo.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#define MODULE "bdebug"

namespace bdebug {

typedef const char cc_;
static inline cc_* cs(const ss_ &s){
	return s.c_str();
}

// Execute a program and return whatever it writes to stdout
static ss_ exec_get_stdout(char *cmd)
{
	FILE* pipe = popen(cmd, "r");
	if(!pipe) return "ERROR";
	char buffer[50];
	ss_ result;
	try{
		while(!feof(pipe)){
			if(fgets(buffer, 50, pipe) != NULL)
				result += buffer; // Can cause std::bad_alloc
		}
		pclose(pipe);
		return result;
	}catch(std::bad_alloc){
		return ss_();
	}
}

// Execute a program and return whatever it writes to stdout, without trailing
// newline
static ss_ exec_get_stdout_without_newline(char *cmd)
{
	ss_ s = exec_get_stdout(cmd);
	if(!s.empty() && s[s.size()-1] == '\n')
		s = s.substr(0, s.size()-1);
	return s;
}

static void* get_library_executable_address(const ss_ &lib_path)
{
	char cmdbuf[500];
	snprintf(cmdbuf, sizeof cmdbuf,
			"cat /proc/%i/maps | grep %s | grep ' ..x. ' | cut -d- -f1",
			getpid(), cs(lib_path));
	ss_ address_s = exec_get_stdout_without_newline(cmdbuf);
	//log_v(MODULE, "address_s=\"%s\"", cs(address_s));
	if(address_s.empty())
		return (void*)0x0;
	void *address = (void*)strtoul(address_s.c_str(), NULL, 16);
	return address;
}

//#define __USE_GNU
#include <ucontext.h> // REG_EIP (or REG_RIP)

#if __WORDSIZE == 64 // REG_RIP replaces REG_EIP on x86_64
	#define REG_EIP REG_RIP
#endif

static JMutex backtrace_mutex;

static char backtrace_buffer[10000];
static size_t backtrace_buffer_len = 0;

static void bt_print_newline()
{
	backtrace_buffer_len += snprintf(
			backtrace_buffer + backtrace_buffer_len,
			sizeof backtrace_buffer - backtrace_buffer_len,
			"\n");
}

static void bt_print(const char *fmt, ...)
{
	va_list va_args;
	va_start(va_args, fmt);
	backtrace_buffer_len += vsnprintf(
			backtrace_buffer + backtrace_buffer_len,
			sizeof backtrace_buffer - backtrace_buffer_len,
			fmt, va_args);
	va_end(va_args);
	bt_print_newline();
}

enum BacktraceFilterAction {BFA_PASS, BFA_START_LATER, BFA_STOP};
typedef std::function<BacktraceFilterAction(int i, int &first_real_i,
		ss_ &cppfilt_symbol, ss_ &addr2line_output)> backtrace_filter_t;

auto bt_default_filter = [](int i, int &first_real_i, 
		ss_ &cppfilt_symbol, ss_ &addr2line_output)
{
	// Clean up the beginning of the backtrace (for whatever reason there
	// often seems to be two basic_string-related lines at the beginnong of
	// the backtrace)
	if(i <= 2 && i <= first_real_i &&
			addr2line_output.find("/basic_string.h") != ss_::npos){
		return BFA_START_LATER;
	}
	return BFA_PASS;
};

static void bt_print_backtrace(char **symbols, void* const *trace, int trace_size,
		int start_from_i = 1, backtrace_filter_t filter_f = backtrace_filter_t())
{
	if(!filter_f)
		filter_f = bt_default_filter;

	int first_real_i = start_from_i;
	for(int i = 1; i < trace_size; i++){
		char cmdbuf[500];
		// Parse symbol to get file name
		// Example: "../cache/rccpp_build/main.so(+0x14e2c) [0x7f7880d36e2c]"
		//log_v(MODULE, "symbol: %s", symbols[i]);
		Strfnd f(symbols[i]);
		ss_ file_path = f.next("(");
		//log_v(MODULE, "file_path: %s", cs(file_path));

		void *address = trace[i];

		bool is_shared_lib = (file_path.find(".so") != ss_::npos);
		//log_v(MODULE, "is_shared_lib=%s", is_shared_lib?"true":"false");
		if(is_shared_lib){
			void *base_addr = get_library_executable_address(file_path);
			//log_v(MODULE, "base_addr=%p", base_addr);
			address = (void*)((char*)trace[i] - (char*)base_addr);
			//log_v(MODULE, "address=%p", address);
		}

		snprintf(cmdbuf, sizeof cmdbuf, "echo '%s' | c++filt", symbols[i]);
		ss_ cppfilt_symbol = exec_get_stdout_without_newline(cmdbuf);

		ss_ addr2line_output;
		if(address != NULL){
			snprintf(cmdbuf, sizeof cmdbuf, "addr2line %p -e %s",
					address, cs(file_path));
			addr2line_output = exec_get_stdout_without_newline(cmdbuf);
		}

		auto r = filter_f(i, first_real_i, cppfilt_symbol,addr2line_output);
		if(r == BFA_PASS){
		} else if(r == BFA_START_LATER){
			first_real_i = i + 1;
		} else if(r == BFA_STOP){
			break;
		}

		if(addr2line_output.size() > 4){
			bt_print("    #%i  %s", i-first_real_i, cs(addr2line_output));
			//bt_print("    = %s", cs(cppfilt_symbol));
		} else {
			bt_print("    #%i  %s", i-first_real_i, cs(cppfilt_symbol));
		}
	}

	// Remove trailing newline
	if(backtrace_buffer[backtrace_buffer_len-1] == '\n'){
		backtrace_buffer[backtrace_buffer_len-1] = 0;
		backtrace_buffer_len--;
	}
}

static void log_backtrace(void* const *trace, int trace_size, const ss_ &title,
		bool use_lock = true)
{
	char **symbols = backtrace_symbols(trace, trace_size);

	if(use_lock){
		// Lock spinlock
		backtrace_mutex.Lock();
	}

	backtrace_buffer_len = 0;
	bt_print("\n  %s", cs(title));
	// The first stack frame points to this functiton
	bt_print_backtrace(symbols, trace, trace_size, 1);

	// Print to log
	backtrace_buffer[sizeof backtrace_buffer - 1] = 0;
	errorstream<<backtrace_buffer<<std::endl;

	if(use_lock){
		// Unlock spinlock
		backtrace_mutex.Unlock();
	}

	free(symbols);
}

void log_current_backtrace(const ss_ &title)
{
	void *trace[BACKTRACE_SIZE];
	int trace_size = backtrace(trace, BACKTRACE_SIZE);

	log_backtrace(trace, trace_size, title);
}

#include <cxxabi.h>
#include <dlfcn.h>

static void *last_exception_frames[BACKTRACE_SIZE];
static int last_exception_num_frames = 0;
static ss_ last_exception_name;

// GCC-specific
static ss_ demangle(const char *name)
{
	int status;
	std::unique_ptr<char,void(*)(void*)>
			realname(abi::__cxa_demangle(name, 0, 0, &status), &std::free);
	return status ? "failed" : &*realname;
}

// GCC-specific
extern "C" {
	void __cxa_throw(void *ex, void *info, void (*dest)(void *)) {
		last_exception_name = demangle(
				reinterpret_cast<const std::type_info*>(info)->name());
		last_exception_num_frames = backtrace(last_exception_frames,
				sizeof last_exception_frames / sizeof(void*));

		static void (*const rethrow)(void*,void*,void(*)(void*))
		__attribute__((noreturn)) = (void (*)(void*,void*,void(*)(void*)))
				dlsym(RTLD_NEXT, "__cxa_throw");
		rethrow(ex,info,dest);
	}
}

void log_exception_backtrace(const ss_ &title)
{
	log_backtrace(last_exception_frames, last_exception_num_frames, title);
}

void get_current_backtrace(StoredBacktrace &result)
{
	result.exception_name.clear();
	result.num_frames = backtrace(result.frames, BACKTRACE_SIZE);
}

void get_exception_backtrace(StoredBacktrace &result)
{
	result.exception_name = last_exception_name;
	result.num_frames = last_exception_num_frames;
	for(int i = 0; i < result.num_frames; i++){
		result.frames[i] = last_exception_frames[i];
	}
}

void log_backtrace(const StoredBacktrace &result, const ss_ &title)
{
	log_backtrace(result.frames, result.num_frames, title);
}

void log_backtrace_chain(const std::list<ThreadBacktrace> &chain,
		const char *reason, bool cut_at_api)
{
	JMutexAutoLock ms(backtrace_mutex);

	backtrace_buffer_len = 0;
	ss_ ex_name;
	bool header_printed = false;
	for(const bdebug::ThreadBacktrace &bt_step : chain){
		if(!bt_step.bt.exception_name.empty()){
			ex_name = bt_step.bt.exception_name;
		}
		if(!header_printed){
			header_printed = true;
			if(!ex_name.empty())
				bt_print("\n  Backtrace for %s(\"%s\"):", cs(ex_name), reason);
			else
				bt_print("\n  Backtrace for %s:", reason);
		}
		bt_print("  Thread(%s):", cs(bt_step.thread_name));
		char **symbols = backtrace_symbols(bt_step.bt.frames, bt_step.bt.num_frames);

		bool stop_at_next = false;

		auto bt_filter = [&](int i, int &first_real_i, 
				ss_ &cppfilt_symbol, ss_ &addr2line_output)
		{
			if(stop_at_next){
				return BFA_STOP;
			}
			// Skip this stuff at the beginning
			if(i <= 2 && i <= first_real_i &&
					addr2line_output.find("/basic_string.h") != ss_::npos){
				return BFA_START_LATER;
			}
			// Stop at current module API
			if(!bt_step.thread_name.empty() && addr2line_output.find(
					"/"+bt_step.thread_name+"/api.h") != ss_::npos){
				stop_at_next = true;
				return BFA_PASS;
			}
			return BFA_PASS;
		};

		bt_print_backtrace(symbols, bt_step.bt.frames, bt_step.bt.num_frames,
				1, bt_filter);
		bt_print_newline();

		free(symbols);
	}
	// Print to log
	backtrace_buffer[sizeof backtrace_buffer - 1] = 0;
	errorstream<<backtrace_buffer<<std::endl;
}

/*************************************************************************
 * Signal handling
 *************************************************************************/

// Used for signals because a signal often occurs inside core/log's mutex
// synchronization, causing a deadlock. Also, does not allocate new memory.
static void stderr_backtrace(void* const *trace, int trace_size, int sig)
{
	char **symbols = backtrace_symbols(trace, trace_size);

	backtrace_mutex.Lock();

	backtrace_buffer_len = 0;
	// The first stack frame points to this functiton
	bt_print_backtrace(symbols, trace, trace_size, 1);

	// Print to stderr
	backtrace_buffer[sizeof backtrace_buffer - 1] = 0;
	fprintf(stderr, "  Backtrace for signal %i:\n%s\n", sig, backtrace_buffer);

	backtrace_mutex.Unlock();

	free(symbols);
}

// Used for avoiding the handing of a SIGSEGV caused by trying to handle a
// SIGABRT or so.
static std::atomic_int g_signal_handlers_active(0);

static void debug_sighandler(int sig, siginfo_t *info, void *secret)
{
	// Make sure we're not handling a signal inside a signal handler
	int num_handlers_active = g_signal_handlers_active.fetch_add(1);
	if(num_handlers_active != 0){
		// Get out of here, we're in deep trouble
		g_signal_handlers_active--;
		exit(1);
	}

	// Disable libc memory allocation (and use the static memory pool)
	bdebug::cmem_libc_disable();
	// Disable worst memory allocation stuff in logging
	// TODO (not really possible with minetest's logging system)
	//log_disable_bloat();

	// NOTE: Do not use log in here, because a signal can occur inside the
	//       logging functions too

	ucontext_t *uc = (ucontext_t*)secret;
	fprintf(stderr, "\n");
	if(sig == SIGSEGV){
		fprintf(stderr, "Crash: SIGSEGV: Address %p (executing %p)\n",
				info->si_addr, (void*)uc->uc_mcontext.gregs[REG_EIP]);
	} else if(sig == SIGABRT){
		fprintf(stderr, "Crash: SIGABRT\n");
	} else {
		fprintf(stderr, "Crash: Signal %d\n", sig);
	}

	void *trace[BACKTRACE_SIZE];
	int trace_size = backtrace(trace, BACKTRACE_SIZE);
	// Overwrite sigaction with caller's address
	trace[1] = (void*) uc->uc_mcontext.gregs[REG_EIP];

	stderr_backtrace(trace, trace_size, sig);

	g_signal_handlers_active--;
	exit(1);
}

void init_signal_handlers(const SigConfig &config)
{
	struct sigaction sa;
	sa.sa_sigaction = debug_sighandler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;

	if(config.catch_segfault)
		sigaction(SIGSEGV, &sa, NULL);
	if(config.catch_abort)
		sigaction(SIGABRT, &sa, NULL);
}

}
// vim: set noet ts=4 sw=4:
