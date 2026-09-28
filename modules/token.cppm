module;

#include <cstddef>
#include <ostream>
#include <unordered_map>
#include <format>
#include <memory>
#include <string>

#include "token_macros.hpp"

export module formlang:token;

import :concepts;
import :hashing;
import :formatting;
import :utils;

namespace fl {
namespace detail {
using TokenNamesMap = std::unordered_map<std::size_t, std::string>;
inline TokenNamesMap &getTokenNames() {
	static TokenNamesMap names;
	return names;
}
}	  // namespace detail
}	  // namespace fl

export namespace fl {
/**
 * @brief A Token with a name
 *
 */
struct Token {
	std::size_t value;
	uint8_t	   *data = nullptr;

	constexpr Token(std::size_t value) : value(value) {}

	template <auto Id>
	struct counter {
		using tag = counter;

		struct generator {
			friend consteval auto is_defined(tag) { return true; }
		};
		friend consteval auto is_defined(tag);

		template <typename Tag = tag, auto = is_defined(Tag{})>
		static consteval auto exists(auto) {
			return true;
		}

		static consteval auto exists(...) { return generator(), false; }
	};

   public:
	template <auto Id = int{}, typename = decltype([] {})>
	static consteval auto unique_id() {
		if constexpr (not counter<Id>::exists(Id)) return INITIAL_SIZE + Id;
		else return unique_id<Id + 1>();
	}

	Token(const Token &other)			 = default;
	Token(Token &&other)				 = default;
	Token &operator=(const Token &other) = default;
	Token &operator=(Token &&other)		 = default;

	consteval Token() : value(0) {}
	constexpr Token(char value) : value(value) {}
	constexpr Token(int value) : value(value) {}
	Token(const Token &other, uint8_t *data) : value(other.value), data(data) {}

	explicit constexpr operator int64_t() const { return value; }
	explicit constexpr operator std::size_t() const { return value; }
	explicit constexpr operator char() const { return value; }
	explicit constexpr operator uint8_t() const { return value; }
	explicit constexpr operator uint16_t() const { return value; }
	explicit constexpr operator int() const { return value; }
	explicit constexpr operator bool() const { return value; }

	static constexpr std::size_t INITIAL_SIZE = 256;
	static std::size_t			 size;
	static const Token			 eps;
	static const Token			 eof;

	static constexpr Token createTokenConstexpr(std::size_t value) { return Token(value); }
	static void			   setTokenName(std::size_t value, const std::string &name) {
		size = std::max(size, value + 1);
		detail::getTokenNames().insert({value, name});
	}

	static Token createToken(const std::string &name, std::size_t value = ++size) {
		detail::getTokenNames().insert({value, name});
		return Token(value);
	}
	static Token createDependentToken(const Token &base) {
		detail::getTokenNames().insert({++Token::size, detail::getTokenNames().find(base.value)->second + "'"});
		return Token(Token::size);
	}

	bool operator==(const Token &other) const { return value == other.value; }
	bool operator!=(const Token &other) const { return value != other.value; }
	bool operator<(const Token &other) const { return value < other.value; }
	bool operator<=(const Token &other) const { return value <= other.value; }
	bool operator>(const Token &other) const { return value > other.value; }
	bool operator>=(const Token &other) const { return value >= other.value; }

	void debug_print(std::ostream &out) const {
		if (value < Token::INITIAL_SIZE) print_escaped_char(out, static_cast<unsigned char>(value));
		auto it = detail::getTokenNames().find(value);
		if (it == detail::getTokenNames().end()) { out << value; }
		out << it->second;
	}

	Token operator++() {
		++value;
		return *this;
	}

	uint8_t *getData() { return data; }
	template <typename T>
		requires(sizeof(T) <= sizeof(uint8_t *))
	auto &setData(const T &val) {
		data = (uint8_t *)val;
		return *this;
	}
};

inline std::size_t Token::size = Token::INITIAL_SIZE;

inline std::ostream &operator<<(std::ostream &out, const Token &v) {
	if (v.value < Token::INITIAL_SIZE) return out << static_cast<unsigned char>(v.value);
	auto it = detail::getTokenNames().find(v.value);
	if (it == detail::getTokenNames().end()) { return out << v.value; }
	return out << it->second;
}

inline const Token Token::eps = Token::createToken("ε");
inline const Token Token::eof = Token::createToken("eof");

static_assert(fl::symbol<fl::Token>, "fl::Token does not satisfy isLetter concept");

}	  // namespace fl
export template <>
struct std::formatter<fl::Token> : fl::ostream_formatter {};
