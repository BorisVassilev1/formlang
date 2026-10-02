#include <iostream>
#include <fstream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"

#include "dpda.hpp"
#include "letter.hpp"
#include "parser.hpp"
#include "state.hpp"

using namespace fl;

#define s	 0
#define _f	 1
#define f(x) x + Letter::size
#define eps	 Letter::eps

#define CHECK_RECOGNIZE(a, str) CHECK(a.recognize(str))

#define CHECK_RECOGNIZE_FALSE(a, str) CHECK_FALSE(a.recognize(str))

#define CHECK_THROWS_PRINT(smt) \
	try {                       \
		smt;                    \
	} catch (const std::exception &e) { std::cerr << e << std::endl; }

#define CHECK_PARSETREE_FALSE(a, str) \
	std::cerr << str << std::endl;    \
	CHECK_THROWS_PRINT(a.parse(str));

#define CHECK_PARSETREE__(a, str, p)                \
	{                                               \
		std::cout << str << std::endl;              \
		auto t = a.parse(str);                      \
		CHECK(t != nullptr);                        \
		if (p) std::cout << t << std::endl;         \
		else std::cout << " accepted" << std::endl; \
	}

#define CHECK_PARSETREE(a, str)		  CHECK_PARSETREE__(a, str, false);
#define CHECK_PARSETREE_PRINT(a, str) CHECK_PARSETREE__(a, str, true);

TEST_CASE("a^n.b^n hardcoded") {
	/*
	 * S -> aSb | eps
	 */
	DPDA<State<Letter>, Letter> a;
	a.addTransition(0, 'a', eps, 0, {'a'});
	a.addTransition(0, 'a', 'a', 0, {'a', 'a'});
	a.addTransition(0, 'b', 'a', 1, {});
	a.addTransition(1, 'b', 'a', 1, {});
	a.addTransition(1, eps, eps, 2, {});
	a.qFinal = 2;

	const char *str1 = "aaaaaabbbbbb";
	const char *str2 = "aaaaabbbbbb";
	SUBCASE(str1) { CHECK(a.recognize(str1)); }
	SUBCASE(str2) { CHECK_FALSE(a.recognize(str2)); }
}

TEST_CASE("arithmetics hardcoded") {
	/* SIGMA = {i, +, (, ), ., #}
	 *
	 * e -> e+t | t
	 * t -> t.f | f
	 * f -> i | (e)
	 *
	 *
	 * modified:
	 *  e -> tE
	 *  E -> eps | +tE
	 *  t -> FT
	 *  T -> eps | .fT
	 *  f -> i | (e)
	 */

	std::vector<char> Sigma = {'i', '+', '(', ')', '.', '#'};

	DPDA<State<Letter>, Letter> a;
	a.addTransition(s, eps, eps, _f, "e");

	for (char x : Sigma)
		a.addTransition(_f, x, eps, f(x), {});

	for (char x : {'i', '+', '(', ')', '.'})
		a.addTransition(f(x), eps, x, _f, {});

	for (char x : Sigma) {
		a.addTransition(f(x), eps, 'e', f(x), "tE");
	}
	a.addTransition(f('+'), eps, 'E', f('+'), "+tE");

	for (char x : {')', '#'}) {
		a.addTransition(f(x), eps, 'E', f(x), {});
	}

	for (char x : Sigma) {
		a.addTransition(f(x), eps, 't', f(x), "fT");
	}
	a.addTransition(f('.'), eps, 'T', f('.'), ".fT");

	for (char x : {'+', ')', '#'}) {
		a.addTransition(f(x), eps, 'T', f(x), {});
	}
	a.addTransition(f('('), eps, 'f', f('('), "(e)");
	a.addTransition(f('i'), eps, 'f', f('i'), "i");
	a.qFinal = f('#');
	// a.enable_print = true;

	const char *str1 = "(i+i).i#";
	SUBCASE(str1) { CHECK_RECOGNIZE(a, str1); }

	const char *str2 = "(i+i).i.(i.(i+.)).(i+i+i)#";
	SUBCASE(str2) { CHECK_RECOGNIZE_FALSE(a, str2); }
	const char *str3 = "(i+i).i.(i.(i+i+i+i)).(i+i+i)#";
	SUBCASE(str3) { CHECK_RECOGNIZE(a, str3); }
}

TEST_CASE("0^n.1^n.# hardcoded") {
	/*
	 * S -> aSb | eps
	 */
	DPDA<State<Letter>, Letter> a;
	a.addTransition(s, eps, eps, _f, "S");
	a.addTransition(_f, '0', eps, f('0'), {});
	a.addTransition(f('0'), eps, '0', _f, {});
	a.addTransition(_f, '1', eps, f('1'), {});
	a.addTransition(f('1'), eps, '1', _f, {});
	a.addTransition(f('0'), eps, 'S', f('0'), "0S1");
	a.addTransition(f('1'), eps, 'S', f('1'), {});
	a.addTransition(_f, '#', eps, f('#'), {});
	a.qFinal = f('#');

	const char *str1 = "000111#";
	SUBCASE(str1) { CHECK_RECOGNIZE(a, str1); }

	const char *str2 = "001111#";
	SUBCASE(str2) { CHECK_RECOGNIZE_FALSE(a, str2); }
}
#undef eps
#undef f
#undef s
#undef _f

