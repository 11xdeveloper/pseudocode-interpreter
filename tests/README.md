# Tests

Two kinds of test, both run by `ctest`:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Add `-j8` to run them in parallel, and `--output-on-failure` to see why one failed.

## Unit tests (`unit/`)

C++ tests of the core library, called directly: values and operators, the type table, the environment and the built-in functions. They use the small framework in `unit/TestFramework.h`:

```cpp
TEST_CASE("operations", div_and_mod_truncate_toward_zero) {
	CHECK_EQ(describe(applyBinary(BinaryOp::IntDiv, I(-7), I(2))), "INTEGER:-3");
	CHECK_ERROR("R004", applyBinary(BinaryOp::Mod, I(1), I(0)));
}
```

Each suite (`operations`, `values`, `types`, `environment`, `builtins`) is one ctest test. Run one directly with `build/tests/unit_tests values`. A new suite needs adding to the list in `CMakeLists.txt`.

## Program tests (`programs/`)

Each `NAME.pseudo` is a whole program, run by the interpreter and compared exactly with `NAME.expected`: the exit status, standard output and standard error. They are grouped by what they test: `lexical`, `syntax`, `semantic`, `runtime` and `warnings` cover every error and warning code in spec section 12; `language`, `builtins` and `files` cover what programs can do.

Comment lines at the top of a program control how it runs:

```
// ARGS: --seed 42 --strict
// STDIN: a line of input
// STDIN-CRLF: a line of input ending with \r\n
```

A folder `NAME.files` next to the program holds input files; they are copied into the folder the program runs in. Each program runs in a fresh temporary folder under its own file name, so error messages read `NAME.pseudo:3:1: ...` wherever the tests are run.

To add a program test, write the `.pseudo` file, then create its `.expected` file and read it carefully before committing: the test is only as right as that file.

```
build/tests/program_tests --update --interpreter build/PseudocodeInterpreter tests/programs/language/new_test.pseudo
```

After a change that is meant to alter output, such as rewording a message, rewrite every expected file and review the diff:

```
cmake --build build --target update_expected
git diff tests/programs
```

`--keep` leaves each program's temporary folder in place and prints where it is, to look at the files it wrote.

## Sanitizers

To check for memory errors and undefined behaviour, build everything with AddressSanitizer and UBSan and run the same tests:

```
cmake -S . -B build-asan -DPSEUDOCODE_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan
```

The recursion-limit test uses `--max-depth 500`: AddressSanitizer cannot unwind an error through the default 10,000 nested calls.

## WebAssembly

The same tests run against the WebAssembly build, with a native build's `program_tests` (see `wasm/README.md`):

```
emcmake cmake -S . -B build-wasm -DPSEUDOCODE_TEST_RUNNER=$PWD/build/tests/program_tests
cmake --build build-wasm
ctest --test-dir build-wasm
```
