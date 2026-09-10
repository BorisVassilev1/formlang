#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <ratio>
#include <stack>
#include <unordered_map>

#include "dpda.h"
#include "state.hpp"
#include "utils.h"
#include "cfg.h"
#include "formatting.hpp"
#include "hashing.hpp"

namespace fl {

/**
 * @brief A node in a parse tree
 *
 * @tparam Symbol - type of the symbols in the alphabet
 */
template <symbol Symbol>
struct ParseNode {
	Symbol											value;
	std::vector<std::unique_ptr<ParseNode<Symbol>>> children;

	ParseNode(const Symbol value) : value(value) {}
	ParseNode(const Symbol value, std::vector<std::unique_ptr<ParseNode<Symbol>>> &&children)
		: value(value), children(std::move(children)) {}
};

/**
 * @brief A custom exception type for errors, emitted while a DPDA is parsing a string
 */
class ParseError : public std::exception {
   protected:
	std::string msg;
	std::size_t position;

   public:
	ParseError(const std::string &msg, std::size_t position)
		: msg(std::format("{} at {}", msg, position)), position(position) {}
	ParseError(const char *msg, std::size_t position)
		: msg(std::string(msg) + " at " + std::to_string(position)), position(position) {}

	virtual ~ParseError() noexcept {}

	virtual const char *what() const noexcept { return msg.c_str(); }
};

template <class Symbol>
inline std::size_t getLengthOfTokens(const std::vector<Symbol> &word, std::size_t offset, std::size_t length) {
	std::string s;
	for (std::size_t i = 0; i < length; ++i) {
		s += std::format("{}", word[offset + i]);
	}
	return s.size();
}

template <symbol Symbol>
class Parser : public DPDA<State<Symbol>, Symbol> {
   protected:
	using LState = State<Symbol>;
	using typename DPDA<LState, Symbol>::DeltaMap;
	using DPDA<LState, Symbol>::printState;
	using DPDA<LState, Symbol>::addTransition;
	using DPDA<LState, Symbol>::transition;

	CFG<Symbol> g;

	const fl::unordered_map<Symbol, bool>					   nullable;
	const fl::unordered_map<Symbol, fl::unordered_set<Symbol>> first;
	const fl::unordered_map<Symbol, fl::unordered_set<Symbol>> follow;

	void detectMistake(const std::vector<Symbol> &word, std::size_t offset, const std::vector<Symbol> &stack,
					   LState current_state) const {
		size_t		position = offset > 0 ? offset - 1 : 0;
		std::string msg;
		Symbol		currentLetter = Symbol(current_state - Symbol::size);

		// dbLog(dbg::LOG_ERROR, "-------------");
		// printState(current_state, offset, stack, word);
		// dbLog(dbg::LOG_ERROR, "-------------");

		if (current_state == 1 && !g.terminals.contains(word[offset])) {
			msg = std::format("unexpected '{}' - not a valid terminal", word[offset]);
		} else if (!stack.empty() && current_state > Symbol::size) {
			fl::unordered_set<Symbol> expected;
			auto					 &firsts = *this->first.find(stack.back());
			expected						 = firsts.second;
			if (this->nullable.find(stack.back())->second) {
				auto &follows = *this->follow.find(stack.back());
				expected.insert(follows.second.begin(), follows.second.end());
			}
			auto filtered = expected | std::views::filter([&](const Symbol l) { return g.terminals.contains(l); });

			msg = std::format("expected one of {}, but got '{}'", filtered, currentLetter);
		} else if (stack.empty() && offset == word.size()) {
			msg = std::format("unexpected end of file");
		} else if (current_state > Symbol::size) {
			msg = std::format("unexpected {}", currentLetter);
		} else {
			msg = "unknown error";
		}
		msg += "\n";

		for (size_t i = std::max((int)position - 5, 0); i < std::min(position + 5, word.size()); ++i) {
			msg += std::format("{}", word[i]);
		}

		int			tokensBefore = std::min((int)position, 5);
		std::size_t len			 = getLengthOfTokens(word, position - tokensBefore, tokensBefore);
		// this does not work on clang 20.1.6 c++26
		// msg += std::format("\n{: >{}}", '^', len + 1);
		msg += "\n";
		for (size_t i = 0; i < len; ++i) {
			msg += " ";
		}
		msg += '^';

		throw ParseError(msg, position);
	}

