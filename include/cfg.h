#pragma once

#include <functional>
#include <vector>
#include <iostream>
#include <cassert>

#include "concepts.hpp"
#include "hashing.hpp"
#include "datastructures.hpp"
#include "formatting.hpp"

namespace fl {

/**
 * @brief The right side of a production rule in a Context-Free Grammar
 * @tparam Symbol - Type of symbols in the alphabet
 */
template <symbol Symbol>
struct Production {
	std::vector<Symbol> rhs;
	std::vector<bool>	ignore;			 // whether to ignore the symbol in a parsing tree
	Symbol				replaceWith;	 // if != eps, replace the non-terminal that produced this production
										 // with this symbol in the AST

	Production(std::vector<Symbol> &&rhs) : rhs(std::move(rhs)), ignore(rhs.size(), false), replaceWith(Symbol::eps) {}
	Production(const std::vector<Symbol> &rhs) : rhs(rhs), ignore(rhs.size(), false), replaceWith(Symbol::eps) {}

	Production(std::vector<Symbol> &&rhs, const std::vector<bool> &ignore, Symbol replaceWith = Symbol::eps)
		: rhs(std::move(rhs)), ignore(ignore), replaceWith(replaceWith) {}

	Production(const std::vector<Symbol> &rhs, const std::vector<bool> &ignore, Symbol replaceWith = Symbol::eps)
		: rhs(rhs), ignore(ignore), replaceWith(replaceWith) {}

	auto				 begin() const { return rhs.begin(); }
	auto				 end() const { return rhs.end(); }
	auto				 empty() const { return rhs.empty(); }
	auto				 size() const { return rhs.size(); }
	auto				 operator[](std::size_t i) const { return rhs[i]; }
	friend std::ostream &operator<<(std::ostream &os, const Production &p) { return os << p.rhs; }

	auto push_back(const Symbol &l) {
		ignore.push_back(false);
		return rhs.push_back(l);
	}
};

template <symbol Symbol>
Production(std::vector<Symbol> &&) -> Production<Symbol>;
template <symbol Symbol>
Production(const std::vector<Symbol> &) -> Production<Symbol>;
template <symbol Symbol>
Production(std::vector<Symbol> &&, const std::vector<bool> &, Symbol) -> Production<Symbol>;
template <symbol Symbol>
Production(const std::vector<Symbol> &, const std::vector<bool> &, Symbol) -> Production<Symbol>;

/**
 * @brief A Context-Free Grammar
 *
 * @tparam Symbol - Type of symbols in the alphabet
 */
template <symbol Symbol>
class CFG {
   public:
	using Production = struct Production<Symbol>;
	using Rule		 = std::pair<Symbol, Production>;

	struct NonTerminalData {
		int	 upwardSpillThreshold = -1;		// if >= 0, the non-terminal will disappear in the AST if it has <= children
		bool ignoreEmpty		  = true;
		bool ignoreSingleChild	  = true;
	};

	fl::unordered_map<Symbol, NonTerminalData> nonTerminalData;

	fl::unordered_multimap<Symbol, Production> rules;
	fl::unordered_set<Symbol>				   terminals;
	fl::unordered_set<Symbol>				   nonTerminals;
	Symbol									   start;
	Symbol									   eof = Symbol::eof;

   public:
	CFG(const Symbol &start, const Symbol &eof) : start(start), eof(eof) {
		terminals.insert(eof);
		nonTerminals.insert(start);
	}

	/**
	 * @brief computes if symbols are nullable
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 * @return fl::unordered_map<Symbol, bool>
	 */
	fl::unordered_map<Symbol, bool> findNullables() const {
		fl::unordered_map<Symbol, bool> res;

		for (const auto &[k, v] : rules) {
			if (v.empty()) { res.insert({k, true}); }
		}
		for (Symbol l : terminals) {
			res.insert({l, false});
		}

		std::size_t prevSize = 0;
		while (prevSize != res.size()) {
			prevSize = res.size();

			for (const auto &[k, v] : rules) {
				if (res.contains(k)) continue;

				bool isNullable = true;
				bool isKnown	= true;
				for (Symbol l : v) {
					bool contains = res.contains(l);
					if (contains && !res.find(l)->second) {
						res.insert({k, false});
						isNullable = false;
						break;
					}
					isKnown = isKnown && contains;
				}
				if (isNullable && isKnown) { res.insert({k, true}); }
			}
		}

		return std::move(res);
	}

