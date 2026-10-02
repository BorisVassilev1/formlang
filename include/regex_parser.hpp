#pragma once

#include <cstring>
#include <stack>
#include <sstream>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>


#include "parser.hpp"
#include "token.hpp"
#include "cfg.hpp"
#include "formatting.hpp"
#include "utils.hpp"

namespace rgx {
using fl::ParseNode;
using fl::Token;

inline const Token Identifier	   = Token::createToken("id");
inline const Token Tuple		   = Token::createToken("tuple");
inline const Token Concatenation  = Token::createToken("concat");
inline const Token Concatenation_ = Token::createToken("concat'");
inline const Token Union		   = Token::createToken("union");
inline const Token Union_		   = Token::createToken("union'");
inline const Token KleeneStar	   = Token::createToken("star");
inline const Token KleeneStar_	   = Token::createToken("star'");
inline const Token Braces		   = Token::createToken("braces");
inline const Token Null		   = Token::createToken("null");

struct TokenizedString : public std::vector<Token> {
	TokenizedString() = default;
	using std::vector<Token>::vector;
	using std::vector<Token>::operator=;
	TokenizedString(std::vector<Token> &&other) : std::vector<Token>(std::move(other)) {}
	~TokenizedString() {
		for (auto &token : *this) {
			if (token.value == Identifier.value && token.data) {
				delete[] reinterpret_cast<char *>(token.data);
				token.data = nullptr;
			}
		}
	}
};

inline TokenizedString tokenize(const std::string &text) {
	std::vector<Token> res;
	for (std::size_t i = 0; i < text.length(); ++i) {
		if (std::isspace(text[i])) continue;
		if (text[i] == '\'') {
			std::string name;
			bool		escape = false;
			++i;
			while (i < text.length() && (escape || text[i] != '\'')) {
				if (text[i] == '\\' && !escape) {
					escape = true;
				} else {
					name += text[i];
					escape = false;
				}
				++i;
			}
			char *s = new char[name.size() + 1];
			strcpy(s, name.c_str());
			res.push_back(Token(Identifier, reinterpret_cast<uint8_t *>(s)));
		} else if (std::string_view(text.begin() + i, text.end()).starts_with("null")) {
			res.push_back(Null);
			i += 3;
		} else res.push_back(text[i]);
	}
	res.push_back(Token::eof);
	return res;
}

inline fl::CFG<Token> createRegexGrammar() {
	fl::CFG<Token> g(Union, Token::eof);
	g.nonTerminals = {Concatenation, Concatenation_, Union, Union_, KleeneStar, KleeneStar_, Tuple, Braces};
	g.terminals	   = {Token::eof, Identifier, '.', '+', '*', '<', ',', '>', '(', ')', '!', Null};

	g.addRule(Union, {Concatenation, Union_});
	g.addRule(Union_, {'+', Concatenation, Union_});
	g.addRule(Union_, {});
	g.addRule(Concatenation, {KleeneStar, Concatenation_});
	g.addRule(Concatenation_, {'.', KleeneStar, Concatenation_});
	g.addRule(Concatenation_, {});
	g.addRule(KleeneStar, {Braces, KleeneStar_});
	g.addRule(KleeneStar_, {'*'});
	g.addRule(KleeneStar_, {'!'});
	g.addRule(KleeneStar_, {});
	g.addRule(Tuple, {'<', Identifier, ',', Identifier, '>'});
	g.addRule(Tuple, {Identifier});
	g.addRule(Tuple, {Null});
	g.addRule(Braces, {'(', Union, ')'});
	g.addRule(Braces, {Tuple});
	return g;
}

class RegexParser : public fl::Parser<fl::Token> {
   public:
	RegexParser() : Parser<Token>(createRegexGrammar()) {}

