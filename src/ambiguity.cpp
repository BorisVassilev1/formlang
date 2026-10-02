#include "ambiguity.hpp"

int				  fl::foundat = 1, fl::sccIndex = 0;
std::vector<int>  fl::scc;
std::vector<int>  fl::disc, fl::low;
std::vector<bool> fl::onstack;
