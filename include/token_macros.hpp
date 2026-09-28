#pragma once

// Macros can't be exported from a C++20 module interface, so this (originally
// part of token.h) lives here as a plain header. Consumers that need it must
// #include this directly, in addition to `import formlang;` for fl::Token.

#define CREATE_TOKEN_CONSTEXPR(name, string)                                                      \
	constexpr fl::Token name		 = fl::Token::createTokenConstexpr(fl::Token::unique_id<>()); \
	static int			_init_##name = []() {                                                     \
		fl::Token::setTokenName(name.value, string);                                              \
		return 0;                                                                                 \
	}();