	std::unique_ptr<ParseNode<Token>> makeParseTree(
		const std::vector<std::reference_wrapper<const Parser<Token>::DeltaMap::value_type>> &productions,
		const std::vector<Token> &word, int &k, int &j) {
		std::vector<std::unique_ptr<ParseNode<Token>>> children;
		auto										  &product = std::get<1>(productions[k].get().second);
		int											   old_k   = k;
		++k;

		bool skipped_star = false;
		bool is_excl	  = false;
		for (size_t i = 0; i < product.size(); ++i) {
			if (product[i] == word[j]) {
				if (word[j] == Identifier || word[j] == Null || word[j] == '*' || word[j] == '!') {
					children.push_back(std::make_unique<ParseNode<Token>>(word[j]));
				}
				++j;
			} else {
				auto child = makeParseTree(productions, word, k, j);
				if (child->value == '*' || child->value == '!') skipped_star = true;
				if (child->value == '!') is_excl = true;
				if (!child->children.empty()) children.push_back(std::move(child));
			}
		}
		auto t = std::get<2>(productions[old_k].get().first);
		if (children.size() == 1 && !skipped_star && t != Tuple) { return std::move(children[0]); }
		if (is_excl) t.data = reinterpret_cast<uint8_t *>('!');
		return std::make_unique<ParseNode<Token>>(t, std::move(children));
	}

	auto parse(const std::vector<Token> &tokens) {
		auto prod = generateProductions(tokens);
		int	 i = 0, k = 0;
		return makeParseTree(prod.second, tokens, i, k);
	}
};

class Regex {
   public:
	virtual void print(std::ostream &out) const = 0;
	virtual ~Regex()							= default;
	virtual int size() const					= 0;
};

class UnionRegex : public Regex {
   public:
	std::unique_ptr<Regex> left;
	std::unique_ptr<Regex> right;
	UnionRegex(std::unique_ptr<Regex> left, std::unique_ptr<Regex> right)
		: left(std::move(left)), right(std::move(right)) {}

	void print(std::ostream &out) const override {
		out << "(";
		if (left) left->print(out);
		out << "+";
		if (right) right->print(out);
		out << ")";
	}

	int size() const override { return (left ? left->size() : 0) + (right ? right->size() : 0) + 1; }
};

class ConcatRegex : public Regex {
   public:
	std::unique_ptr<Regex> left;
	std::unique_ptr<Regex> right;
	ConcatRegex(std::unique_ptr<Regex> left, std::unique_ptr<Regex> right)
		: left(std::move(left)), right(std::move(right)) {}

	void print(std::ostream &out) const override {
		out << "(";
		if (left) left->print(out);
		out << ".";
		if (right) right->print(out);
		out << ")";
	}

	int size() const override { return (left ? left->size() : 0) + (right ? right->size() : 0) + 1; }
};

class KleeneStarRegex : public Regex {
   public:
	std::unique_ptr<Regex> child;

	KleeneStarRegex(std::unique_ptr<Regex> child) : child(std::move(child)) {}
	void print(std::ostream &out) const override {
		out << "(";
		if (child) child->print(out);
		out << ")*";
	}

	int size() const override { return (child ? child->size() : 0) + 1; }
};

class KleenePlusRegex : public Regex {
   public:
	std::unique_ptr<Regex> child;

	KleenePlusRegex(std::unique_ptr<Regex> child) : child(std::move(child)) {}
	void print(std::ostream &out) const override {
		out << "(";
		if (child) child->print(out);
		out << ")!";
	}

	int size() const override { return (child ? child->size() : 0) + 1; }
};

template <class Letter = char>
class TupleRegex : public Regex {
   public:
	std::vector<Letter> left;
	std::vector<Letter> right;

	TupleRegex(std::string left, std::string right)
		requires std::is_same_v<Letter, char>
		: left(left.begin(), left.end()), right(right.begin(), right.end()) {}
	TupleRegex(std::vector<Letter> left, std::vector<Letter> right) : left(std::move(left)), right(std::move(right)) {}

	void print(std::ostream &out) const override {
		using fl::operator<<;
		out << "<'" << left << "','" << right << "'>";
	}

