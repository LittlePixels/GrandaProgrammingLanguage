# Granda Programming Language

> A fast, efficient, object-oriented language with snake_case conventions — compiled to native binaries via C.

**Author:** PocketPixels  
**Version:** 0.1.0  
**Language:** C++17  
**License:** MIT  

---

## Overview

Granda is a statically-typed, object-oriented programming language designed around clarity and speed. It enforces **snake_case** naming throughout, features **single-inheritance OOP**, **type inference**, and compiles directly to native machine code by emitting optimized C99 which is then compiled by your system's C compiler.

Memory is managed automatically using **reference counting** — no manual `free()`, no stop-the-world pauses.

---

## Language at a Glance

```granda
# Variables — type inferred or explicit
let name: str = "Granda"
let version = 1           # inferred: int

# Functions
fn add(a: int, b: int) -> int {
    return a + b
}

# Classes with inheritance
class Shape {
    color: str

    fn area(self) -> float {
        return 0.0
    }
}

class Rectangle extends Shape {
    width: float
    height: float

    fn new(color: str, w: float, h: float) -> Rectangle {
        return Rectangle { color: color, width: w, height: h }
    }

    fn area(self) -> float {
        return self.width * self.height
    }
}

# Main entry point
fn main() {
    let rect = Rectangle::new("blue", 5.0, 3.0)
    println("Area: " + str(rect.area()))

    # Range-based for loop
    for i in 0..10 {
        println(str(i))
    }

    # Array iteration
    let scores: [int] = [10, 20, 30]
    for s in scores {
        println(str(s))
    }
}
```

---

## Features

| Feature | Status |
|---|---|
| Snake_case enforcement | ✅ |
| Static typing + inference | ✅ |
| Classes & single inheritance | ✅ |
| Reference-counted GC | ✅ |
| Native binary output | ✅ |
| Range `for` loops (`0..n`) | ✅ |
| Array `for` loops | ✅ |
| String concatenation (`+`) | ✅ |
| Built-in `print` / `println` | ✅ |
| Built-in `len`, `str`, `int`, `float` | ✅ |
| `break` / `continue` | ✅ |
| Compound operators (`+=`, `-=`, `*=`, `/=`) | ✅ |
| Vtable polymorphism | 🔜 planned |
| Interfaces / traits | 🔜 planned |
| Module system | 🔜 planned |
| Standard library | 🔜 planned |

---

## Syntax Reference

### Types

| Granda | C equivalent | Notes |
|--------|-------------|-------|
| `int` | `int64_t` | 64-bit signed integer |
| `float` | `double` | 64-bit floating point |
| `str` | `GrandaStr*` | Heap-allocated, ref-counted |
| `bool` | `int` | `true` / `false` literals |
| `[T]` | `GrandaArray*` | Dynamic array, ref-counted |
| `ClassName` | `ClassName*` | Ref-counted heap object |

### Variables

```granda
let x = 42              # inferred: int
let y: float = 3.14     # explicit type
let name: str = "hi"
```

### Functions

```granda
fn greet(name: str) -> str {
    return "Hello, " + name + "!"
}
```

### Classes

```granda
class Animal {
    name: str
    age: int

    fn speak(self) -> str {
        return "..."
    }
}

class Dog extends Animal {
    breed: str

    fn new(name: str, age: int, breed: str) -> Dog {
        return Dog { name: name, age: age, breed: breed }
    }

    fn speak(self) -> str {
        return self.name + " says: Woof!"
    }
}
```

### Control Flow

```granda
# if / elif / else
if x > 10 {
    println("big")
} elif x > 5 {
    println("medium")
} else {
    println("small")
}

# while
while x > 0 {
    x -= 1
}

# for — integer range
for i in 0..100 {
    println(str(i))
}

# for — array
let items: [str] = ["a", "b", "c"]
for item in items {
    println(item)
}
```

### Built-in Functions

| Function | Description |
|----------|-------------|
| `print(x)` | Print without newline |
| `println(x)` | Print with newline |
| `str(x)` | Convert any value to `str` |
| `int(x)` | Convert to `int` |
| `float(x)` | Convert to `float` |
| `len(x)` | Length of `str` or array |
| `push(arr, val)` | Append to array |
| `assert(cond)` | Panic if `cond` is false |

---

## Building the Compiler

### Requirements

- CMake 3.16+
- A C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- A C99 compiler on the target machine (`cc`, `gcc`, or `clang`) — used at runtime to compile generated code

### Build

```bash
git clone https://github.com/LittlePixels/GrandaProgrammingLanguage.git
cd GrandaProgrammingLanguage
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

The `granda` compiler binary will be placed in the `build/` directory alongside the runtime files.

---

## Usage

```
granda <source.gra> [options]

Options:
  -o <output>       Output binary name (default: a.out)
  --emit-c          Print generated C source and exit
  --cc <cmd>        C compiler to use (default: cc)
  --runtime <dir>   Path to granda_rt.h / granda_rt.c
  -h / --help       Show help
```

### Compile and run a program

```bash
./granda examples/hello.gra -o hello
./hello
```

```bash
./granda examples/classes.gra -o shapes
./shapes
```

### Inspect generated C

```bash
./granda examples/hello.gra --emit-c
```

---

## Project Structure

```
GrandaProgrammingLanguage/
├── CMakeLists.txt
├── src/
│   ├── main.cpp          # CLI driver
│   ├── lexer.h/cpp       # Tokenizer
│   ├── ast.h             # AST node definitions
│   ├── parser.h/cpp      # Recursive-descent parser
│   ├── type_checker.h/cpp # Type inference + checking
│   ├── codegen.h/cpp     # C99 code generator
├── runtime/
│   ├── granda_rt.h       # Runtime API
│   └── granda_rt.c       # RC-GC, strings, arrays, I/O
└── examples/
    ├── hello.gra
    └── classes.gra
```

---

## How It Works

```
source.gra
    │
    ▼
 Lexer          tokenizes source into a flat token stream
    │
    ▼
 Parser         builds a typed AST via recursive descent
    │
    ▼
 TypeChecker    resolves inference, validates types, checks inheritance
    │
    ▼
 Codegen        emits C99 source with RC_ASSIGN macros for GC objects
    │
    ▼
 cc / gcc       compiles the generated C to a native binary
    │
    ▼
 native binary  runs directly on the target machine
```

The GC is **reference counting** — every heap object (`str`, array, class instance) carries a 32-bit `ref_count` in its header. The `RC_ASSIGN` macro handles retain/release automatically in the generated C.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

*Made with ❤️ by [PocketPixels](https://github.com/LittlePixels)*