	/**
	 * @brief computes if epsilon can be derived from a word \ref{w}.
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 *
	 * @param w - word
	 * @param nullable - the result from CFG::findNullables
	 * @return true - if the above statement is true
	 * @return false - else
	 */
	bool nullable(const std::vector<Symbol> &w, const fl::unordered_map<Symbol, bool> &nullable) const {
		if (w.empty()) return true;
		bool ans = true;
		for (const auto x : w) {
			ans = ans && nullable.find(x)->second;
		}
		return ans;
	}

	/**
	 * @brief finds the FIRST set for all symbols in the grammar
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 *
	 * @param nullable - the result from CFG::findNullables
	 * @return fl::unordered_map<Symbol, fl::unordered_set<Symbol>>
	 */
	fl::unordered_map<Symbol, fl::unordered_set<Symbol>> findFirsts(
		const fl::unordered_map<Symbol, bool> &nullable) const {
		fl::unordered_map<Symbol, fl::unordered_set<Symbol>> first;

		for (Symbol l : terminals) {
			first.insert({l, {l}});
		}
		for (Symbol l : nonTerminals) {
			first.insert({l, {}});
		}

		bool change = true;
		while (change) {
			change = false;
			for (const auto &[k, v] : rules) {
				auto &firstSet = first.find(k)->second;
				if (!v.empty() && first.contains(v[0])) {
					for (const auto l : first.find(v[0])->second) {
						if (!firstSet.contains(l)) {
							firstSet.insert(l);
							change = true;
						}
					}
				}
				for (size_t i = 0; i < v.size(); ++i) {
					if (i)
						for (const auto l : first.find(v[i])->second) {
							if (!firstSet.contains(l)) {
								firstSet.insert(l);
								change = true;
							}
						}
					if (!nullable.find(v[i])->second) break;
				}
			}
		}

		return first;
	}
	/**
	 * @brief checks if x is in the FIRST set for the word w
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 *
	 * @param x
	 * @param w
	 * @param nullable
	 * @param first
	 * @return true
	 * @return false
	 */
	bool isFirst(Symbol x, const std::vector<Symbol> &w, const fl::unordered_map<Symbol, bool> &nullable,
				 const fl::unordered_map<Symbol, fl::unordered_set<Symbol>> &first) const {
		for (size_t i = 0; i < w.size(); ++i) {
			if (first.find(w[i])->second.contains(x)) return true;
			if (!nullable.find(w[i])->second) break;
		}
		return false;
	}

	/**
	 * @brief computes the FIRST set for a word w
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 * @param w
	 * @param nullable
	 * @param first
	 * @return fl::unordered_set<Symbol>
	 */
	fl::unordered_set<Symbol> first(const std::vector<Symbol> &w, const fl::unordered_map<Symbol, bool> &nullable,
									const fl::unordered_map<Symbol, fl::unordered_set<Symbol>> &first) const {
		fl::unordered_set<Symbol> res;
		for (size_t i = 0; i < w.size(); ++i) {
			const auto &firstOfLetter = first.find(w[i])->second;
			for (const auto l : firstOfLetter) {
				res.insert(l);
			}
			if (!nullable.find(w[i])->second) break;
		}
		return res;
	}

	/**
	 * @brief Computes the FOLLOW set for all symbols in the grammar
	 * http://ll1academy.cs.ucla.edu/static/LLParsing.pdf
	 * @param nullable
	 * @param first
	 * @return fl::unordered_map<Symbol, fl::unordered_set<Symbol>>
	 */
	fl::unordered_map<Symbol, fl::unordered_set<Symbol>> findFollows(
		const fl::unordered_map<Symbol, bool>					   &nullable,
		const fl::unordered_map<Symbol, fl::unordered_set<Symbol>> &first) const {
		fl::unordered_map<Symbol, fl::unordered_set<Symbol>> follow;

		for (Symbol l : nonTerminals) {
			follow.insert({l, {}});
		}

		follow.find(start)->second.insert(eof);

		bool change = true;
		while (change) {
			change = false;
			for (const auto &[A, v] : rules) {
				for (size_t i = 0; i < v.size(); ++i) {
					if (!nonTerminals.contains(v[i])) continue;
					// productions of type A -> aBb
					// where A and B are nonterminals and a and b are words
					auto	   &followB = follow.find(v[i])->second;
					const auto	b		= std::vector<Symbol>{v.begin() + i + 1, v.end()};
					const auto &firstb	= this->first(b, nullable, first);

					if (!b.empty()) {
						for (const auto l : firstb) {
							if (!followB.contains(l)) {
								followB.insert(l);
								change = true;
							}
						}
					}

					if (this->nullable(b, nullable)) {
						const auto followA = follow.find(A)->second;
						for (const auto l : followA) {
							if (!followB.contains(l)) {
								followB.insert(l);
								change = true;
							}
						}
					}
				}
			}
		}

		return follow;
	}

