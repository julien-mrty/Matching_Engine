cmake_minimum_required(VERSION 3.31)
project(TradingEngine)

# 1. Basic setup
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 2. Find required packages
set(Protobuf_ROOT "C:/Program Files/Protobuf")
find_package(Protobuf REQUIRED)

set(gRPC_ROOT "C:/Users/julie/Desktop/All/Important/Programmation/Tools/grpc/.build/Release")
find_package(gRPC REQUIRED)

# 3. Generate gRPC/protobuf files
set(PROTO_FILE proto/trading_engine.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILE})

# 4. Create server executable
add_executable(server
    src/server/main.cpp
    src/server/engine_impl.cpp
    ${PROTO_SRCS} ${PROTO_HDRS}
    ${GRPC_SRCS} ${GRPC_HDRS}
)

# 5. Link dependencies
target_link_libraries(server
    gRPC::grpc++
    protobuf::libprotobuf
)

# 6. Include generated files directory
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)