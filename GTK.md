Compiler GCC 14.2
- From this I can chose for each build the C++ language standard (C++17, C++20, etc.)
- GG 14.2 supports both

vcpkg : package manager, used to download proto and grpc

cmake DCMAKE_TOOLCHAIN_FILE points to : vcpkg.cmake
- This tell which compilers/SDKs/paths CMake should use to compile/link
- Generator used : Visual Studio 2022
  - Generator is which build system files CMake produces
