// http://www.apache.org/licenses/LICENSE-2.0
// Copyright 2014 Perttu Ahola <celeron55@gmail.com>
#include "bdebug/debug.h"
#include "log.h"
#define MODULE "bdebug"

namespace bdebug {

void log_current_backtrace(const ss_ &title)
{
	log_i(MODULE, "Backtrace logging not implemented on Windows");
}

void log_exception_backtrace(const ss_ &title)
{
	log_i(MODULE, "Backtrace logging not implemented on Windows");
}

void get_current_backtrace(StoredBacktrace &result)
{
}

void get_exception_backtrace(StoredBacktrace &result)
{
}

void log_backtrace(const StoredBacktrace &result, const ss_ &title)
{
}

void log_backtrace_chain(const std::list<ThreadBacktrace> &chain,
		const char *reason, bool cut_at_api)
{
}

void init_signal_handlers(const SigConfig &config)
{
	// No-op
}

}
// vim: set noet ts=4 sw=4:
