# Granda Programming Language

> A fast, efficient, object-oriented language with snake_case conventions — compiled to native binaries via C.

**Author:** PocketPixels  
**Version:** 0.2.0  
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
| Vtable polymorphism | ✅ |
| Interfaces / traits (with default methods) | ✅ |
| Module system (`import`, `--I`) | ✅ |
| Standard library (35+ functions) | ✅ |

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

### Traits (Interfaces)

Traits define a contract that classes can implement. Methods can be abstract (no body) or have default implementations.

```granda
trait Drawable {
    fn area(self) -> float                         # abstract — must be implemented
    fn description(self) -> str {                  # default — optional override
        return "A drawable shape"
    }
}

class Circle implements Drawable {
    radius: float

    fn area(self) -> float {
        return 3.14159 * self.radius * self.radius
    }
    # description() is inherited from the trait default
}
```

### Virtual Dispatch

Mark methods `virtual` for dynamic dispatch through a vtable. Subclasses use `override` to provide their own implementation.

```granda
class Shape {
    color: str

    virtual fn area(self) -> float {
        return 0.0
    }
}

class Rectangle extends Shape {
    width: float
    height: float

    override fn area(self) -> float {
        return self.width * self.height
    }
}

# Polymorphic function — dispatches via vtable
fn print_area(s: Shape) {
    println(str(s.area()))
}
```

### Modules

Split code across files. Place helper files in an include directory and reference them with `import`.

```granda
# math_utils.gra (in the include directory)
pub fn clamp(val: float, lo: float, hi: float) -> float {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
}
```

```granda
# main.gra
import math_utils

fn main() {
    let c = math_utils::clamp(5.0, 0.0, 3.0)
    println(str(c))  # 3
}
```

```bash
./granda main.gra -I include_dir/ -o main
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

#### IO

| Function | Description |
|----------|-------------|
| `print(x)` | Print without newline |
| `println(x)` | Print with newline |
| `read_file(path)` | Read file contents as `str` |
| `write_file(path, data)` | Write `data` to file |
| `file_exists(path)` | Returns `true` if file exists |
| `read_line()` | Read a line from stdin |
| `args()` | Get CLI arguments array |

#### Math

| Function | Description |
|----------|-------------|
| `sin(x)` | Sine (radians) |
| `cos(x)` | Cosine (radians) |
| `tan(x)` | Tangent (radians) |
| `sqrt(x)` | Square root |
| `pow(base, exp)` | Power |
| `abs(x)` | Absolute value (`int` or `float`) |
| `floor(x)` | Floor |
| `ceil(x)` | Ceil |
| `round(x)` | Round to nearest int |
| `min(a, b)` | Minimum |
| `max(a, b)` | Maximum |
| `log(x)` | Natural logarithm |
| `log2(x)` | Base-2 logarithm |
| `log10(x)` | Base-10 logarithm |

#### String

| Function | Description |
|----------|-------------|
| `substr(s, start, len)` | Extract substring |
| `index_of(s, sub)` | Find index of substring |
| `contains(s, sub)` | Check if substring exists |
| `to_upper(s)` | Uppercase |
| `to_lower(s)` | Lowercase |
| `trim(s)` | Strip whitespace |
| `split(s, sep)` | Split by separator |
| `replace(s, old, new)` | Replace substring |
| `starts_with(s, prefix)` | Check prefix |
| `ends_with(s, suffix)` | Check suffix |
| `char_at(s, i)` | Get character at index |
| `str_to_int(s)` | Parse string to `int` |
| `str_to_float(s)` | Parse string to `float` |

#### Conversion & Utility

| Function | Description |
|----------|-------------|
| `str(x)` | Convert any value to `str` |
| `int(x)` | Convert to `int` |
| `float(x)` | Convert to `float` |
| `len(x)` | Length of `str` or array |
| `push(arr, val)` | Append to array |
| `pop(arr)` | Remove and return last element |
| `sort(arr)` | Sort array in place |
| `reverse(arr)` | Reverse array in place |
| `assert(cond)` | Panic if `cond` is false |

#### Random & Time

| Function | Description |
|----------|-------------|
| `rand_int(lo, hi)` | Random integer in range |
| `rand_float()` | Random float in [0, 1) |
| `rand_seed(seed)` | Seed the RNG |
| `time_now()` | Current timestamp (seconds) |
| `time_sleep(secs)` | Sleep for N seconds |

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
  -I <dir>          Include directory for module imports
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
│   ├── main.cpp          # CLI driver, module loading
│   ├── lexer.h/cpp       # Tokenizer (VIRTUAL, OVERRIDE, TRAIT, IMPLEMENTS)
│   ├── ast.h             # AST node definitions (TraitDecl, TypeKind::TRAIT)
│   ├── parser.h/cpp      # Recursive-descent parser
│   ├── type_checker.h/cpp # Type inference, trait validation, vtable checks
│   ├── codegen.h/cpp     # C99 code generator (vtables, trait dispatch, modules)
├── runtime/
│   ├── granda_rt.h       # Runtime API (35+ stdlib functions)
│   └── granda_rt.c       # RC-GC, strings, arrays, I/O, math, time
└── examples/
    ├── hello.gra
    ├── classes.gra
    ├── traits.gra
    ├── polymorphism.gra
    ├── stdlib_test.gra
    ├── trait_defaults.gra
    ├── modules_test.gra
    └── modules/
        └── math_utils.gra
```

---

## How It Works

```
source.gra (+ imported modules)
    │
    ▼
 Lexer          tokenizes source into a flat token stream
    │
    ▼
 Parser         builds a typed AST via recursive descent
    │
    ▼
 TypeChecker    resolves inference, validates types, checks inheritance,
                validates trait implementations, vtable analysis
    │
    ▼
 Codegen        emits C99 with vtable structs, trait dispatch,
                module-prefixed names, and RC macros for GC objects
    │
    ▼
 cc / gcc       compiles the generated C to a native binary
    │
    ▼
 native binary  runs directly on the target machine
```

The GC is **reference counting** — every heap object (`str`, array, class instance) carries a 32-bit `ref_count` in its header. The runtime macros handle retain/release automatically in the generated C.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

*Made with ❤️ by [PocketPixels](https://github.com/LittlePixels)*
