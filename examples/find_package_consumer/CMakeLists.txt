cmake_minimum_required(VERSION 3.16)
project(ZenithFindPackageConsumer LANGUAGES CXX)

find_package(ZenithMemory 1.0 REQUIRED)

add_executable(ZenithFindPackageConsumer main.cpp)
target_link_libraries(ZenithFindPackageConsumer PRIVATE Zenith::Memory)
target_compile_features(ZenithFindPackageConsumer PRIVATE cxx_std_17)
