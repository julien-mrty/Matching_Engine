# Matching Engine

## Why & How to Install vcpkg, Protobuf, and gRPC (C++)

### Why These Tools?
- **vcpkg**: A C/C++ package manager that installs libraries (headers, libs, tools) and plugs them into CMake automatically. No manual path juggling.  
- **Protobuf**: Schema + code generation for messages. Provides `protoc` and the `libprotobuf` runtime.  
- **gRPC**: RPC framework built on HTTP/2 that uses Protobuf messages. Provides `grpc++` libraries and the codegen plugin.  

Using vcpkg avoids common Windows issues: missing headers, wrong ABIs, and CMake not finding packages.

---

## Windows Quickstart (VS 2022)

### 1 Prerequisites
- Visual Studio 2022 (**Desktop C++** workload)
- CMake ≥ 3.20
- Git, PowerShell

### 2 Install vcpkg + Libraries
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe install protobuf:x64-windows grpc:x64-windows
C:\vcpkg\vcpkg.exe install sqlitecpp
C:\vcpkg\vcpkg.exe install Gtest

### 3 Configure Your CMake Project (with vcpkg Toolchain)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_BUILD_TYPE=Release

#### Why These Flags?

##### Visual Studio generator (`-G "Visual Studio 17 2022" -A x64`)
Generates a VS solution/projects for 64-bit. VS is multi-config: choose Debug/Release at build time.

##### Toolchain file (`-DCMAKE_TOOLCHAIN_FILE=...vcpkg.cmake`)
Integrates vcpkg so `find_package(Protobuf gRPC)` gets headers, libs, `protoc`, and the gRPC plugin automatically.

##### Triplet (`-DVCPKG_TARGET_TRIPLET=x64-windows`)
Ensures vcpkg selects binaries matching `-A x64`.

##### Build type (`-DCMAKE_BUILD_TYPE=Release`)
Hint for single-config generators; harmless here. With VS you still pass `--config Release` when building.

---

**Note:**  
- PowerShell line continuation uses the backtick `` ` ``.  
- In **cmd.exe** use `^`.  
- In **bash** use `\`.

---

# Build (Generate Protobuf/gRPC Code + Compile)

**Generate code & build for everything:**
```bash
cmake --build build --config Release -j
```

**Generate code & build the proto library:**
```bash
cmake --build build --target proto_lib --config Release -j
```

**Build the apps:**
Building server or client will automatically build proto_lib first beacause they have target_link_libraries(server PRIVATE proto_lib) in CmakeLists.txt
```bash
cmake --build build --target server --config Release -j
cmake --build build --target client --config Release -j
```

##### Target: `proto_lib`
Building this target triggers the **protoc + gRPC codegen step** (creates `*.pb.cc/.h` and `*.grpc.pb.cc/.h`).  
Building **server** or **client** also triggers it (they link `proto_lib`).

##### `--config Release`
Required with Visual Studio solutions to pick the configuration.

##### `-j`
Parallel build (uses all cores).

---

# Run (Two Terminals)

**Terminal — server:**
```bash
./build/Release/server 0.0.0.0:50051
```

**Terminal — client:**
```bash
./build/Release/client.exe localhost:50051 C1 SYM BUY LIMIT 10050 2 10
```
_Output:_
```
[client] accepted order_id=1
```

```bash
./build/Release/client.exe localhost:50051 C2 SYM SELL MARKET 0 0 5
```
_Output:_
```
[client] accepted order_id=2
```

---

# Tests

**Run the unit and integration tests:**
```bash
ctest --test-dir build -C Release -V
```

---

# Common Pitfalls & Quick Fixes

## Missing generated headers (`*.pb.h`, `*.grpc.pb.h`)
- Build `proto_lib` (or `server`/`client`) to trigger codegen.
- Check `build/generated/`.

## VS Code squiggles on `<grpcpp/grpcpp.h>`
Editor can’t see include paths. Add:
```
C:\vcpkg\installed\x64-windows\include
${workspaceFolder}\build\generated
```

Or set in `.vscode/settings.json`:
```json
"C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
"C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json"
```
And configure with:
```bash
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Config/arch mismatch
Keep:
- `-A x64`
- Triplet `x64-windows`
- Use `--config Debug|Release` consistently.

## Switching toolchain/triplet
- Delete `build/`  
- Re-run **Configure**.

---

# TL;DR
Install with **vcpkg**, point **CMake** to the vcpkg toolchain, `find_package(Protobuf gRPC)`, run Protobuf/gRPC codegen in CMake, and link the imported targets.  
This keeps your C++ build **clean** and **portable**.
