# WebAssembly build

The interpreter compiles to WebAssembly with [Emscripten](https://emscripten.org) (tested with version 6.0; `brew install emscripten` on macOS):

```
emcmake cmake -S . -B build-wasm
cmake --build build-wasm
```

This builds the same interpreter in two forms. It is an optimised (Release) build unless `CMAKE_BUILD_TYPE` says otherwise.

## On the command line, with Node.js

`build-wasm/pseudocode` runs the interpreter with Node.js, and takes the same options as the native one:

```
build-wasm/pseudocode --seed 42 program.pseudo < input.txt
```

It behaves as the native interpreter does: files are real files, relative to the program's folder, the exit status is the same, and calls can nest 10,000 deep. It runs the program in a worker thread with a 512 MB stack (`cli.mjs`), because Node's main thread has room for only about 1,500 nested calls.

## In a web page

`pseudocode.mjs` exports one function, which runs a program given as text:

```js
import { runPseudocode } from './pseudocode.mjs';

const result = await runPseudocode(source, {
	input: 'Ada\n',                      // all of the program's input, given up front
	args: ['--max-steps', '1000000'],    // any of the command line options
	files: { 'Scores.txt': '12\n15\n' }, // files for the program to read
});
result.exitCode; // 0, 1 for errors found before running, 2 for runtime errors
result.stdout;   // the program's output
result.stderr;   // error and warning messages
result.files;    // { name: text }: the files in the program's folder afterwards
```

Copy `pseudocode.mjs`, `pseudocode-core.mjs` and `pseudocode-core.wasm` from `build-wasm` to the site. They must be served over HTTP, not opened as `file://` pages. To try the example page:

```
python3 -m http.server -d build-wasm 8000
```

and open http://localhost:8000/example.html.

In a page, the interpreter differs from the native one in three ways:

- **Input is given up front.** `INPUT` reads from `input`; once that runs out, `INPUT` stops with R014, as it does at the end of standard input.
- **Files are in memory** and last for one run. Each run starts with a fresh interpreter, so nothing carries over from one run to the next.
- **Recursion is shallower.** A page cannot enlarge the browser's stack, so calls can nest only about 1,000 to 1,500 deep, depending on the browser. Going deeper is R011, but without the line and call chain, since the browser stops the interpreter before its own check does.

A run blocks the thread it is on until the program ends, so a real page should call `runPseudocode` from a worker. The example page passes `--max-steps` instead, so an endless loop stops with an error rather than hanging the page.

## Tests

The unit tests run under Node.js. The program tests need a native build's runner, because WebAssembly cannot start other programs:

```
cmake -S . -B build && cmake --build build
emcmake cmake -S . -B build-wasm -DPSEUDOCODE_TEST_RUNNER=$PWD/build/tests/program_tests
cmake --build build-wasm
ctest --test-dir build-wasm
```

These test the command line form. The web module was checked by hand, in Node and in Chrome, with the example page and the cases above.
