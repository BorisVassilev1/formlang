#include <token.h>
#include <memory>
#include <unordered_map>
#include "utils.h"

namespace fl {

using NamesMap								= std::unordered_map<std::size_t, std::string>;
static std::unique_ptr<NamesMap> TokenNames = nullptr;

static std::unordered_map<std::size_t, std::string> &getTokenNames() {
	if (!TokenNames) return *(TokenNames = std::make_unique<NamesMap>());
	else return *TokenNames;
}

void Token::setTokenName(std::size_t value, const std::string &name) {
	size = std::max(size, value + 1);
	getTokenNames().insert({value, name});
}

// registers a Token with a name
Token Token::createToken(const std::string &name, std::size_t value) {
	getTokenNames().insert({value, name});
	return Token(value);
}

Token Token::createDependentToken(const Token &base) {
	getTokenNames().insert({++Token::size, getTokenNames().find(base.value)->second + "'"});
	return Token(Token::size);
}

void Token::debug_print(std::ostream &out) const {
	if (value < Token::INITIAL_SIZE) print_escaped_char(out, static_cast<unsigned char>(value));
	auto it = getTokenNames().find(value);
	if (it == getTokenNames().end()) { out << value; }
	out << it->second;
}

std::ostream &operator<<(std::ostream &out, const Token &v) {
	if (v.value < Token::INITIAL_SIZE) return out << static_cast<unsigned char>(v.value);
	auto it = getTokenNames().find(v.value);
	if (it == getTokenNames().end()) { return out << v.value; }
	return out << it->second;
}

std::size_t Token::size = Token::INITIAL_SIZE;

const Token Token::eps = Token::createToken("ε");
const Token Token::eof = Token::createToken("eof");

}	  // namespace fl
