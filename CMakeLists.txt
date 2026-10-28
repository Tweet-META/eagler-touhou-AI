cmake_minimum_required(VERSION 3.20)
project(eagler-touhou-ai VERSION 0.1.0 LANGUAGES CXX)

add_library(eagler_ai STATIC src/Planner.cpp)
add_library(eagler::ai ALIAS eagler_ai)
target_include_directories(eagler_ai PUBLIC include)
target_compile_features(eagler_ai PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(eagler_ai PRIVATE /W4 /utf-8 /fp:strict)
else()
  target_compile_options(eagler_ai PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

if(EMSCRIPTEN)
  add_executable(avoidance src/WebApi.cpp)
  target_link_libraries(avoidance PRIVATE eagler_ai)
  set_target_properties(avoidance PROPERTIES SUFFIX ".mjs")
  target_link_options(avoidance PRIVATE
    --no-entry -O3 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
    -sFILESYSTEM=0 -sINITIAL_MEMORY=16777216 -sALLOW_MEMORY_GROWTH=0
    "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAP32']"
    "-sEXPORTED_FUNCTIONS=['_ai_abi_version','_ai_create','_ai_destroy','_ai_reset','_ai_settings','_ai_begin','_ai_hazards','_ai_count','_ai_step','_ai_result']")
endif()