TEST_CASE("arithmetics from grammar") {
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

	Parser<Letter> a(g);
	// a.printTransitions();
	// a.enable_print = true;
	const char *str1 = "(i+i).i#";
	SUBCASE(str1) {
		CHECK_RECOGNIZE(a, str1);
		CHECK_PARSETREE(a, str1);
	}

	const char *str2 = "(i+i).i.(i.(i+.)).(i+i+i)#";
	SUBCASE(str2) {
		CHECK_RECOGNIZE_FALSE(a, str2);
		CHECK_PARSETREE_FALSE(a, str2);
	}

	const char *str3 = "(i+i).i.(i.(i+i+i+i)).(i+i+i)#";
	SUBCASE(str3) {
		CHECK_RECOGNIZE(a, str3);
		CHECK_PARSETREE(a, str3);
	}

	const char *str4 = "(i#";
	SUBCASE(str4) {
		CHECK_RECOGNIZE_FALSE(a, str4);
		CHECK_PARSETREE_FALSE(a, str4);
	}

	std::ofstream out("graph.txt");
	a.printAsDOT(out);
	out.close();
}

TEST_CASE("ll1 finite grammar") {
	CFG<Letter> g;
	g.terminals	   = {'a', 'b', 'c', 'd', '#'};
	g.nonTerminals = {'S', 'A', 'B'};
	g.addRule('S', "aABb");
	g.addRule('A', "c");
	g.addRule('A', "");
	g.addRule('B', "d");
	g.addRule('B', "");
	g.start = 'S';
	g.eof	= '#';

	Parser<Letter> a(g);
	// a.printTransitions();
	// a.enable_print = true;
	const char *str1 = "acdb#";
	SUBCASE(str1) {
		CHECK_RECOGNIZE(a, str1);
		CHECK_PARSETREE(a, str1);
	}
	const char *str2 = "aaaaa#";
	SUBCASE(str2) {
		CHECK_RECOGNIZE_FALSE(a, str2);
		CHECK_PARSETREE_FALSE(a, str2);
	}
}

TEST_CASE("ll1 regular grammar") {
	CFG<Letter> g;
	g.terminals	   = {'a', 'b', 'c', 'd', 'e', 'f', '#'};
	g.nonTerminals = {'S', 'A', 'B', 'C', 'D'};
	g.addRule('S', "AB");
	g.addRule('S', "eDa");
	g.addRule('A', "ab");
	g.addRule('A', "c");
	g.addRule('B', "dC");
	g.addRule('C', "eC");
	g.addRule('C', "");
	g.addRule('D', "fD");
	g.addRule('D', "");
	g.start = 'S';
	g.eof	= '#';

	Parser<Letter> a(g);
	// a.printTransitions();
	// a.enable_print = true;
	const char *str1 = "efffb#";
	SUBCASE(str1) {
		CHECK_RECOGNIZE_FALSE(a, str1);
		CHECK_PARSETREE_FALSE(a, str1);
	}

	const char *str2 = "abdfeee#";
	SUBCASE(str2) {
		CHECK_RECOGNIZE_FALSE(a, str2);
		CHECK_PARSETREE_FALSE(a, str2);
	}

	const char *str3 = "efffa#";
	SUBCASE(str3) {
		CHECK_RECOGNIZE(a, str3);
		CHECK_PARSETREE_PRINT(a, str3);
	}
	const char *str4 = "abdeee#";
	SUBCASE(str4) {
		CHECK_RECOGNIZE(a, str4);
		CHECK_PARSETREE(a, str4);
	}
}

TEST_CASE("a^n.b^n") {
	CFG<Letter> g;
	g.terminals	   = {'a', 'b', '#'};
	g.nonTerminals = {'S'};
	g.addRule('S', "aSb");
	g.addRule('S', {});
	g.start = 'S';

	Parser<Letter> a(g);
	std::cout << a.parse("aabb") << std::endl;
}

TEST_CASE("ambiguous grammar") {
	CFG<Letter> g;
	g.terminals	   = {'a'};
	g.nonTerminals = {'S', 'A'};
	g.start		   = 'S';
	g.addRule('S', "A");
	g.addRule('S', "a");
	g.addRule('A', "a");

	using Parser = Parser<Letter>;
	Parser *a;
	CHECK_THROWS_PRINT(a = new Parser(g));
	(void)a;
}

TEST_CASE("epsilon grammar") {
	CFG<Letter> g;
	g.terminals	   = {'a', 'b', '#'};
	g.nonTerminals = {'S', 'A', 'B'};
	g.start		   = 'S';
	g.addRule('S', "AB");
	g.addRule('A', {});
	g.addRule('B', {});

	Parser<Letter> a(g);
	CHECK_RECOGNIZE(a, "#");
	CHECK_RECOGNIZE_FALSE(a, "a");
}
