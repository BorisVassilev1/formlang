#pragma once

#define CREATE_TOKEN_CONSTEXPR(name, string)                                                      \
	constexpr fl::Token name		 = fl::Token::createTokenConstexpr(fl::Token::unique_id<>()); \
	static int			_init_##name = []() {                                                     \
		fl::Token::setTokenName(name.value, string);                                              \
		return 0;                                                                                 \
	}();
