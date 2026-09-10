#include "OutputFSA.hpp"
#include "SSFT.hpp"
#include "lex_traverser.hpp"
#include <cassert>
#include <cctype>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <ostream>
#include <string_view>

#include <parser.h>
#include <cfg.h>
#include <utils.h>
#include <token.h>
#include <earley.hpp>
#include <grammar_factory.hpp>

using namespace fl;

const Token Program				 = Token::createToken("Program");
const Token Number				 = Token::createToken("Number");
const Token Identifier			 = Token::createToken("Identifier");
const Token NL					 = Token::createToken("NL", '\n');
const Token Expression			 = Token::createToken("Expression");
const Token Assignment			 = Token::createToken("Assignment");
const Token Arithmetic			 = Token::createToken("Arithmetic");
const Token Term				 = Token::createToken("Term");
const Token Factor				 = Token::createToken("Factor");
const Token Conditional			 = Token::createToken("Conditional");
const Token Comparison			 = Token::createToken("Comparison");
const Token CommaSep			 = Token::createToken("CommaSep");
const Token ParamList			 = Token::createToken("ParamList");
const Token Scope				 = Token::createToken("Scope");
const Token ParenthesisExpr		 = Token::createToken("(Expr)");
const Token ParenthesisParamList = Token::createToken("(ParamList)");
const Token BracketParamList	 = Token::createToken("[ParamList]");

const Token If	  = Token::createToken("if");
const Token While = Token::createToken("while");
const Token For	  = Token::createToken("for");

const Token Error = Token::createToken("Error");
const Token WS	  = Token::createToken("Whitespace");

CFG<Token> createCFGNew() {
	using namespace ll1g;

	Token current = Token::eps;
	auto  NT	  = [&]() {
		assert(current != Token::eps && "Cannot create dependent token from eps");
		return current = Token::createDependentToken(current);
	};

	current		 = Program;
	auto program = Repeat(Program, Choice(NT(), Expression, Conditional, Scope), INT_MAX);

	current			= Expression;
	auto expression = Seq(Expression, {false, true}, Optional(NT(), Assignment), Token(';'));

	current			= Assignment;
	auto assignment = Seq(Assignment, Comparison,
						  RepeatChoice(NT(), Production({Token('='), Comparison}, {true, false}, Token('='))));

	current			= Comparison;
	auto comparison = Seq(Comparison, Arithmetic,
						  RepeatChoice(NT(), Production({Token('<'), Arithmetic}, {true, false}, Token('<')),
									   Production({Token('>'), Arithmetic}, {true, false}, Token('>'))));

	current			= Arithmetic;
	auto arithmetic = Seq(Arithmetic, Term,
						  RepeatChoice(NT(), Production({Token('+'), Term}, {true, false}, Token('+')),
									   Production({Token('-'), Term}, {true, false}, Token('-'))));

	current	  = Term;
	auto term = Seq(Term, Factor,
					RepeatChoice(NT(), Production({Token('*'), Factor}, {true, false}, Token('*')),
								 Production({Token('/'), Factor}, {true, false}, Token('/'))));

	current				 = ParenthesisExpr;
	auto parenthesisExpr = Word(ParenthesisExpr, {Token('('), Assignment, Token(')')}, {true, false, true});
	parenthesisExpr.setIgnoreEmpty(true);
	parenthesisExpr.setIgnoreSingleChild(true);

	current		= Factor;
	auto factor = Choice(Factor, std::vector{Identifier, ParamList}, std::vector{Number},
						 std::move(parenthesisExpr)		// move the entire grammar
	);

	current	   = Scope;
	auto scope = Word(Scope, {'{', Program, '}'}, {true, false, true});
	scope.setIgnoreEmpty(false);
	scope.setIgnoreSingleChild(false);

	current = Conditional;
	auto conditional =
		Choice(Conditional, Production({If, '(', Assignment, ')', Scope}, {true, true, false, true, false}, If),
			   Production({While, '(', Assignment, ')', Scope}, {true, true, false, true, false}, While),
			   Production({For, '(', Expression, Expression, Assignment, ')', Scope},
						  {true, true, false, false, false, true, false}, For));
	conditional.setIgnoreEmpty(false);

	current		   = ParamList;
	auto paramList = RepeatChoice(NT(), Word(ParenthesisParamList, {'(', CommaSep, ')'}, {true, false, true}),
								  Word(BracketParamList, {'[', CommaSep, ']'}, {true, false, true}));
	paramList.getNonTerminalData(ParenthesisParamList).ignoreEmpty		 = false;
	paramList.getNonTerminalData(ParenthesisParamList).ignoreSingleChild = false;
	paramList.getNonTerminalData(BracketParamList).ignoreEmpty			 = false;
	paramList.getNonTerminalData(BracketParamList).ignoreSingleChild	 = false;
	paramList.setUpwardSpillThreshold(INT_MAX);

	auto optionalParamList = Optional(ParamList, std::move(paramList));

	current		  = CommaSep;
	auto commaSep = Seq(NT(), Assignment, Repeat(NT(), Word(NT(), {',', Assignment}, {true, false}, true), INT_MAX));
	commaSep.setUpwardSpillThreshold(INT_MAX);

	auto optionalCommaSep = Optional(CommaSep, std::move(commaSep));
	optionalCommaSep.setUpwardSpillThreshold(INT_MAX);

	auto g = Combine(program, expression, assignment, comparison, arithmetic, term, factor, scope, conditional,
					 optionalParamList, optionalCommaSep);

	// g.printParseTable();
	return g;
}

