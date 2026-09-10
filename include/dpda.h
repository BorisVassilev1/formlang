#pragma once

#include <format>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <ranges>

#include "cfg.h"
#include "concepts.hpp"
#include "hashing.hpp"
#include "debug.hpp"

namespace fl {
/**
 * @brief A Deterministic PushDown Automaton
 *
 * @tparam State - type of the states
 * @tparam Letter - type of the Symbols in the alphabet
 */
template <state State = std::size_t, symbol Letter = char>
class DPDA {
   public:
	using Production = typename CFG<Letter>::Production;
	using DeltaMap	 = std::unordered_map<std::tuple<State, Letter, Letter>, std::tuple<State, Production>,
										  fl::hash<std::tuple<State, Letter, Letter>>>;
	DeltaMap delta;		/// the transition function

	State qFinal	   = 0;			// the final state
	bool  enable_print = false;		// if true, the DPDA will print debug info while parsing strings
   protected:
	void printState(State state, size_t offset, const std::vector<Letter> stack,
					const std::vector<Letter> &word) const {
		std::cout << "(" << state << " , ";
		if (offset < word.size()) {
			for (auto a = word.begin() + offset; a != word.end(); ++a) {
				std::cout << *a;
			}
		}
		std::cout << " , ";
		for (const auto a : stack) {
			std::cout << a;
		}
		std::cout << ')' << std::endl;
	}

	DeltaMap::const_iterator transition(State &q, Letter a, Letter top, std::vector<Letter> &stack,
										std::size_t &offset) const {
		std::tuple<State, Letter, Letter> search = {q, a, top};
		auto							  res	 = delta.find(search);
		if (res == delta.end()) { return res; }

		if (top != Letter::eps) stack.pop_back();
		for (const auto &l : std::ranges::views::reverse(std::get<1>(res->second))) {
			stack.push_back(l);
		}
		q = std::get<0>(res->second);
		if (a != Letter::eps) ++offset;
		return res;
	}

   public:
	DPDA() {}

	/**
	 * @brief Dumps all the transitions to stdout
	 */
	void printTransitions() const {
		for (const auto &t : delta) {
			std::cout << t << std::endl;
		}
	}

	/**
	 * @brief Runs the automaton on a word and returns true if it is recognized, false otherwise
	 *
	 * @param word
	 * @return true
	 * @return false
	 */
	bool recognize(const std::vector<Letter> &word) const {
		std::size_t						  offset = 0;
		std::vector<Letter>				  stack;
		State							  current_state = 0;
		typename DeltaMap::const_iterator res			= delta.begin();

		while ((current_state != qFinal || !stack.empty()) && res != delta.end()) {
			const Letter &l = offset < word.size() ? word[offset] : Letter::eps;
			const Letter &s = stack.empty() ? Letter::eps : stack.back();
			if (enable_print) { printState(current_state, offset, stack, word); }

			res = transition(current_state, l, s, stack, offset);
			if (res == delta.end() && s != Letter::eps) {
				res = transition(current_state, l, Letter::eps, stack, offset);
			}
			if (res == delta.end() && l != Letter::eps) {
				res = transition(current_state, Letter::eps, s, stack, offset);
			}
			if (res == delta.end() && s != Letter::eps && l != Letter::eps) {
				res = transition(current_state, Letter::eps, Letter::eps, stack, offset);
			}
		}
		if (enable_print) { printState(current_state, offset, stack, word); }

		bool accepted = current_state == qFinal && stack.empty() && offset == word.size();
		if (enable_print) {
			std::cout << std::format("current_state: {}, offset: {}, stack empty: {}, word size: {}, accepted: {}\n",
									 current_state, offset, stack.empty(), word.size(), accepted);
		}

		return accepted;
	}

	/**
	 * @brief Same as the other implementation, but for std::string
	 *
	 * @tparam U - dummy
	 * @param word
	 * @return requires
	 */
	template <typename U = Letter>
		requires std::is_constructible_v<Letter, char>
	bool recognize(const std::string &word) const {
		std::vector<Letter> w;
		for (std::size_t i = 0; i < word.size(); ++i) {
			w.push_back(Letter(word[i]));
		}
		return recognize(w);
	}

	/**
	 * @brief adds the transition ((q, a, x), (q1, w)) to the automaton
	 *
	 * @param q
	 * @param a
	 * @param x
	 * @param q1
	 * @param w
	 */
	void addTransition(State q, Letter a, Letter x, State q1, const Production &w) {
		auto to_insert = std::make_pair(std::tuple<State, Letter, Letter>{q, a, x}, std::make_tuple(q1, w));
		auto found	   = delta.find(to_insert.first);
		if (found != delta.end()) {
			// throw std::runtime_error(
			// 	std::format("Failed to add transition: New transition {} conflicts with {}", to_insert, *found));

			dbLog(dbg::LOG_ERROR,
				  std::format("Failed to add transition: New transition {} conflicts with {}", to_insert, *found));
		}
		delta.insert(to_insert);
	}

	template <class U = std::vector<Letter> &&>
		requires std::is_convertible_v<typename std::remove_reference_t<U>::value_type, Letter>
	void addTransition(State q, Letter a, Letter x, State q1, U &&w) {
		return addTransition(q, a, x, q1, Production(std::forward<U>(w)));
	}

	/**
	 * @brief Same as other impklementation, but for std::string
	 *
	 * @tparam U
	 * @param q
	 * @param a
	 * @param x
	 * @param q1
	 * @param w
	 * @return requires
	 */
	template <class U = Letter>
		requires std::is_convertible_v<Letter, char>
	void addTransition(State q, Letter a, Letter x, State q1, const std::string &w) {
		return addTransition(q, a, x, q1, std::vector<Letter>(w.begin(), w.end()));
	}

	/**
	 * @brief Prints the automaton in the DOT language for graphviz to draw it
	 *
	 * @param out
	 */
	void printAsDOT(std::ostream &out) const {
		out << "digraph {\n overlap = false; splines = true; nodesep = 0.3; layout = dot;\n";
		out << "node [shape=doublecircle];\"" << qFinal << "\";\nnode [shape=circle];\n";
		for (const auto &[u, v] : delta) {
			const auto &[q1, a, x] = u;
			const auto &[q2, w]	   = v;
			out << "\t\"" << q1 << "\" -> \"" << q2 << "\" [xlabel = \"" << a << ", " << x;
			if (!w.empty()) { out << " / " << w; }
			out << "\" , minlen = \"3\"];" << std::endl;
		}
		out << "}";
	}
};
}	  // namespace fl
