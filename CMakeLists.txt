cmake_minimum_required(VERSION 3.14)
project(cpp_ftp)

set(CMAKE_CXX_STANDARD 20)
include_directories(/usr/local/include)
include_directories(${CMAKE_CURRENT_LIST_DIR})
add_executable(cpp_ftp main.cpp argparse.hpp globals.hpp netbuffer.hpp ftp.hpp utils.hpp ftptransfer.h)

add_subdirectory(filesystem)
add_subdirectory(sockpp)
# if sockpp is installed, then uncomment the following line
# and comment out the previous line (add_subdirectory(sockpp))
# find_library(SOCKPP sockpp)

target_link_libraries(cpp_ftp ghc_filesystem)
target_link_libraries(cpp_ftp sockpp)
# if sockpp is installed, then uncomment the following line
# and comment out the previous line (target_link_libraries(cpp_ftp sockpp))
# target_link_libraries(cpp_ftp "${SOCKPP}")