std::unique_ptr<CFG<Token>> g{nullptr};

auto createTokenizer() {
	OutputFSA<Token> if_	= OutputFSA("'if'", If);
	OutputFSA<Token> while_ = OutputFSA("'while'", While);
	OutputFSA<Token> for_	= OutputFSA("'for'", For);

	std::string_view digits = "('0'+'1'+'2'+'3'+'4'+'5'+'6'+'7'+'8'+'9')";
	std::string_view _az =
		"('a'+'b'+'c'+'d'+'e'+'f'+'g'+'h'+'i'+'j'+'k'+'l'+'m'+'n'+'o'+'p'+'q'+'r'+'s'+'t'+'u'+'v'+'w'+'x'+'y'+'z')";
	std::string_view _AZ =
		"('A'+'B'+'C'+'D'+'E'+'F'+'G'+'H'+'I'+'J'+'K'+'L'+'M'+'N'+'O'+'P'+'Q'+'R'+'S'+'T'+'U'+'V'+'W'+'X'+'Y'+'Z')";

	std::string		 numberRegex = std::format("({}!)", digits);
	OutputFSA<Token> number_	 = OutputFSA(numberRegex, Number);
	OutputFSA<Token> identifier_ = OutputFSA(std::format("({}+{}+'_')!", _az, _AZ), Identifier);

	OutputFSA<Token> semicolon{"';'", ';'};
	OutputFSA<Token> colon{"':'", ':'};
	OutputFSA<Token> comma{"','", ','};
	OutputFSA<Token> dot{"'.'", '.'};
	OutputFSA<Token> plus{"'+'", '+'};
	OutputFSA<Token> minus{"'-'", '-'};
	OutputFSA<Token> multiply{"'*'", '*'};
	OutputFSA<Token> divide{"'/'", '/'};
	OutputFSA<Token> equal{"'='", '='};
	OutputFSA<Token> lt{"'<'", '<'};
	OutputFSA<Token> tilde{"'~'", '~'};
	OutputFSA<Token> braceOpen{"'{'", '{'};
	OutputFSA<Token> braceClose{"'}'", '}'};
	OutputFSA<Token> parenthOpen{"'('", '('};
	OutputFSA<Token> parenthClose{"')'", ')'};
	OutputFSA<Token> brackOpen{"'['", '['};
	OutputFSA<Token> brackClose{"']'", ']'};
	OutputFSA<Token> exclamation{"'!'", '!'};

	OutputFSA<Token> whitespace{"(' '+'\n'+'\t'+'\r')!", WS};
	OutputFSA<Token> eof{realtimeFST(BS_WordFSA<Token>({Token::eof}, {})),
						 Token::eof};	  // hacky but regexes do not support EOF

	auto tokenizer = UnionOutputFSA<Token>(
		// union everything
		std::move(if_), std::move(while_), std::move(for_), std::move(number_), std::move(identifier_),
		std::move(whitespace),
		// single characters
		std::move(semicolon), std::move(colon), std::move(comma), std::move(dot), std::move(plus), std::move(minus),
		std::move(multiply), std::move(divide), std::move(equal), std::move(lt), std::move(tilde), std::move(braceOpen),
		std::move(braceClose), std::move(parenthOpen), std::move(parenthClose), std::move(brackOpen),
		std::move(brackClose), std::move(exclamation), std::move(eof));
	return tokenizer.determinizeToSSFT();
}

