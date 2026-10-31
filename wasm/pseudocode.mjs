// Runs a pseudocode program in the interpreter compiled to WebAssembly, in a
// web page, a worker or Node.js:
//
//   import { runPseudocode } from './pseudocode.mjs';
//   const result = await runPseudocode(source, { input: '5\n', args: ['--seed', '42'] });
//   result.exitCode   0, 1 for errors found before running, 2 for runtime errors
//   result.stdout     what the program output
//   result.stderr     the error and warning messages
//   result.files      { name: text } for the files in the program's folder afterwards
//
// The program's input is all given up front. `files` puts text files in the
// program's folder before it runs, for OPENFILE ... FOR READ. Each run has a
// fresh interpreter, so nothing carries over from one run to the next.
// A run blocks the thread it is on until the program ends, so a page should
// run programs in a worker, or pass --max-steps to stop endless loops.
// `maxOutput` limits the program's output, in bytes: a program that outputs
// more is stopped with runtime error R015.

import createInterpreter from './pseudocode-core.mjs';

const Folder = '/program';
const DefaultMaxOutput = 64 * 1024 * 1024;

// A stream's bytes, one byte each. (A JavaScript array of bytes takes eight
// times the memory, and stops growing at about 112 million elements.)
// Pushing past the limit throws, and so does running out of memory, which
// the interpreter sees as a failed write: it stops with R015 rather than
// carrying on without its output.
class Bytes {
	constructor(limit = Infinity) {
		this.limit = limit;
		this.overLimit = false;
		this.data = new Uint8Array(4096);
		this.length = 0;
	}

	push(byte) {
		if (this.length >= this.limit) {
			this.overLimit = true;
			throw new RangeError('Output limit reached');
		}
		if (this.length === this.data.length) {
			const grown = new Uint8Array(Math.min(this.data.length * 2, Math.max(this.limit, this.data.length + 1)));
			grown.set(this.data);
			this.data = grown;
		}
		this.data[this.length++] = byte;
	}

	append(text) {
		for (const byte of new TextEncoder().encode(text)) {
			this.push(byte);
		}
	}

	text() {
		return new TextDecoder().decode(this.data.subarray(0, this.length));
	}
}

export async function runPseudocode(source, {
	input = '', args = [], files = {}, fileName = 'program.pseudo', maxOutput = DefaultMaxOutput,
} = {}) {
	if (typeof maxOutput !== 'number' || !(maxOutput >= 0)) {
		throw new TypeError(`maxOutput must be a number of bytes, 0 or more, not ${typeof maxOutput === 'string' ? `"${maxOutput}"` : maxOutput}`);
	}
	const inputBytes = new TextEncoder().encode(input);
	let inputPosition = 0;
	const stdout = new Bytes(maxOutput);
	const stderr = new Bytes();
	const interpreter = await createInterpreter({
		stdin: () => (inputPosition < inputBytes.length ? inputBytes[inputPosition++] : null),
		stdout: byte => byte !== null && stdout.push(byte),
		stderr: byte => byte !== null && stderr.push(byte),
	});

	const { FS } = interpreter;
	FS.mkdir(Folder);
	FS.chdir(Folder);
	for (const [name, content] of Object.entries(files)) {
		FS.writeFile(name, content);
	}
	FS.writeFile(fileName, source);

	let exitCode;
	try {
		exitCode = interpreter.callMain([...args, fileName]);
	}
	catch (error) {
		// WebAssembly calls also use the engine's own stack, which a page
		// cannot enlarge, so deep recursion can exhaust it before the
		// interpreter's own check (R011) does. Chrome and Safari throw a
		// RangeError, Firefox an InternalError.
		if (!(error instanceof RangeError || error?.name === 'InternalError')) {
			throw error;
		}
		stderr.append(`${fileName}: Runtime error R011: Too many nested calls (possible infinite recursion)\n`);
		exitCode = 2;
	}
	if (stdout.overLimit) {
		stderr.append(`The program's output was over the limit of ${maxOutput} bytes (the maxOutput option).\n`);
	}

	const after = {};
	for (const name of FS.readdir(Folder)) {
		if (name !== fileName && FS.isFile(FS.stat(name).mode)) {
			after[name] = FS.readFile(name, { encoding: 'utf8' });
		}
	}
	return { exitCode, stdout: stdout.text(), stderr: stderr.text(), files: after };
}
