#pragma once
#include <cstddef>
#include <ostream>
#include <unordered_map>
#include <format>

#include "hashing.hpp"
#include "formatting.hpp"

namespace fl {
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
	static void			   setTokenName(std::size_t value, const std::string &name);

	static Token createToken(const std::string &name, std::size_t value = ++size);
	static Token createDependentToken(const Token &base);

	bool				 operator==(const Token &other) const { return value == other.value; }
	bool				 operator!=(const Token &other) const { return value != other.value; }
	bool				 operator<(const Token &other) const { return value < other.value; }
	bool				 operator<=(const Token &other) const { return value <= other.value; }
	bool				 operator>(const Token &other) const { return value > other.value; }
	bool				 operator>=(const Token &other) const { return value >= other.value; }
	friend std::ostream &operator<<(std::ostream &out, const Token &v);

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

#define CREATE_TOKEN_CONSTEXPR(name, string)                                                      \
	constexpr fl::Token name		 = fl::Token::createTokenConstexpr(fl::Token::unique_id<>()); \
	static int			_init_##name = []() {                                                     \
		fl::Token::setTokenName(name.value, string);                                              \
		return 0;                                                                                 \
	}();

// static_assert(Token::unique_id() == 256);
// static_assert(Token::unique_id() == 257);
// static_assert(Token::unique_id() == 258);
// static_assert(Token::unique_id() == 259);

std::ostream &operator<<(std::ostream &out, const Token &v);

static_assert(fl::symbol<fl::Token>, "fl::Token does not satisfy isLetter concept");

}	  // namespace fl
template <>
struct std::formatter<fl::Token> : fl::ostream_formatter {};