   public:
	using DPDA<LState, Symbol>::delta;
	using DPDA<LState, Symbol>::qFinal;
	using DPDA<LState, Symbol>::enable_print;
	using DPDA<LState, Symbol>::printTransitions;

	const auto &getFirst() const { return first; }
	const auto &getFollow() const { return follow; }
	const auto &getNullable() const { return nullable; }

	/**
	 * @brief Construct a Parser from an LL(1) grammar. Throws if grammar is not LL(1)
	 *
	 * @param grammar
	 */
	Parser(const CFG<Symbol> &grammar)
		: g(grammar),
		  nullable(grammar.findNullables()),
		  first(grammar.findFirsts(nullable)),
		  follow(grammar.findFollows(nullable, first)) {
		bool intersection = false;
		for (const auto l : grammar.terminals) {
			if (grammar.nonTerminals.contains(l)) {
				intersection = true;
				break;
			}
		}
		for (const auto l : grammar.nonTerminals) {
			if (grammar.terminals.contains(l)) {
				intersection = true;
				break;
			}
		}
		if (intersection) {
			throw std::runtime_error("Grammar is not LL(1): intersection between terminals and nonterminals");
		}

		addTransition(0, Symbol::eps, Symbol::eps, 1, std::vector<Symbol>{grammar.start});

		auto f = [](const Symbol l) -> LState { return Symbol::size + std::size_t(l); };

		try {
			for (const auto &[A, v] : grammar.rules) {
				if (v.empty()) {
					const auto &followA = follow.find(A)->second;
					for (const auto l : followA) {
						addTransition(f(l), Symbol::eps, A, f(l), v);
					}
				} else {
					const auto &firstA = grammar.first(v.rhs, nullable, first);
					for (const auto l : firstA) {
						addTransition(f(l), Symbol::eps, A, f(l), v);
					}
				}
			}

			for (const auto l : grammar.terminals) {
				addTransition(1, l, Symbol::eps, f(l), {});
				if (l != grammar.eof) addTransition(f(l), Symbol::eps, l, 1, {});
			}
		} catch (...) { std::throw_with_nested(std::runtime_error("Failed to create parser for grammar")); }

		qFinal = f(grammar.eof);
	}

	/**
	 * @brief Constructs a parse tree from a word and a series of productions
	 *
	 * @param productions
	 * @param word
	 * @return requires&&
	 */
	std::unique_ptr<ParseNode<Symbol>> makeParseTree(
		const std::vector<std::reference_wrapper<const typename DeltaMap::value_type>> &productions,
		const std::vector<Symbol>													   &word) const {
		// stack implementation
		struct ParsingState {
			std::unique_ptr<ParseNode<Symbol>> node;
			unsigned int					   output_index;	 // where to continue from when backtracking
			unsigned int					   prod_index;		 // index of production generating that node
		};

		std::stack<ParsingState> parseStack;
		parseStack.push({std::make_unique<ParseNode<Symbol>>(g.start), 0, 0});
		unsigned int word_position	 = 0;	  // position in output_word
		unsigned int next_production = 0;	  // index of production to use

		while (word_position < word.size()) {
			auto &[topNode, idx, prod_index] = parseStack.top();
			const auto &[from, to]			 = productions[prod_index].get();
			const auto &[_, product]		 = to;

			if (idx == product.size()) {
				if (product.empty()) topNode->children.push_back(std::make_unique<ParseNode<Symbol>>(Symbol::eps));

				if (parseStack.size() == 1) break;
				auto [childNode, _, _] = std::move(parseStack.top());
				parseStack.pop();

				auto &[topNode, idx, prod_index] = parseStack.top();
				topNode->children.emplace_back(std::move(childNode));
				++idx;

				continue;
			}

			if (product[idx] == word[word_position]) {	   // terminal
				topNode->children.push_back(std::make_unique<ParseNode<Symbol>>(word[word_position]));
				++word_position;
				++idx;
			} else {	 // non-terminal
				parseStack.push({std::make_unique<ParseNode<Symbol>>(product[idx]), 0, ++next_production});
			}
		}

		return std::move(parseStack.top().node);
	}

