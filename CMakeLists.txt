cmake_minimum_required(VERSION 3.28)
project(sdp_project)

set(COMPILE_ARGS 
	-std=c++26 -Wall -Wextra -Wpedantic --pedantic-errors -O3
	#-g -fsanitize=address
)

file(GLOB_RECURSE LANG_SOURCES "src/*.cpp")

add_library(lang STATIC ${LANG_SOURCES})
target_compile_options(lang PRIVATE ${COMPILE_ARGS} -fPIC)
target_link_options(lang PRIVATE ${COMPILE_ARGS} -fPIC)
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
)

foreach(target IN LISTS FL_TARGETS)
	add_executable(${target}
		tests/${target}.cpp
	)
	target_link_libraries(${target} PRIVATE lang)
	target_compile_options(${target} PRIVATE ${COMPILE_ARGS} $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c2y-extensions>)
	target_link_options(${target} PRIVATE ${COMPILE_ARGS})
	target_include_directories(${target} PRIVATE ./include/)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ../
	)
endforeach()


enable_testing()
add_test(NAME dpda_test
	COMMAND dpda_test 
	DEPENDS dpda_test)

add_test(NAME cfg_test
	COMMAND cfg_test 
	DEPENDS cfg_test)


add_custom_target(DPDA_check COMMAND ${CMAKE_CTEST_COMMAND}
	DEPENDS cfg_test dpda_test)

#add_library(DPDA STATIC
#	DPDA/language.cpp
#	DPDA/utils.cpp
#	DPDA/token.cpp
#	util/utils.cpp
#	Regex/regexParser.cpp
#	Regex/ambiguity.cpp
#)
#target_compile_options(DPDA PRIVATE ${COMPILE_ARGS})
#target_link_options(DPDA PRIVATE ${COMPILE_ARGS})
#target_include_directories(DPDA PUBLIC .)

