cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) Find packages provided by vcpkg (you pass the toolchain at configure time)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# 2) Where the .proto lives and where generated code should go
set(PROTO ${CMAKE_SOURCE_DIR}/proto/matching_engine.proto)
set(GEN   ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN})

# 3) Run protoc + gRPC C++ plugin to generate sources (create *.pb.cc/h and *.grpc.pb.cc/h)
add_custom_command(
  OUTPUT  ${GEN}/matching_engine.pb.cc        
          ${GEN}/matching_engine.pb.h
          ${GEN}/matching_engine.grpc.pb.cc   
          ${GEN}/matching_engine.grpc.pb.h
          
  COMMAND protobuf::protoc
  ARGS --proto_path=${CMAKE_SOURCE_DIR}/proto
       --cpp_out=${GEN}
       --grpc_out=${GEN}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       ${PROTO}
  DEPENDS ${PROTO} protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generating C++ from ${PROTO}"
)

# 4) Library that wraps the generated code (headers are in ${GEN})
# creates the custom commands (proto_lib) that know how to run protoc and 
# where to put the generated .pb.cc/.grpc.pb.cc.
add_library(proto_lib STATIC
  ${GEN}/matching_engine.pb.cc
  ${GEN}/matching_engine.grpc.pb.cc
)
target_include_directories(proto_lib PUBLIC ${GEN})
# link against the vcpkg-provided imported targets
target_link_libraries(proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# 5) Server and client executables that link the generated library
add_executable(server src/server/server.cpp)
target_link_libraries(server PRIVATE proto_lib storage gRPC::grpc++ protobuf::libprotobuf)

add_executable(client src/client/client.cpp)
target_link_libraries(client PRIVATE proto_lib storage gRPC::grpc++ protobuf::libprotobuf)

# --- SQLiteCpp
find_package(SQLiteCpp CONFIG REQUIRED)

# Your storage library (adjust paths as needed)
add_library(storage STATIC
  src/storage/storage.cpp
)
target_include_directories(storage
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(storage PUBLIC cxx_std_20)
target_link_libraries(storage
  PRIVATE SQLiteCpp
  # If your toolchain complains about unresolved sqlite3 symbols, also add:
  # PRIVATE unofficial::sqlite3::sqlite3
)

# Link storage into your binaries
target_link_libraries(server PRIVATE storage)
target_link_libraries(client PRIVATE storage)