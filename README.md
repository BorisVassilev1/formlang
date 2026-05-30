# FormLang 

Library for working with formal languages. Currently features:
- Context-Free Grammars (CFG)
- Deterministic PushDown Automata (DPDA)
- LL(1) Grammar Parser (Parse-Tree builder)
- Earley Parser (no parse-tree building)
- Regular Expression Parser 
- Regular Relation Expression Parser
- Two-Tape Finite-State Automata
- Subsequential Transducers
- UTF-8 Alphabet

The was initially developed as a project for the "Data Structures Programming" course at FMI, but later expanded in the courses "Finite State Automata and Applications", "Compilers - Introduction", "Languages, Automata and Computability".

Sections will be documented here in the near future.

## LL(1) Grammar Parser
### Project for the "Data Structures Programming" course at FMI

This project is a library that defines Context-Free Grammar, Deterministic PushDown Automaton, and Parser (Parse-Tree builder) as C++ classes.

The main functionality can be described by this example:
```cpp
CFG<Letter> g;
g.terminals	   = {'a', 'b', '#'};
g.nonTerminals = {'S'};
g.addRule('S', "aSb");
g.addRule('S', {});
g.start = 'S';

Parser<State, Letter> a(g);
std::cout << a.parse("aabb") << std::endl;
```
This produces the following parse-tree:
```
-S
 ├-a
 ├-S
 │ ├-a
 │ ├-S
 │ │ └-ε
 │ └-b
 └-b
```
A more complex example can be seen in [language.cpp](https://github.com/BorisVassilev1/sdp_2023/blob/master/DPDA/language.cpp).

### Notes on algorithms used

The main algorithm used to construct a Parser for a LL(1) grammar is described in detail in [this presentation from UCLA](http://ll1academy.cs.ucla.edu/static/LLParsing.pdf) and a proof for its correctness is described in [this Chapter from UCalgary's Compiler Construction course](https://pages.cpsc.ucalgary.ca/~robin/class/411/LL1.2.html). While a general test and parsing algorithm exist for `LL(k)` grammars for every `k` (described in [this paper](https://www.sciencedirect.com/science/article/pii/S0019995870904468?via%3Dihub)), such implementation would be impractical because the size of the PDA's that parse those grammars grows exponentially with `k`. Also, in the future left-factorization can be implemented to allow for the parsing of some non-LL(1) grammars with LL(1) languages. 

While discussing the assignment, there was the idea to implement algorithms that construct grammars or parsers for languages, described as some basic set operation on the languages of existing machines, but most such known algorithms proved to be either trivial ($A: (G_1, G_2) \rightarrow G_3$ where $L(G_3) = L(G_1).L(G_2)$) or unsolvable ($A: (G_1, G_2) \rightarrow G_3 $ where $L(G_3) = L(G1)\cap L(G2)$). Another possibility was to compute some relation between the languages of grammars, but later I found [this paper](https://dl.acm.org/doi/pdf/10.1145/322307.322317) that essentially states that most of those relations are undecidable, even for LL and LR grammars.

## Compilation Instructions:

Run the `configure` script for your operating system (`.bat` for windows and `.sh` for Linux\MacOS). Then run `cmake --build ./build`. The cmake target `check` runs all test cases. 
The project was tested with `clang`.
