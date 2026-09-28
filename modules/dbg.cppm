module;

#include <memory>
#include <mutex>
#include <type_traits>
#include <cxxabi.h>
#include <string>
#include <iostream>

export module formlang:dbg;

export namespace dbg {

template <class T>
auto type_name() {
	typedef typename std::remove_reference<T>::type TR;

	std::unique_ptr<char, void (*)(void *)> own(abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
												std::free);

	std::string r = own != nullptr ? own.get() : typeid(TR).name();
	if (std::is_const<TR>::value) r += " const";
	if (std::is_volatile<TR>::value) r += " volatile";
	if (std::is_lvalue_reference<T>::value) r += "&";
	else if (std::is_rvalue_reference<T>::value) r += "&&";
	return r;
}

inline auto typename_demangle(const char *n) {
	std::unique_ptr<char, void (*)(void *)> own(abi::__cxa_demangle(n, nullptr, nullptr, nullptr), std::free);
	std::string								r = own != nullptr ? own.get() : n;
	return r;
}

template <class T>
inline auto type_name(T *v) {
	return typename_demangle(typeid(v).name());
}
/**
 *
 * @brief Log levels
 */
enum {
	LOG_DEBUG	= (0),
	LOG_INFO	= (1),
	LOG_WARNING = (2),
	LOG_ERROR	= (3),
};

inline const char *log_colors[]{"\x1B[0;92m", "\033[0m", "\x1B[0;93m", "\x1B[0;91m"};

inline std::mutex &getMutex() {
	static std::mutex m;
	return m;
}

/**
 * @brief prints to std::cerr
 *
 * @return 1
 */
template <class... Types>
bool inline f_dbLog(std::ostream &out, Types... args) {
	std::lock_guard lock(dbg::getMutex());
	(out << ... << args) << std::flush;
	return 1;
}

}	  // namespace dbg
