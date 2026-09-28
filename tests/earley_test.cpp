import formlang;

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../doctest.h"

using namespace fl;

TEST_CASE("a^nb^n") {
	/* S -> aSb | eps */
	CFG<Letter> g;
	g.terminals	   = {'a', 'b', '#'};
	g.nonTerminals = {'S'};
	g.addRule('S', "aSb");
	g.addRule('S', {});
	g.start = 'S';

	auto p		   = EarleyParser<Letter>(g);
	p.enable_print = true;

	SUBCASE("aabb") { CHECK(p.recognize({'a', 'a', 'b', 'b'})); }
	SUBCASE("aabbb") { CHECK_FALSE(p.recognize({'a', 'a', 'b', 'b', 'b'})); }
	std::cout << std::endl << std::endl;
}

TEST_CASE("grammar") {
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

	auto p = EarleyParser<Letter>(g);

	CHECK(p.recognize({'i', '+', 'i'}));

	CHECK(p.recognize({'i', '+', 'i', '.', 'i'}));

	CHECK(p.recognize({'i', '+', 'i', '.', 'i', '+', 'i', '.', '(', 'i', '+', 'i', ')'}));
}
