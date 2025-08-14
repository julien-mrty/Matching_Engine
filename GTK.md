Compiler GCC 14.2
- From this I can chose for each build the C++ language standard (C++17, C++20, etc.)
- GG 14.2 supports both

vcpkg : package manager, used to download proto and grpc

cmake DCMAKE_TOOLCHAIN_FILE points to : vcpkg.cmake
- This tell which compilers/SDKs/paths CMake should use to compile/link
- Generator used : Visual Studio 2022
  - Generator is which build system files CMake produces



What is ABI?
ABI = Application Binary Interface.
It’s the contract at the binary level between separately compiled pieces of code (your app and a shared/static library, or two .cpp files). It covers things like:

how functions are named in the object file (name mangling),

calling convention (how args/returns are passed, who pops the stack),

object layout in memory (data member offsets, vtable layout, alignment),

exception and RTTI formats,

how inlining/linkage works across TUs.

If you change a class’s private data members that are visible in a public header, you usually change its object layout, and that can break ABI (code compiled against the old layout may crash or misbehave when linked to the new one).

API vs ABI:

API: the source-level interface (headers, function signatures).

ABI: the binary-level interface (how compiled code interoperates)




Two ways to structure a class
1) “Normal” header + cpp (no PIMPL)
Header exposes the class and its private members (or at least needs to include the headers of private members’ types).

Header (foo.h)

cpp
Copy
Edit
#pragma once
#include <string>

class Foo {
public:
  void do_work();
private:
  int count_;
  std::string name_;  // header must include <string>
};
Source (foo.cpp)

cpp
Copy
Edit
#include "foo.h"
void Foo::do_work() { /* ... */ }
Pros:

Simple, no heap allocation/indirection.

Best performance (compiler can inline more).

Cons:

Changes to private members force recompilation of everything including this header.

Exposes transitive headers to users (more build coupling).

ABI breaks easily if used across library boundaries.

This is what you meant by “put only the function signatures in the header and implement with void Storage::method(...) in the .cpp”. That’s the standard way.

2) PIMPL (Private Implementation) with std::unique_ptr<Impl>
You hide all private data in an internal Impl type that lives in the .cpp.
The header only shows the public surface and a pointer p_ to the hidden implementation.

Header (foo.h)

cpp
Copy
Edit
#pragma once
#include <memory>

class Foo {
public:
  Foo();
  ~Foo();                      // declared (definition in .cpp!)

  Foo(Foo&&) noexcept;         // move allowed
  Foo& operator=(Foo&&) noexcept;

  Foo(const Foo&) = delete;    // copying is harder with PIMPL—often deleted
  Foo& operator=(const Foo&) = delete;

  void do_work();

private:
  struct Impl;                 // forward declaration (incomplete type)
  std::unique_ptr<Impl> p_;    // pointer to hidden implementation
};
Source (foo.cpp)

cpp
Copy
Edit
#include "foo.h"
#include <string>
#include <vector>

// Now we define the real guts:
struct Foo::Impl {
  int count = 0;
  std::string name;
  std::vector<int> cache;
};

Foo::Foo() : p_(std::make_unique<Impl>()) {}
Foo::~Foo() = default;              // defined here, after Impl is complete

Foo::Foo(Foo&&) noexcept = default; // unique_ptr move
Foo& Foo::operator=(Foo&&) noexcept = default;

void Foo::do_work() {
  p_->count++;
  // ...
}
Why the destructor defined in the .cpp?
Because std::unique_ptr<Impl> needs the complete type of Impl when it destroys it.
If you defaulted the destructor in the header, it would be inline there—where Impl is incomplete—and that’s ill-formed. Declaring ~Foo(); in the header and defining it in the .cpp solves this.

PIMPL: Pros / Cons
Pros

Stable ABI: changing private data doesn’t change the header or object layout seen by users.

Faster incremental builds: users don’t recompile when you change internals.

Information hiding: the header doesn’t pull in heavy dependencies.

Cons

Small runtime overhead: one heap allocation + one pointer indirection.

Slightly more boilerplate (ctor/dtor, move ops, forward decl).

Copying is non-trivial (you either delete copy or implement deep copy).

When to use which?
Internal / performance-critical types used only inside one module: normal header+cpp is fine.

Public library surface shared across many TUs/binaries or expected to evolve: PIMPL shines (ABI stability, smaller rebuilds).

Templates: usually cannot hide implementation in a .cpp (must be visible to all TUs), so PIMPL isn’t a fit.




Why forward declare instead of include?
Faster builds / smaller dependency graph
If engine.h includes storage.h, and storage.h includes SQLite headers, then every file that includes engine.h indirectly parses SQLite too. A single forward declaration makes the header lightweight.

Encapsulation
Users of MatchingEngine don’t need to know Storage’s internals (or even that it uses SQLite). Forward declaring avoids leaking that detail.

Fewer rebuilds
If storage.h changes, any header that includes it causes a large recompile wave. With a forward decl, only engine.cpp recompiles.

Break cycles
If A needs a pointer to B and B needs a pointer to A, forward declarations on both sides break circular includes cleanly.

When you must include the header (need a complete type)
You cannot get by with only a forward decl if your header:

stores the type by value (needs sizeof(T)),

derives from it (class X : public Storage),

calls its methods inline in the header,

declares members that contain it by value (e.g., std::vector<Storage>),

or defines a destructor inline while holding std::unique_ptr<Storage>.

Practical rule of thumb
If the header only mentions a type (as a parameter, return type, pointer, or reference), forward declare it.

If the header uses the type’s layout/behavior (value members, inline calls, inheritance), include it.

Put the #include in the .cpp where you actually need the full definition.