std::pair<std::vector<Token>, WordSet<Token>> tokenize(std::vector<Token> &text) {
	static fl::SparseSSFST<Token> tokenizer = createTokenizer();
	std::vector<Token>	   tokens;
	WordSet<Token>		   words;

	for (auto token : LexerRange(text, tokenizer, Error)) {
		if (token.token == WS) continue;
		if (token.token == Error) {
			std::print(std::cout, "error: unrecognized token at line {}:\n'", token.line);
			auto view = std::span{text.data() + token.from, text.data() + token.to};
			for (auto c : view)
				std::cout << char(c);
			std::cout << "'" << std::endl;
			exit(1);
		}
		if (token.token == Number) {
			auto view = std::span{text.data() + token.from, text.data() + token.to};
			// to size_t
			std::size_t num = 0;
			for (auto c : view) {
				num *= 10;
				num += char(c) - '0';
			}
			tokens.push_back(Token(Number, (uint8_t *)num));
		} else if (token.token == Identifier) {
			auto view  = std::span{text.data() + token.from, text.data() + token.to};
			auto index = words.addWord(view);
			tokens.push_back(Token(Identifier, (uint8_t *)(size_t)index));
		} else {
			tokens.push_back(token.token);
		}
	}
	return {tokens, words};
}

struct ASTNode {
	Token								  type;
	std::vector<std::unique_ptr<ASTNode>> children;

   public:
	ASTNode(Token type) : type(type) {}
};

static WordSet<Token> words;
std::ostream		 &operator<<(std::ostream &out, const ASTNode *node) {
	bits b;
	p_show<ASTNode>(out, node, b, [](std::ostream &out, const ASTNode *r) {
		out << " " << r->type << " ";
		if (r->type == Number) { out << reinterpret_cast<std::size_t>(r->type.data); }
		if (r->type == Identifier) {
			auto toPrint = words.getWord(reinterpret_cast<std::size_t>(r->type.data));
			for (auto c : toPrint)
				out << char(c);
		}
		out << std::endl;
	});
	return out;
}
std::ostream &operator<<(std::ostream &out, const std::unique_ptr<ASTNode> &node) { return out << node.get(); }

template <>
struct std::formatter<ASTNode> : ostream_formatter {};

void random_tokens(std::vector<Token> &text, std::ostream &os) {
	for (Token &i : text) {
		if (i == Number) {
			os << rand();
		} else if (i == Identifier) {
			os << gen_random_string(rand() % 10 + 1) << " ";
		} else {
			os << i;
		}
	}
}

int main(int argc, char **argv) {
	g = std::make_unique<CFG<Token>>(createCFGNew());
	// g->printParseTable();
	if (argc >= 2 && std::string(argv[1]) == "generate") {
		srand(time(0));
		std::size_t tokens = -1;
		if (argc == 3) { tokens = std::stoi(argv[2]); }
		std::size_t cnt = 0;
		while (tokens > cnt) {
			auto v = g->generate(1000, 2000);
			cnt += v.size();
			random_tokens(v, std::cout);
		}
		return 0;
	}
	try {
		Parser<Token> parser(*g);
		auto		  tokenizer = createTokenizer();

		// parser.enable_print = true;

		EarleyParser<Token> earleyParser(*g);
		earleyParser.expect_eof = true;

		std::string fileName = "test_file.txt";
		if (argc == 2) fileName = argv[1];

		std::ifstream file(fileName);
		if (!file) {
			std::cout << "error: " << strerror(errno) << std::endl;
			return 1;
		}
		std::cout << "parsing file: " << fileName << std::endl;
		std::vector<Token> text;
		for (Token t : CharInputStream<Token>(file)) {
			text.push_back(t);
		}

		BENCH(tokenize(text), 100, "BENCH tokenize : ");
		auto [tokens, words] = tokenize(text);
		::words				 = std::move(words);
		std::cout << "tokens: " << tokens.size() << std::endl;

		BENCH(parser.parse(tokens), 10, "BENCH building parse tree: ");
		auto t = parser.parse(tokens);
		// if (tokens.size() < 1000) std::cout << t << std::endl;

		BENCH(earleyParser.recognize(tokens), 10, "BENCH earley parse: ");
		assert(earleyParser.recognize(tokens));

		BENCH(parser.ASTparse(tokens), 100, "BENCH building AST: ");
		if (tokens.size() < 1000) {
			auto ast = parser.ASTparse(tokens);
			std::cout << (ASTNode *)ast.get() << std::endl;
		}

	} catch (const std::exception &e) { std::cerr << e << std::endl; }
}