	int size() const override { return 1; }
};

inline std::unique_ptr<Regex> generateRegex() {
	if (rand() % 2) return std::make_unique<TupleRegex<char>>(fl::gen_random_string(0, 3), fl::gen_random_string(0, 3));

	int r = rand() % 3;
	switch (r) {
		case 0: return std::make_unique<UnionRegex>(generateRegex(), generateRegex());
		case 1: return std::make_unique<ConcatRegex>(generateRegex(), generateRegex());
		case 2: return std::make_unique<KleeneStarRegex>(generateRegex());
	}
	return nullptr;		// Should never reach here
}

inline std::string generateRegexString(std::size_t min) {
	while (true) {
		std::stringstream ss;
		auto			  regex = generateRegex();
		regex->print(ss);
		auto str = ss.str();
		if (str.size() >= min) { return str; }
	}
}

inline std::unique_ptr<Regex> parseTreeToRegex(const ParseNode<Token> *root, TokenizedString &owner) {
	if (!root) return nullptr;

	if (root->value == Union || root->value == Union_) {
		return std::make_unique<UnionRegex>(parseTreeToRegex(root->children[0].get(), owner),
											parseTreeToRegex(root->children[1].get(), owner));
	} else if (root->value == Concatenation || root->value == Concatenation_) {
		return std::make_unique<ConcatRegex>(parseTreeToRegex(root->children[0].get(), owner),
											 parseTreeToRegex(root->children[1].get(), owner));
	} else if (root->value == KleeneStar) {
		if ((size_t)root->value.data == (size_t)'!')
			return std::make_unique<KleenePlusRegex>(parseTreeToRegex(root->children[0].get(), owner));
		else return std::make_unique<KleeneStarRegex>(parseTreeToRegex(root->children[0].get(), owner));

	} else if (root->value == Tuple) {
		if (root->children.size() == 1) {
			if (root->children[0]->value == Null) {
				return std::make_unique<TupleRegex<char>>(std::string(1ull, '\0'), std::string());
			} else
				return std::make_unique<TupleRegex<char>>(
					std::string(reinterpret_cast<char *>(root->children[0]->value.data)), std::string());
		} else if (root->children.size() == 2) {
			char *left	 = reinterpret_cast<char *>(root->children[0]->value.data);
			char *right	 = reinterpret_cast<char *>(root->children[1]->value.data);
			auto  left_s = std::string(left), right_s = std::string(right);
			return std::make_unique<TupleRegex<char>>(std::move(left_s), std::move(right_s));
		}
		return nullptr;
	}
	return nullptr;
}

inline std::unique_ptr<Regex> toLeftAssoc(std::unique_ptr<Regex> &&regex) {
	if (auto *r = dynamic_cast<UnionRegex *>(regex.get())) {
		if (auto *right = dynamic_cast<UnionRegex *>(r->right.get())) {
			auto b		= std::move(right->left);
			right->left = std::move(regex);
			regex		= std::move(r->right);
			r->right	= std::move(b);

			regex = toLeftAssoc(std::move(regex));
		} else {
			r->left	 = toLeftAssoc(std::move(r->left));
			r->right = toLeftAssoc(std::move(r->right));
		}
	} else if (auto *r = dynamic_cast<ConcatRegex *>(regex.get())) {
		if (auto *right = dynamic_cast<ConcatRegex *>(r->right.get())) {
			auto b		= std::move(right->left);
			right->left = std::move(regex);
			regex		= std::move(r->right);
			r->right	= std::move(b);

			regex = toLeftAssoc(std::move(regex));
		} else {
			r->left	 = toLeftAssoc(std::move(r->left));
			r->right = toLeftAssoc(std::move(r->right));
		}
	} else if (auto *r = dynamic_cast<KleeneStarRegex *>(regex.get())) {
		r->child = toLeftAssoc(std::move(r->child));
	}
	return std::move(regex);
}

inline std::unique_ptr<Regex> parseRegex(const std::string &text) {
	static RegexParser parser;
	auto			   tokens = tokenize(text);
	if (tokens.empty()) return nullptr;
	auto parseTree = parser.parse(tokens);
	auto r		   = parseTreeToRegex(parseTree.get(), tokens);
	return toLeftAssoc(std::move(r));
}

inline std::string Alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._?;:/!@#$%^&*()-+=<>[]{}|\\`~";

inline std::string identity(const std::string &alphabet) {
	std::string result;
	for (std::size_t i = 0; i < alphabet.size(); ++i) {
		if (i > 0) result += "+";
		char c = alphabet[i];
		result += std::format("<'{}', '{}'>", c, c);
	}
	return result;
}

inline std::string optionalReplace(const std::string &regex, const std::string &alphabet = Alphabet) {
	auto id = identity(alphabet);
	return std::format("({})*.(({}).({})*)*", id, regex, id);
}

inline std::ostream &operator<<(std::ostream &out, const Regex &regex) {
	regex.print(out);
	return out;
}

inline std::ostream &operator<<(std::ostream &out, std::unique_ptr<Regex> &regex) {
	if (regex) {
		regex->print(out);
	} else {
		out << "nullptr";
	}
	return out;
}

};	   // namespace rgx
