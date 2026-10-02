#pragma once

#include <tuple>
#include <ostream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <format>
#include <string_view>
#include <sstream>


namespace fl {

template <class... Args>
std::ostream &operator<<(std::ostream &out, const std::tuple<Args...> &t);
template <class A, class B>
std::ostream &operator<<(std::ostream &out, const std::pair<A, B> &t);
template <class T>
std::ostream &operator<<(std::ostream &out, std::vector<T> v);

template <class... Args>
std::ostream &operator<<(std::ostream &out, const std::tuple<Args...> &t) {
	out << "(";
	[&]<std::size_t... p>(std::index_sequence<p...>) {
		((out << (p ? ", " : "") << "\'" << std::get<p>(t) << "\'"), ...);
	}(std::make_index_sequence<std::tuple_size_v<std::tuple<Args...>>>{});
	return out << ")";
}

template <class A, class B>
std::ostream &operator<<(std::ostream &out, const std::pair<A, B> &t) {
	return out << "(" << t.first << ", " << t.second << ")";
}

template <class T>
std::ostream &operator<<(std::ostream &out, std::vector<T> v) {
	for (const T &x : v)
		out << x;
	return out;
}

template <class T>
std::ostream &operator<<(std::ostream &out, std::unordered_set<T> v) {
	out << "( ";
	for (const T &x : v)
		out << '\'' << x << "\' ";
	out << ")";
	return out;
}

std::ostream &operator<<(std::ostream &out, const std::exception &e);

template <class A, class B>
std::ostream &operator<<(std::ostream &out, const std::unordered_map<A, B> &m) {
	out << "{";
	for (auto &[K, V] : m) {
		out << '(' << K << " : " << V << ')' << std::endl;
	}
	out << "}";
	return out;
}

template <typename Char>
struct basic_ostream_formatter : std::formatter<std::basic_string_view<Char>, Char> {
	template <typename T, typename OutputIt>
	//	requires requires(std::basic_ostream<Char> &os, const T &v) { os << v; }
	auto format(const T &value, std::basic_format_context<OutputIt, Char> &ctx) const -> OutputIt {
		std::basic_stringstream<Char> ss;
		ss << value;
		return std::formatter<std::basic_string_view<Char>, Char>::format(ss.view(), ctx);
	}
};

using ostream_formatter = basic_ostream_formatter<char>;

}	  // namespace fl

template <>
struct std::formatter<std::exception> : fl::ostream_formatter {};
template <class... Args>
struct std::formatter<std::tuple<Args...>> : fl::ostream_formatter {};
template <class A, class B>
struct std::formatter<std::pair<A, B>> : fl::ostream_formatter {};
template <class T>
struct std::formatter<std::unordered_set<T>> : fl::ostream_formatter {};
template <class T>
struct std::formatter<std::vector<T>> : fl::ostream_formatter {};
template <class A, class B>
struct std::formatter<std::unordered_multimap<A, B>> : fl::ostream_formatter {};
