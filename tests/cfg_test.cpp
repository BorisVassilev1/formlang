#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"

#include <set>
#include <string>
#include <tuple>

#include "cfg.hpp"
#include "letter.hpp"

using namespace fl;

namespace {

std::string toStr(const std::vector<Letter> &rhs) {
	std::string s;
	for (Letter l : rhs) s.push_back((char)l);
	return s;
}

// Entry: (nonterminal, lookahead, production rhs as a string; "" means the epsilon production)
using Entry = std::tuple<char, char, std::string>;

std::set<Entry> parseTable(const CFG<Letter> &g) {
	const auto nullable = g.findNullables();
	const auto first	= g.findFirsts(nullable);
	const auto follow	= g.findFollows(nullable, first);

	std::set<Entry> table;
	for (const auto &[A, v] : g.rules) {
		if (v.empty()) {
			for (Letter l : follow.find(A)->second) table.insert({(char)A, (char)l, ""});
		} else {
			for (Letter l : g.first(v.rhs, nullable, first)) table.insert({(char)A, (char)l, toStr(v.rhs)});
		}
	}
	return table;
}

}	 // namespace

TEST_CASE("nullable is true if ANY production of a nonterminal is nullable") {
	// S -> a | B
	// B -> eps
	// S is nullable (via S -> B -> eps) even though S also has the non-nullable production S -> a.
	// Whether this bug reproduces depends on which of S's two productions the unordered_multimap
	// iterates first, so both insertion orders are exercised.
	SUBCASE("non-nullable production inserted before the nullable one") {
		CFG<Letter> g;
		g.terminals	   = {'a', '#'};
		g.nonTerminals = {'S', 'B'};
		g.addRule('S', "a");
		g.addRule('S', "B");
		g.addRule('B', "");
		g.start = 'S';
		g.eof	= '#';

		auto nullable = g.findNullables();
		CHECK(nullable.find('B')->second == true);
		CHECK(nullable.find('S')->second == true);
	}

	SUBCASE("nullable production inserted before the non-nullable one") {
		CFG<Letter> g;
		g.terminals	   = {'a', '#'};
		g.nonTerminals = {'S', 'B'};
		g.addRule('B', "");
		g.addRule('S', "B");
		g.addRule('S', "a");
		g.start = 'S';
		g.eof	= '#';

		auto nullable = g.findNullables();
		CHECK(nullable.find('B')->second == true);
		CHECK(nullable.find('S')->second == true);
	}
}

TEST_CASE("LL(1) parse table of the dot/plus expression grammar") {
	CFG<Letter> g;
	g.terminals	   = {'i', '(', ')', '.', '+', '#'};
	g.nonTerminals = {'e', 'E', 't', 'T', 'f'};
	g.addRule('e', "tE");
	g.addRule('E', "");
	g.addRule('E', "+tE");
	g.addRule('t', "fT");
	g.addRule('T', "");
	g.addRule('T', ".fT");
	g.addRule('f', "(e)");
	g.addRule('f', "i");
	g.start = 'e';
	g.eof	= '#';

	const std::set<Entry> expected = {
		{'e', 'i', "tE"},
		{'e', '(', "tE"},
		{'E', '+', "+tE"},
		{'E', '#', ""	},
		{'E', ')', ""	},
		{'t', 'i', "fT"},
		{'t', '(', "fT"},
		{'T', '.', ".fT"},
		{'T', '+', ""	},
		{'T', '#', ""	},
		{'T', ')', ""	},
		{'f', '(', "(e)"},
		{'f', 'i', "i"	},
	};

	CHECK(parseTable(g) == expected);
}
