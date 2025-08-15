cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ------------ Protobuf/gRPC ------------
# Find packages provided by vcpkg (you pass the toolchain at configure time)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Where the .proto lives and where generated code should go
set(PROTO ${CMAKE_SOURCE_DIR}/proto/matching_engine.proto)
set(GEN   ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN})

# Run protoc + gRPC C++ plugin to generate sources (create *.pb.cc/h and *.grpc.pb.cc/h)
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

# Library that wraps the generated code (headers are in ${GEN})
# creates the custom commands (proto_lib) that know how to run protoc and 
# where to put the generated .pb.cc/.grpc.pb.cc.
add_library(proto_lib STATIC
  ${GEN}/matching_engine.pb.cc
  ${GEN}/matching_engine.grpc.pb.cc
)

# Publish generated headers to dependents
target_include_directories(proto_lib PUBLIC ${GEN})
# link against the vcpkg-provided imported targets
target_link_libraries(proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)


# ------------ Storage ------------
# SQLiteCpp
find_package(SQLiteCpp CONFIG REQUIRED)

# Your storage library (adjust paths as needed)
add_library(storage STATIC
  src/storage/storage.cpp
)
target_compile_features(storage PUBLIC cxx_std_20)
target_link_libraries(storage PRIVATE proto_lib SQLiteCpp
  # If your toolchain complains about unresolved sqlite3 symbols, also add:
  # PRIVATE unofficial::sqlite3::sqlite3
)
target_include_directories(storage PUBLIC ${CMAKE_SOURCE_DIR}/include)


# ------------ Server ---------------
# Server executable that link the generated library
add_library(server_lib STATIC src/server/matching_engine_service.cpp)

add_executable(server src/server/main.cpp)

# Make include/ visible explicitly
target_include_directories(server_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(server_lib PUBLIC proto_lib storage gRPC::grpc++ protobuf::libprotobuf)
target_link_libraries(server PRIVATE server_lib)


# ------------ Client ------------
# Client executable that link the generated library
add_executable(client src/client/client.cpp)
# Make include/ visible explicitly
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(client PRIVATE proto_lib storage gRPC::grpc++ protobuf::libprotobuf)


# ------------------------------------------------ Tests ------------------------------------------------
enable_testing()
find_package(GTest CONFIG REQUIRED)

# ------------ Unit tests ------------
add_executable(server_unit_tests tests/test_price.cpp)
target_include_directories(server_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(server_unit_tests
  PRIVATE proto_lib storage server_lib GTest::gtest GTest::gtest_main
)
add_test(NAME server_unit_tests COMMAND $<TARGET_FILE:server_unit_tests>)
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --output-on-failure
  DEPENDS server_unit_tests
)

# ------------ Integration tests ------------
add_executable(server_integration_tests
  tests/test_submit_order.cpp
)
target_include_directories(server_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(server_integration_tests
  PRIVATE
    server_lib                # <-- contains MatchingEngineServiceImpl symbols
    proto_lib                 # generated *.pb.cc
    storage
    gRPC::grpc++
    protobuf::libprotobuf
    SQLiteCpp
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME server_integration_tests COMMAND $<TARGET_FILE:server_integration_tests>)
