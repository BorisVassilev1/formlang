import formlang;
#include <cassert>
#include <cstdlib>
#include <ctime>

int main() {
	fl::CFG<fl::Letter> g;
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

	g.printParseTable();

	srand(time(0));
	// std::cout << g.generate(90, 110) << std::endl;
}