	CFG() {}

	/**
	 * @brief Computes and prints the parse table to stdout
	 */
	void printParseTable() const {
		for (const auto l : nonTerminals) {
			std::cout << l << "\t";
			if (nonTerminalData.contains(l)) {
				const auto &d = nonTerminalData.find(l)->second;
				std::cout << "[spill threshold: " << d.upwardSpillThreshold << "]";
			} else {
				std::cout << "[no data]";
			}
			std::cout << std::endl;
		}

		const auto nullable = findNullables();
		const auto first	= findFirsts(nullable);
		const auto follow	= findFollows(nullable, first);

		for (const auto &[A, v] : rules) {
			if (v.empty()) {
				const auto &followA = follow.find(A)->second;
				for (const auto l : followA) {
					std::cout << A << ", " << l << " ~~> " << A << " -> " << Symbol::eps << std::endl;
				}
			} else {
				const auto &firstA = this->first(v.rhs, nullable, first);
				for (const auto l : firstA) {
					std::cout << A << ", " << l << " ~~> " << A << " -> " << v << std::endl;
				}
			}
		}
	}

	/**
	 * @brief Prints all rules of the grammar to stdout
	 */
	void printRules() const {
		for (const auto &[A, v] : rules) {
			if (v.empty()) std::cout << A << " -> " << Symbol::eps << std::endl;
			else std::cout << A << " -> " << v << std::endl;
		}
	}

	/**
	 * @brief Adds the rule (a -> w) to the grammar, where a is a nonterminal symbol and w is a word
	 *
	 * @param a - nonterminal
	 * @param w - a word
	 */
	void addRule(Symbol a, const Production &w) {
		if (!nonTerminals.contains(a)) { nonTerminals.insert(a); }
		if (!nonTerminalData.contains(a)) { nonTerminalData.insert({a, NonTerminalData()}); }
		rules.insert({a, w});
	}
	void addRule(Symbol a, const std::vector<Symbol> &w) { addRule(a, Production(w)); }

	/**
	 * @brief adds the rule (a -> w) to the grammar. Available only if \ref{Symbol} can be implicitly constructed from
	 * char
	 *
	 * @tparam U - dummy
	 * @param a - a nonterminal symbol
	 * @param w - a word
	 * @return requires
	 */
	template <class U = Symbol>
		requires std::is_convertible_v<char, Symbol>
	void addRule(char a, const std::string &w) {
		addRule(a, std::vector<Symbol>(w.begin(), w.end()));
	}

	std::vector<Symbol> generate(std::size_t min, std::size_t max) const {
		auto nullables = findNullables();

		auto [v, b] = generate(max, start, nullables);
		while (!b || v.size() < min) {
			std::tie(v, b) = generate(max, start, nullables);
			std::cout << v << std::endl;
		}
		return v;
	}

	std::pair<std::vector<Symbol>, bool> generate(std::size_t max, Symbol l,
												  fl::unordered_map<Symbol, bool, fl::hash<Symbol>> &nullables) const {
		if (max <= 0) return {{}, false};

		bool isNullable = nullables[l];
		if (isNullable) {
			if (rand() % 2) return {{}, true};
		}

		auto [b, e] = rules.equal_range(l);
		auto size	= std::distance(b, e) - isNullable;

		int val = (rand() + 1) % size;
		std::advance(b, val);
		auto [_, w] = *b;

		std::vector<Symbol> result;

		for (const Symbol &l : w) {
			if (nonTerminals.contains(l)) {
				auto [v, b] = generate(max - result.size(), l, nullables);
				if (!b) return {{}, false};
				result.insert(result.end(), v.begin(), v.end());
			} else {
				result.push_back(l);
			}
		}

		return {result, true};
	}

	auto &getNonTerminalData(Symbol l) {
		assert(nonTerminals.contains(l));
		return nonTerminalData[l];
	}

	const auto &getNonTerminalData(Symbol l) const {
		assert(nonTerminals.contains(l));
		assert(nonTerminalData.contains(l));
		return nonTerminalData.find(l)->second;
	}

	void setIgnoreEmpty(bool ignore) { getNonTerminalData(start).ignoreEmpty = ignore; }

	void setUpwardSpillThreshold(int threshold) { getNonTerminalData(start).upwardSpillThreshold = threshold; }

	void setIgnoreSingleChild(bool ignore) { getNonTerminalData(start).ignoreSingleChild = ignore; }
};
}	  // namespace fl
