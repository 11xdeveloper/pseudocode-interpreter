# pseudocode-interpreter

An interpreter for Cambridge International AS & A Level Computer Science (9618) pseudocode, compiled to WebAssembly. It runs in web pages, in Web Workers and in Node.js, and as a command.

It covers the whole language in the *Pseudocode Guide for Teachers*: arrays, records, enumerated, pointer and set types, procedures and functions, text and random files, and classes. The [language specification](https://github.com/11xdeveloper/pseudocode-interpreter/blob/main/spec.md) says exactly what it accepts and lists every error and warning.

```
npm install pseudocode-interpreter
```

## In code

```js
import { runPseudocode } from 'pseudocode-interpreter';

const result = await runPseudocode(`
DECLARE Name : STRING
INPUT Name
OUTPUT "Hello, ", Name
`, {
	input: 'Ada\n',                      // all of the program's input, given up front
	args: ['--max-steps', '1000000'],    // any of the command line options
	files: { 'Scores.txt': '12\n15\n' }, // files for the program to read
	maxOutput: 1024 * 1024,              // stop with R015 after this many bytes of output (default 64 MiB)
});

result.exitCode; // 0, 1 for errors found before running, 2 for runtime errors
result.stdout;   // "Hello, Ada\n"
result.stderr;   // error and warning messages
result.files;    // { name: text }: the files in the program's folder afterwards
```

Each call runs the program in a fresh interpreter, so nothing carries over from one run to the next. TypeScript types are included.

The `args` are the same as on the command line (below). `--max-steps` is worth passing whenever the program comes from a user, so that an endless loop stops with an error instead of running for ever. `maxOutput` limits the output: a program that outputs more is stopped with runtime error R015, and `stdout` holds its output up to the limit.

### In a web page

The package is an ES module with a `.wasm` file beside it, which it finds relative to its own URL. Bundlers that understand `new URL('…', import.meta.url)`, such as Vite, webpack 5 and Parcel, copy the `.wasm` file into the build for you. Without a bundler, serve the package's files over HTTP (not as `file://` pages) and import `pseudocode.mjs`.

A run blocks the thread it is on until the program ends, so a page should run programs in a [Web Worker](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API):

```js
// worker.js, started with new Worker(new URL('./worker.js', import.meta.url), { type: 'module' })
import { runPseudocode } from 'pseudocode-interpreter';

onmessage = async ({ data }) => postMessage(await runPseudocode(data.source, data.options));
```

In a page, the interpreter differs from the command line in four ways:

- **Input is given up front.** `INPUT` reads from `input`; once that runs out, `INPUT` stops with R014, as it does at the end of standard input.
- **Files are in memory** and last for one run. Pass them in with `files` and read what the program wrote from `result.files`.
- **Recursion is shallower.** A page cannot enlarge the browser's stack, so calls can nest only about 1,000 to 1,500 deep, depending on the browser. Going deeper is R011, reported without the line and call chain.
- **Output is limited**, to 64 MiB unless `maxOutput` says otherwise. A program that outputs more stops with R015, and `stdout` keeps its output up to the limit.

## On the command line

```
npx pseudocode-interpreter program.pseudo
```

or, once installed, `pseudocode program.pseudo`. It reads standard input, reads and writes real files relative to the program's folder, and calls can nest 10,000 deep, as in the native interpreter.

```
Usage: pseudocode [options] [file]

  --strict        every variable must be declared
  --defaults      variables start as 0, 0.0, "", FALSE and so on
  --max-steps N   stop after N steps (catches endless loops)
  --max-depth N   allow calls to nest N deep (default 10000)
  --seed N        make RAND give the same numbers on every run
  --tokens        print the tokens instead of running
  --ast           print the syntax tree instead of running
  --types         print the defined types instead of running
```

The exit status is 0 if the program ran to the end, 1 for errors found before running, and 2 for runtime errors.

## Requirements

Node.js @PSEUDOCODE_NODE_ENGINES@, or a browser with WebAssembly exception handling: Chrome and Edge 95, Firefox 100 or Safari 15.2 and later.

## Licence

MIT