	std::unique_ptr<ParseNode<Symbol>> makeAST(
		const std::vector<std::reference_wrapper<const typename DeltaMap::value_type>> &productions,
		const std::vector<Symbol>													   &word) const {
		// stack implementation
		struct ParsingState {
			std::unique_ptr<ParseNode<Symbol>> node;
			unsigned int					   output_index;	 // where to continue from when backtracking
			unsigned int					   prod_index;		 // index of production generating that node
		};

		std::stack<ParsingState> parseStack;
		parseStack.push({std::make_unique<ParseNode<Symbol>>(g.start), 0, 0});
		unsigned int word_position	 = 0;	  // position in output_word
		unsigned int next_production = 0;	  // index of production to use

		while (word_position < word.size()) {
			auto &[topNode, idx, prod_index] = parseStack.top();
			const auto &[from, to]			 = productions[prod_index].get();
			const auto &[_, _, A]			 = from;
			const auto &[_, product]		 = to;

			if (idx == product.size()) {
				if (parseStack.size() == 1) break;
				auto [childNode, _, _] = std::move(parseStack.top());
				parseStack.pop();

				const auto &NTData				 = g.getNonTerminalData(A);
				auto &[topNode, idx, prod_index] = parseStack.top();

				// throw out the node if it is empty and we ignore empty
				if (NTData.ignoreEmpty && childNode->children.size() == 0) {
					++idx;
					continue;
				}
				// throw out the node if it has a single child and we ignore single child
				if (NTData.ignoreSingleChild && childNode->children.size() == 1) {
					if (product.replaceWith != Symbol::eps) { topNode->value = product.replaceWith; }

					topNode->children.emplace_back(std::move(childNode->children[0]));
					++idx;
					continue;
				}

				if (NTData.upwardSpillThreshold < 0 ||
					childNode->children.size() > size_t(NTData.upwardSpillThreshold)) {
					if (product.replaceWith != Symbol::eps) { childNode->value = product.replaceWith; }
					topNode->children.emplace_back(std::move(childNode));
				} else {	 // spill upwards if we have few children
					for (auto &c : childNode->children) {
						topNode->children.emplace_back(std::move(c));
					}
				}
				++idx;

				continue;
			}

			if (product[idx] == word[word_position]) {	   // terminal
				if (!product.ignore[idx])
					topNode->children.push_back(std::make_unique<ParseNode<Symbol>>(word[word_position]));
				++word_position;
				++idx;
			} else {	 // non-terminal
				parseStack.push({std::make_unique<ParseNode<Symbol>>(product[idx]), 0, ++next_production});
			}
		}

		return std::move(parseStack.top().node);
	}

	using ProductionVector = std::vector<std::reference_wrapper<const typename DeltaMap::value_type>>;

	std::pair<bool, ProductionVector> generateProductions(const std::vector<Symbol> &word) const {
		std::size_t																 offset = 0;
		std::vector<Symbol>														 stack;
		LState																	 current_state = 0;
		typename DeltaMap::const_iterator										 res		   = delta.begin();
		std::vector<std::reference_wrapper<const typename DeltaMap::value_type>> productions;

		while ((current_state != qFinal || !stack.empty()) && res != delta.end()) {
			const Symbol &l = offset < word.size() ? word[offset] : Symbol::eps;
			const Symbol &s = stack.empty() ? Symbol::eps : stack.back();
			if (enable_print) { printState(current_state, offset, stack, word); }

			res = transition(current_state, l, s, stack, offset);
			if (res == delta.end() && s != Symbol::eps) {
				res = transition(current_state, l, Symbol::eps, stack, offset);
			}
			if (res == delta.end() && l != Symbol::eps) {
				res = transition(current_state, Symbol::eps, s, stack, offset);
			}
			if (res == delta.end() && s != Symbol::eps && l != Symbol::eps) {
				res = transition(current_state, Symbol::eps, Symbol::eps, stack, offset);
			}

			if (res != delta.end() && std::get<0>(res->first) == std::get<0>(res->second))
				productions.push_back(std::ref(*res));
		}
		if (enable_print) { printState(current_state, offset, stack, word); }

		bool accepted = current_state == qFinal && stack.empty() && offset == word.size();

		if (!accepted) { detectMistake(word, offset, stack, current_state); }
		return {accepted, productions};
	}

