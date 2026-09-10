#include <ios>
#include <iostream>

#include <parser.h>
#include <token.h>
#include <grammar_factory.hpp>
#include <lex_traverser.hpp>
#include <letter.hpp>
#include "OutputFSA.hpp"

int main() {
	using namespace ll1g;
	using namespace fl;

	auto g = Seq(Letter('S'), {true, false, true}, Letter('['),
				 Optional(Letter('A'),
						  Seq(Letter('B'), Letter('1'),
							  Repeat(Letter('C'), Production<Letter>(toSymbol<Letter>(",1"), {true, false}), INT_MAX))),
				 Letter(']'));

	g.printParseTable();
	std::cout << "----" << std::endl;

	auto parser = Parser<Letter>(g);

	parser.parse(toSymbol<Letter>("[]#"));
	parser.parse(toSymbol<Letter>("[1]#"));
	parser.parse(toSymbol<Letter>("[1,1]#"));
	parser.parse(toSymbol<Letter>("[1,1,1]#"));
	auto pt = parser.parse("[1,1,1,1,1]#");
	std::cout << pt << std::endl;

	pt = parser.ASTparse("[1,1,1,1,1]#");
	std::cout << pt << std::endl;

	const Token If	= Token::createToken("IF");
	const Token For = Token::createToken("FOR");
	const Token Id	= Token::createToken("ID");
	const Token WS	= Token::createToken("WS");

	auto tokenizeIF	 = BS_WordFSA<Token>({'i', 'f'}, {});
	auto tokenizeFor = BS_WordFSA<Token>({'f', 'o', 'r'}, {});

	BS_FSA<Token> tokenizeId = BS_WordFSA<Token>({'a'}, {});
	for (char c = 'b'; c <= 'z'; ++c) {
		tokenizeId = std::move(BS_UnionFSA<Token>(std::move(tokenizeId), BS_WordFSA<Token>({c}, {})));
	}
	tokenizeId = BS_KleeneStarFSA<Token>(std::move(tokenizeId), false);

	BS_FSA<Token> whitespace = BS_WordFSA<Token>({' '}, {});
	whitespace				 = BS_UnionFSA<Token>(std::move(whitespace), BS_WordFSA<Token>({'\n'}, {}));
	whitespace				 = BS_UnionFSA<Token>(std::move(whitespace), BS_WordFSA<Token>({'\t'}, {}));
	whitespace				 = BS_KleeneStarFSA<Token>(std::move(whitespace), false);

	auto TokenizeID	 = OutputFSA(pseudoDeterminizeFST(realtimeFST(std::move(tokenizeId))), Id);
	auto TokenizeFor = OutputFSA("'for'", For);
	auto TokenizeIF	 = OutputFSA(pseudoDeterminizeFST(realtimeFST(std::move(tokenizeIF))), If);
	auto TokenizeWS	 = OutputFSA(pseudoDeterminizeFST(realtimeFST(std::move(whitespace))), WS);

	auto Tokenizer = UnionOutputFSA<Token>(std::move(TokenizeIF), std::move(TokenizeFor), std::move(TokenizeID),
										   std::move(TokenizeWS));
	drawFSA(Tokenizer);

	auto SSFTTokenizer = Tokenizer.determinizeToSSFT();
	drawFSA(SSFTTokenizer);

	auto traverser = SSFSTTraverser(SSFTTokenizer);

	auto result = traverser.traverseOutputOnlyUntilCan(toSymbol<Token>("if"));
	std::ranges::for_each(result, [](auto x) { std::cout << x << " "; });
	std::cout << std::endl;
	result = traverser.traverseOutputOnlyUntilCan(toSymbol<Token>("for"));
	std::ranges::for_each(result, [](auto x) { std::cout << x << " "; });
	std::cout << std::endl;
	result = traverser.traverseOutputOnlyUntilCan(toSymbol<Token>("abc"));
	std::ranges::for_each(result, [](auto x) { std::cout << x << " "; });
	std::cout << std::endl;

	std::cin >> std::noskipws;
	auto input = std::views::istream<char>(std::cin) | std::views::cache_latest;

	LexerRange lexer(input, std::move(SSFTTokenizer), Token::createToken("ERROR"));
	for (auto [token, from, to, line, str] : lexer) {
		std::cout << std::format("Token: {} from {} to {}\n", token, from, to);
		//	std::cout << std::format("Error from {} to {}\n", from, to);
	}

	return 0;
}
