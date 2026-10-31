// Runs PseudocodeInterpreter.js, the interpreter compiled to WebAssembly,
// with Node.js: node pseudocode-cli.mjs [options] [file]
//
// WebAssembly calls use the JavaScript engine's own stack as well as their
// own (STACK_SIZE in Wasm.cmake), and Node's main thread has about 1 MB of
// it: about 1500 nested pseudocode calls. A worker thread can be given a
// stack as large as the native interpreter's, so programs can recurse as
// deeply, and the interpreter's own check stops runaway recursion with R011
// before the engine's stack runs out.

import { Worker } from 'node:worker_threads';

const StackSizeMb = 512;

const worker = new Worker(new URL('./PseudocodeInterpreter.js', import.meta.url), {
	argv: process.argv.slice(2),
	resourceLimits: { stackSizeMb: StackSizeMb },
});
worker.on('exit', code => {
	process.exitCode ??= code;
});
worker.on('error', error => {
	console.error(`Internal error in the interpreter: ${error.message}`);
	process.exitCode = 2;
});