	/**
	 * @brief parses a word and builds its parse tree. If it fails, throws a ParseError
	 *
	 * @param word
	 * @return std::unique_ptr<ParseNode<Symbol>>
	 */
	std::unique_ptr<ParseNode<Symbol>> parse(const std::vector<Symbol> &word) const {
		auto start					 = std::chrono::high_resolution_clock::now();
		auto [accepted, productions] = generateProductions(word);
		auto end					 = std::chrono::high_resolution_clock::now();
		if (!accepted) { return nullptr; }
		dbLog(dbg::LOG_DEBUG, "productions done: ", productions.size(), " in ",
			  std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), "ms");

		auto parseTree = makeParseTree(productions, word);

		return parseTree;
	}

	std::unique_ptr<ParseNode<Symbol>> ASTparse(const std::vector<Symbol> &word) const {
		auto start					 = std::chrono::high_resolution_clock::now();
		auto [accepted, productions] = generateProductions(word);
		auto end					 = std::chrono::high_resolution_clock::now();
		if (!accepted) { return nullptr; }
		dbLog(dbg::LOG_DEBUG, "productions done: ", productions.size(), " in ",
			  std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), "ms");

		auto parseTree = makeAST(productions, word);

		return parseTree;
	}

	/**
	 * @brief same as the other method, but for strings
	 *
	 * @tparam U
	 * @param word
	 * @return std::unique_ptr<ParseNode<Symbol>>
	 */
	template <typename U = Symbol>
	std::unique_ptr<ParseNode<Symbol>> parse(const std::string &word) const {
		std::vector<Symbol> w;
		for (std::size_t i = 0; i < word.size(); ++i) {
			w.push_back(Symbol(word[i]));
		}
		if (word.back() != Symbol::eof) w.push_back(Symbol::eof);
		return parse(w);
	}

	template <typename U = Symbol>
	std::unique_ptr<ParseNode<Symbol>> ASTparse(const std::string &word) const {
		std::vector<Symbol> w;
		for (std::size_t i = 0; i < word.size(); ++i) {
			w.push_back(Symbol(word[i]));
		}
		if (word.back() != Symbol::eof) w.push_back(Symbol::eof);
		return ASTparse(w);
	}

	const CFG<Symbol> &getGrammar() const { return g; }
};

// reworked version of
// https://hbfs.wordpress.com/2016/12/06/pretty-printing-a-tree-in-a-terminal/
namespace {
using bits = std::vector<bool>;

static inline void p_tabs(std::ostream &out, const bits &b) {
	for (auto x : b)
		out << (x ? " \u2502" : "  ");
}

template <class Symbol>
static inline void printFunctionDefault(std::ostream &out, const ParseNode<Symbol> *r) {
	out << "-" << r->value << std::endl;
}

template <class Node>
static void p_show(std::ostream &out, const Node *r, bits &b,
				   std::function<void(std::ostream &, const Node *)> printFunction = printFunctionDefault) {
	// https://en.wikipedia.org/wiki/Box-drawing_character
	if (r) {
		printFunction(out, r);
		// out << "-" << r->value << std::endl;

		for (size_t i = 0; i + 1 < r->children.size(); ++i) {
			p_tabs(out, b);
			out << " \u251c";	  // ├
			b.push_back(true);
			p_show(out, r->children[i].get(), b, printFunction);
			b.pop_back();
		}

		if (!r->children.empty()) {
			p_tabs(out, b);
			out << " \u2514";	  // └
			b.push_back(false);
			p_show(out, r->children.back().get(), b, printFunction);
			b.pop_back();
		}
	} else out << " \u25cb" << std::endl;	  // ○
}
}	  // namespace

template <class Symbol>
std::ostream &operator<<(std::ostream &out, const std::unique_ptr<ParseNode<Symbol>> &node) {
	bits b;
	p_show<ParseNode<Symbol>>(out, node.get(), b, printFunctionDefault<Symbol>);
	return out;
}
}	  // namespace fl

template <fl::symbol Symbol>
struct std::formatter<fl::ParseNode<Symbol>> : fl::ostream_formatter {};
