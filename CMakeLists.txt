cmake_minimum_required(VERSION 3.28)
project(sdp_project)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every target that (transitively) imports the `formlang` module must compile with the exact
# same flags, or CMake has to build a separate copy of every module partition's BMI per distinct
# flag set (visible as "@synth_N" build steps) -- silently multiplying the whole module graph's
# compile cost. Keep this one list shared by `lang` and its own test executables; the top-level
# bpe_proofs project mirrors it for the same reason.
set(COMPILE_ARGS
	-Wall -Wextra -Wpedantic --pedantic-errors -O3
	$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c2y-extensions>
	#-g -fsanitize=address
)

file(GLOB LANG_MODULES "modules/*.cppm")

add_library(lang STATIC)
target_sources(lang
	PUBLIC
		FILE_SET CXX_MODULES
		BASE_DIRS modules
		FILES ${LANG_MODULES}
)
target_compile_options(lang PRIVATE ${COMPILE_ARGS})
target_link_options(lang PRIVATE ${COMPILE_ARGS})
# only the macro-only headers (dbLog/BENCH/CREATE_TOKEN_CONSTEXPR) remain here;
# macros can't be exported from a module, so consumers that need them #include
# this directory directly alongside `import formlang;`
target_include_directories(lang PUBLIC include)
set_target_properties(lang PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ../
)


set(FL_TARGETS
	dpda_test
	cfg_test
	earley_test
	language
	regex
	FST_test
	#regex2
	#regex3
	langdef
	replace_test
	monoid_test
	concept_checks
)

foreach(target IN LISTS FL_TARGETS)
	add_executable(${target}
		tests/${target}.cpp
	)
	target_link_libraries(${target} PRIVATE lang)
	target_compile_options(${target} PRIVATE ${COMPILE_ARGS})
	target_link_options(${target} PRIVATE ${COMPILE_ARGS})
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ../
	)
endforeach()

add_executable(regex2
		regex2.cpp
)
target_link_libraries(regex2 PRIVATE lang)
target_compile_options(regex2 PRIVATE ${COMPILE_ARGS})
target_link_options(regex2 PRIVATE ${COMPILE_ARGS})
set_target_properties(regex2 PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ../
)
add_executable(regex3
		regex3.cpp
)
target_link_libraries(regex3 PRIVATE lang)
target_compile_options(regex3 PRIVATE ${COMPILE_ARGS})
target_link_options(regex3 PRIVATE ${COMPILE_ARGS})
set_target_properties(regex3 PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ../
)

enable_testing()
add_test(NAME dpda_test
	COMMAND dpda_test
	DEPENDS dpda_test)

add_test(NAME cfg_test
	COMMAND cfg_test
	DEPENDS cfg_test)

add_test(NAME concept_checks
	COMMAND concept_checks
	DEPENDS concept_checks)


add_custom_target(DPDA_check COMMAND ${CMAKE_CTEST_COMMAND}
	DEPENDS cfg_test dpda_test)
