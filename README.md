# Matching_Engine


## Why & How to install vcpkg, Protobuf, and gRPC (C++)
### Why these tools?
vcpkg: a C/C++ package manager that installs libraries (headers, libs, tools) and plugs them into CMake automatically. No manual path juggling.

Protobuf: schema + codegen for messages. Gives you protoc and the libprotobuf runtime.

gRPC: RPC framework built on HTTP/2 that uses Protobuf messages. Gives you grpc++ libs and the codegen plugin.

Using vcpkg avoids common Windows pain: missing headers, wrong ABIs, and CMake not finding packages.

## Windows quickstart (VS 2022)
1) Prereqs
Visual Studio 2022 (Desktop C++ workload)

CMake ≥ 3.20

Git, PowerShell

2) Install vcpkg + libs
powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

C:\vcpkg\vcpkg.exe install protobuf:x64-windows grpc:x64-windows


3) Configure your CMake project (with vcpkg toolchain)
powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
OR
cmake --build build --config Debug -j


4) Minimal CMake usage inside your project
cmake

 #CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO ${CMAKE_SOURCE_DIR}/proto/oms.proto)
set(GEN   ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN})

add_custom_command(
  OUTPUT  ${GEN}/oms.pb.cc ${GEN}/oms.pb.h
          ${GEN}/oms.grpc.pb.cc ${GEN}/oms.grpc.pb.h
  COMMAND protobuf::protoc
  ARGS --proto_path=${CMAKE_SOURCE_DIR}/proto
       --cpp_out=${GEN}
       --grpc_out=${GEN}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       ${PROTO}
  DEPENDS ${PROTO} protobuf::protoc gRPC::grpc_cpp_plugin
)

add_library(proto_lib STATIC
  ${GEN}/oms.pb.cc
  ${GEN}/oms.grpc.pb.cc
)
target_include_directories(proto_lib PUBLIC ${GEN})
target_link_libraries(proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)
Verify it worked
powershell
Copy
Edit
where protoc
# -> C:\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe

# Rebuild your project
cmake --build build --config Release -j
Troubleshooting (super short)
Clean reconfigure after changing toolchain/triplet: delete build/ and run CMake again.

Config mismatch: with VS generator, pass --config Debug/Release when building.

Arch mismatch: keep everything x64 (-A x64, triplet x64-windows).

macOS / Linux (alternatives)
macOS (Homebrew): brew install cmake protobuf grpc

Ubuntu (apt): sudo apt install cmake protobuf-compiler libprotobuf-dev libgrpc++-dev

Or use vcpkg with triplets: x64-osx, x64-linux, same CMake toolchain flow.

TL;DR: Install with vcpkg, point CMake to the vcpkg toolchain, find_package(Protobuf gRPC), run Protobuf/gRPC codegen in CMake, and link the imported targets. This keeps your C++ build clean and portable.