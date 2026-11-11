#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <sstream>
#include <iomanip>
#include <unordered_map>

#include "utils_macros.hpp"

#include "pipes.hpp"
#include "formatting.hpp"

namespace fl {

namespace detail {

// Graphviz writes the drawing's physical size in points (1in = 72pt) on the top-level <svg ...> tag, e.g.
// `width="394pt" height="4320pt"`. Rasterizing viewers built on librsvg (feh among them) refuse to render
// anything wider or taller than 32767px, which at a typical 96dpi corresponds to this many points -- past
// that, the SVG is perfectly valid but feh reports it as a broken image.
constexpr double LIBRSVG_RASTER_LIMIT_PT = 32767.0 * 72.0 / 96.0;

inline double svg_tag_dimension_pt(const std::string &svg_text, const char *attr) {
	const auto pos = svg_text.find(attr);
	if (pos == std::string::npos) return 0.0;
	return std::strtod(svg_text.c_str() + pos + std::strlen(attr), nullptr);
}

inline bool svg_too_big_to_rasterize(const std::string &svg_path) {
	std::ifstream	  in(svg_path);
	const std::string svg_text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return svg_tag_dimension_pt(svg_text, "width=\"") > LIBRSVG_RASTER_LIMIT_PT ||
		   svg_tag_dimension_pt(svg_text, "height=\"") > LIBRSVG_RASTER_LIMIT_PT;
}

}	  // namespace detail

template <class T>
class RangeFromPair : public std::ranges::view_interface<RangeFromPair<T>> {
	T range;

   public:
	using iterator		  = decltype(std::declval<T>().first);
	using sentinel		  = decltype(std::declval<T>().second);
	using value_type	  = std::remove_reference_t<decltype(*std::declval<iterator>())>;
	using difference_type = std::ptrdiff_t;
	using reference		  = value_type &;
	using const_reference = const value_type &;

	template <class U>
	RangeFromPair(U &&range) : range(std::forward<U>(range)) {}

	auto begin() { return range.first; }
	auto end() { return range.second; }
};

template <class T>
RangeFromPair(T &&range) -> RangeFromPair<T>;

template <auto N>
struct string_literal {
	constexpr string_literal(const char (&str)[N]) { std::ranges::copy_n(str, N, value); }

	char value[N];

	constexpr operator const char *() const { return value; }
};

std::string getString(std::istream &os);

std::string gen_random_string(const int len);

std::string gen_random_string(const int min, const int max);

class SlowDown {
	std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100);

   public:
	std::chrono::high_resolution_clock::time_point last;

   public:
	SlowDown(std::chrono::high_resolution_clock::duration delay = std::chrono::milliseconds(100));

	void do_thing(const std::function<void(void)> &f);
};

class SlowDown2 {
	time_t delay;

   public:
	time_t last;

   public:
	SlowDown2(time_t delay);

	void do_thing(const std::function<void(void)> &f);
};

class SlowDown3 {
	uint64_t delay;

   public:
	uint64_t last;

   public:
	SlowDown3(auto delay = std::chrono::milliseconds(100)) : delay(delay.count() * 4000000), last(0) {}

	void do_thing(const std::function<void(void)> &f);
};

class Timer {
	std::chrono::high_resolution_clock::time_point start;
	std::ostream								  *out;
	std::string									   name;

   public:
	Timer(const std::string_view &name, std::ostream &out = std::cerr);
	Timer(std::nullptr_t);

	~Timer();

	std::chrono::high_resolution_clock::duration elapsed() const;

	long elapsed_min() const;
	long elapsed_s() const;
	long elapsed_ms() const;
	long elapsed_us() const;
	long elapsed_ns() const;
};

template <class T>
void drawFSA(const T &fsa) {
	{
		ShellProcess p("dot -Tsvg > a.svg");
		fsa.print(p.in());
		p.in() << std::endl;
		p.in().close();
		p.wait();
		if (auto out = getString(p.out()); !out.empty()) std::cerr << out << std::endl;
		if (auto err = getString(p.err()); !err.empty()) std::cerr << err << std::endl;
	}

	if (std::getenv("FL_NO_DRAWING")) {
		std::cerr << "FL_NO_DRAWING set, skipping drawing. Output is in a.svg" << std::endl;
		return;
	}

	/// feh will fail to rasterize big SVGs, so use firefox instead if the SVG is too big
	bool		is_too_big = detail::svg_too_big_to_rasterize("a.svg");
	const char *viewer_cmd;
	if (is_too_big) {
		std::cerr << "SVG is too big to rasterize with feh, using firefox instead" << std::endl;
		viewer_cmd = "firefox ./a.svg";
	} else {
		viewer_cmd = "feh ./a.svg";
	}
	ShellProcess viewer(viewer_cmd);
	viewer.wait();
	if (auto out = getString(viewer.out()); !out.empty()) std::cerr << out << std::endl;
	if (auto err = getString(viewer.err()); !err.empty()) std::cerr << err << std::endl;
}

void print_escaped_char(std::ostream &out, unsigned char c);

namespace detail {
void print_exception(std::ostream &out, const std::exception &e, int level = 0);
}	  // namespace detail

std::ostream &operator<<(std::ostream &out, const std::exception &e);

}	  // namespace fl

static_assert(
	std::ranges::view<fl::RangeFromPair<decltype(std::declval<std::unordered_map<int, int>>().equal_range(0))>>,
	"RangeFromPair should be a view");
