project("document_db")

cmake_minimum_required(VERSION 3.2)

# target_compile_features(${TARGET} PUBLIC cxx_std_17)

# target_compile_options(-g)
set(CMAKE_BUILD_TYPE Debug)
add_executable(main document.h document.cpp main.cpp)