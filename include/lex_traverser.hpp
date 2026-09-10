#pragma once

#include <cstdio>
#include <functional>
#include <iterator>
#include <ranges>
#include <istream>
#include <map>

#include <concepts.hpp>

namespace fl {
template <symbol Symbol>
class CharInputStream : public std::ranges::view_interface<CharInputStream<Symbol>> {
   public:
	std::istream *input_stream = nullptr;

	CharInputStream() = default;
	CharInputStream(std::istream &input_stream) : input_stream(&input_stream) {}
	CharInputStream(const CharInputStream &other)			 = default;
	CharInputStream(CharInputStream &&other)				 = default;
	CharInputStream &operator=(const CharInputStream &other) = default;
	CharInputStream &operator=(CharInputStream &&other)		 = default;

	struct sentinel;

	class iterator {
	   public:
		using iterator_category = std::input_iterator_tag;
		using value_type		= Symbol;
		using difference_type	= std::ptrdiff_t;
		using pointer			= Symbol *;
		using reference			= Symbol;

		std::istream *input_stream;
		Symbol		  current_char = Symbol::eps;
		bool		  end_reached  = false;

		iterator(std::istream *input_stream) : input_stream(input_stream) { ++*this; }

		iterator(const iterator &other)			   = default;
		iterator(iterator &&other)				   = default;
		iterator &operator=(const iterator &other) = default;
		iterator &operator=(iterator &&other)	   = default;

		bool operator!=(const sentinel &) const { return !end_reached; }
		bool operator==(const sentinel &) const { return end_reached; }

		iterator &operator++() {
			int ch		= input_stream->get();
			end_reached = (current_char == Symbol::eof);

			if (input_stream->eof()) {
				current_char = Symbol::eof;
			} else {
				current_char = Symbol((char)ch);
			}
			return *this;
		}
		iterator operator++(int) {
			iterator temp = *this;
			++*this;
			return temp;
		}

		Symbol operator*() const { return current_char; }
	};
	struct sentinel {
		bool operator==(const iterator &it) const { return it.end_reached; }
		bool operator!=(const iterator &it) const { return !it.end_reached; }
	};

	iterator begin() { return iterator(input_stream); }
	sentinel end() { return sentinel{}; }
};
}	  // namespace fl

#include "letter.hpp"

static_assert(std::sentinel_for<fl::CharInputStream<fl::Letter>::sentinel, fl::CharInputStream<fl::Letter>::iterator>);
static_assert(std::ranges::viewable_range<fl::CharInputStream<fl::Letter>>);
static_assert(std::ranges::input_range<fl::CharInputStream<fl::Letter>>);

namespace fl {

template <SSFST T, std::ranges::input_range Range>
	requires(!SSFSTI<T> && std::convertible_to<std::ranges::range_value_t<Range>, typename T::Letter_t>)
class LexerRange : public std::ranges::view_interface<LexerRange<T, Range>> {
	using State	 = typename T::State;
	using Symbol = typename T::Letter_t;

	using This = LexerRange<T, Range>;

   public:
	using InnerIterator = decltype(std::ranges::begin(std::declval<Range &>()));
	using InnerSentinel = decltype(std::ranges::end(std::declval<Range &>()));

	Range	*range;
	const T &ssft;
	Symbol	 error_token;

	LexerRange(Range &range, const T &ssft, Symbol error_token) : range(&range), ssft(ssft), error_token(error_token) {}

	class iterator {
	   public:
		InnerIterator		current;
		InnerSentinel		end;
		State				current_state;
		This			   *lex_ptr;
		std::size_t			position;
		std::size_t			output_position;
		std::size_t			line_number = 1;
		mutable bool		consumed	= false;
		std::vector<Symbol> buffer;
		mutable Symbol		queued_token = Symbol::eps;

		iterator(InnerIterator &&begin, InnerSentinel &&end, This *lex_ptr)
			: current(std::move(begin)),
			  end(std::move(end)),
			  current_state(0),
			  lex_ptr(lex_ptr),
			  position(0),
			  output_position(0) {
			++*this;
		}

		bool operator!=(const InnerSentinel &other) const { return !consumed || current != other; }
		bool operator==(const InnerSentinel &other) const { return consumed && current == other; }

		iterator &operator++() {
			auto ssft_ptr	= &lex_ptr->ssft;
			output_position = position;
			current_state	= 0;
			consumed		= current == end;
			buffer.clear();
			while (current != end) {
				auto [output, success] = ssft_ptr->step(current_state, *current);
				if (!success) {
					if (ssft_ptr->isFinal(current_state)) {
						Symbol output = ssft_ptr->psi(current_state)[0];
						auto   it	  = lex_ptr->skippers.find(output);
						if (it != lex_ptr->skippers.end()) {
							auto skipper  = it->second;
							auto [len, t] = skipper(*this);
							position += len;
							queued_token = t;
						}
						return *this;
					}
					buffer.clear();
					if (*current == '\n') ++line_number;
					buffer.push_back(*current);
					++position;
					++current;

					current_state = ssft_ptr->initial();
					return *this;
				}
				if (*current == '\n') ++line_number;
				buffer.push_back(*current);
				++current;
				++position;
			}
			return *this;
		}

		struct TokenData {
			Symbol					token;
			std::size_t				from;
			std::size_t				to;
			std::size_t				line;
			std::span<const Symbol> str;
		};

		TokenData operator*() const {
			consumed = true;
			if (queued_token != Symbol::eps) {
				auto t		 = queued_token;
				queued_token = Symbol::eps;
				return TokenData{t, output_position, position - 1, line_number,
								 std::span(buffer.begin(), buffer.end())};
			}
			if (lex_ptr->ssft.isFinal(current_state)) {
				return TokenData{lex_ptr->ssft.psi(current_state)[0], output_position, position - 1, line_number,
								 std::span(buffer.begin(), buffer.end())};
			} else {
				return TokenData{lex_ptr->error_token, output_position, position - 1, line_number,
								 std::span(buffer.begin(), buffer.end())};
			}
		}
	};
	using SkipperFunction = std::function<std::tuple<size_t, Symbol>(iterator &it)>;
	std::map<Symbol, SkipperFunction> skippers;
	void attachSkipper(Symbol token, SkipperFunction skipper) { skippers[token] = skipper; }

	iterator begin() { return iterator(std::ranges::begin(*range), std::ranges::end(*range), this); }
	auto	 end() { return std::ranges::end(*range); };
};

template <SSFST T, std::ranges::input_range Range, symbol Symbol>
LexerRange(Range &, const T &, Symbol) -> LexerRange<T, Range>;

}	  // namespace fl
