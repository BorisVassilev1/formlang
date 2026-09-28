#pragma once

// Macros can't be exported from a C++20 module interface, so the dbLog/dbLogR
// logging macros (originally part of debug.hpp) live here as a plain header.
// Consumers that need them must #include this directly, in addition to
// `import formlang;` for the dbg:: names (f_dbLog, log_colors, LOG_*) they
// expand into.

#define COLOR_RESET	 "\033[0m"
#define COLOR_RED	 "\x1B[0;91m"
#define COLOR_GREEN	 "\x1B[0;92m"
#define COLOR_YELLOW "\x1B[0;93m"

#ifndef NDEBUG
	#define DBG_DEBUG
	#ifndef DBG_LOG_LEVEL
		#define DBG_LOG_LEVEL -1
	#endif
#else
	#ifndef DBG_LOG_LEVEL
		#define DBG_LOG_LEVEL 1
	#endif
#endif

#define dbLog(severity, ...)                                                                                       \
	{                                                                                                              \
		if constexpr (severity >= DBG_LOG_LEVEL) {                                                                 \
			if constexpr (severity >= dbg::LOG_WARNING) {                                                          \
				dbg::f_dbLog(std::cerr, dbg::log_colors[severity], "[", #severity, "] ", __VA_ARGS__, COLOR_RESET, \
							 '\n');                                                                                \
			} else {                                                                                               \
				dbg::f_dbLog(std::cout, dbg::log_colors[severity], "[", #severity, "] ", __VA_ARGS__, COLOR_RESET, \
							 '\n');                                                                                \
			}                                                                                                      \
		}                                                                                                          \
	}
#define dbLogR(severity, ...)                                                                               \
	{                                                                                                       \
		if constexpr (severity >= DBG_LOG_LEVEL) {                                                          \
			if constexpr (severity >= dbg::LOG_WARNING) {                                                   \
				dbg::f_dbLog(std::cerr, '\r', dbg::log_colors[severity], "[", #severity, "] ", __VA_ARGS__, \
							 COLOR_RESET);                                                                  \
			} else {                                                                                        \
				dbg::f_dbLog(std::cout, '\r', dbg::log_colors[severity], "[", #severity, "] ", __VA_ARGS__, \
							 COLOR_RESET);                                                                  \
			}                                                                                               \
		}                                                                                                   \
	}
