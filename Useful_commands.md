# Generate gRPC client/server code and Protocol Buffers serialization code from .proto file
protoc --grpc_out=.\generated --cpp_out=.\generated --plugin=protoc-gen-grpc=C:\Users\julie\Desktop\All\Important\Programmation\Tools\grpc\.build\Release\grpc_cpp_plugin.exe .\proto\matching_engine.proto


# Build commands (Windows, VS 2022, vcpkg)
## Note: pass the toolchain at configure time, not inside CMakeLists.txt.

### Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Release

### Build
cmake --build build --config Release -j

### Run (paths may vary)
##### Terminal 1 (server)
./build/Release/server 0.0.0.0:50051

##### Terminal 2 (client)
./build/Release/client localhost:50051 "ping from client"
 -> [client] reply: server echo: ping from client