cmake_minimum_required(VERSION 3.1.0)
project(assign1)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-frtti)
endif()

set(LLVM_ENABLE_RTTI ON)
set(LLVM_ENABLE_EH ON)

find_package(Clang REQUIRED CONFIG HINTS ${LLVM_DIR} ${LLVM_DIR}/lib/cmake/clang NO_DEFAULT_PATH)
# find_package(Clang REQUIRED PATHS ${LLVM_DIR}/lib/cmake/clang NO_DEFAULT_PATH)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} SYSTEM)
link_directories(${LLVM_LIBRARY_DIRS})

# message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "CLANG_INCLUDE_DIRS: ${CLANG_INCLUDE_DIRS}")

file(GLOB SOURCE "./*.cpp")

add_executable(ast-interpreter ${SOURCE})

set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Option
  Support
  )


target_link_libraries(ast-interpreter
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  )

install(TARGETS ast-interpreter
  RUNTIME DESTINATION bin)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)