// Concept-conformance checks that used to live as trailing
// `#include "letter.hpp"` + static_assert blocks at the bottom (or middle) of
// several formlang headers. Module partitions can't interleave imports with
// other declarations the way #include let those headers do, so all of these
// self-checks against the concrete fl::Letter/fl::Token type live here
// instead, in one place, importing the finished module.

import formlang;

#include <ranges>
#include <iterator>

using SSFSTType = fl::SparseSSFST<fl::Letter, fl::InterningMonoid<fl::Letter>>;

// from letter.hpp / token.h (kept for parity; already re-checked in-partition too)
static_assert(fl::symbol<fl::Letter>, "fl::Letter does not satisfy the symbol concept");
static_assert(fl::symbol<fl::Token>, "fl::Token does not satisfy the symbol concept");

// from IntegerMonoid.hpp
static_assert(fl::monoid<fl::IntegerMonoid<>>);
static_assert(fl::free_monoid<fl::IntegerMonoid<>>);
static_assert(fl::printable_monoid<fl::IntegerMonoid<>>);

// from InterningMonoid.hpp
static_assert(fl::monoid<fl::InterningMonoid<char>>);
static_assert(fl::free_monoid<fl::InterningMonoid<char>>);
static_assert(fl::printable_monoid<fl::InterningMonoid<char>>);
static_assert(fl::serializable_monoid<fl::InterningMonoid<fl::Letter>>);

// from SymbolMonoid.hpp
static_assert(fl::monoid<fl::SymbolMonoid<fl::Letter>>);
static_assert(fl::free_monoid<fl::SymbolMonoid<fl::Letter>>);
static_assert(fl::printable_monoid<fl::SymbolMonoid<fl::Letter>>);

// from CartesianMonoid.hpp
static_assert(fl::monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
static_assert(fl::monoid<fl::CartesianMonoid<fl::InterningMonoid<char> &, fl::InterningMonoid<char>>>);
static_assert(fl::monoid<fl::DiagonalMonoid<fl::InterningMonoid<char>>>);
/// cartesian product of free monoids is not itself a free monoid, because (a, Ɛ) and (Ɛ, a) commute
static_assert(!fl::free_monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);
static_assert(fl::printable_monoid<fl::CartesianMonoid<fl::IntegerMonoid<>, fl::IntegerMonoid<>, fl::IntegerMonoid<>>>);

// from FST.hpp
static_assert(fl::FSA<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::IntegerMonoid<>>>,
			  "SparseFST<IntegerMonoid<>> should satisfy the FSA concept");
static_assert(fl::FST<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::IntegerMonoid<>>>,
			  "SparseFST<IntegerMonoid<>> should satisfy the FST concept");
static_assert(fl::FST<fl::SparseFST<fl::InterningMonoid<fl::Letter>, fl::InterningMonoid<fl::Letter>>>,
			  "SparseFST<InterningMonoid<>> should satisfy the FST concept");

// from ExpandedFST.hpp
static_assert(fl::FST<fl::ExpandedFST<fl::Letter, fl::InterningMonoid<fl::Letter>>>,
			  "ExpandedFST does not satisfy the FST concept required by pseudoDeterminizeFST/reverseFST/"
			  "pseudoMinimizeFST");

// from TotalSSFT.hpp
static_assert(fl::SSFSTI<fl::TotalSSFST<fl::Letter>>,
			  "TotalSSFT does not satisfy the subsequential transducer concept");
static_assert(fl::SSFSTI_traversable<fl::TotalSSFST<fl::Letter>>,
			  "TotalSSFT does not satisfy the subsequential transducer traversable concept");

// from SSFT.hpp
static_assert(fl::SSFST<SSFSTType>, "SparseSSFST does not satisfy the subsequential transducer concept");
static_assert(fl::SSFST_traversable<SSFSTType>,
			  "SparseSSFST does not satisfy the subsequential transducer traversable concept");

// from lex_traverser.hpp
static_assert(std::sentinel_for<fl::CharInputStream<fl::Letter>::sentinel, fl::CharInputStream<fl::Letter>::iterator>);
static_assert(std::ranges::viewable_range<fl::CharInputStream<fl::Letter>>);
static_assert(std::ranges::input_range<fl::CharInputStream<fl::Letter>>);

int main() { return 0; }
