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

import createInterpreter from './pseudocode-core.mjs';

const Folder = '/program';

export async function runPseudocode(source, { input = '', args = [], files = {}, fileName = 'program.pseudo' } = {}) {
	const inputBytes = new TextEncoder().encode(input);
	let inputPosition = 0;
	const stdout = [];
	const stderr = [];
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
		const message = `${fileName}: Runtime error R011: Too many nested calls (possible infinite recursion)\n`;
		stderr.push(...new TextEncoder().encode(message));
		exitCode = 2;
	}

	const after = {};
	for (const name of FS.readdir(Folder)) {
		if (name !== fileName && FS.isFile(FS.stat(name).mode)) {
			after[name] = FS.readFile(name, { encoding: 'utf8' });
		}
	}
	const decoder = new TextDecoder();
	return {
		exitCode,
		stdout: decoder.decode(new Uint8Array(stdout)),
		stderr: decoder.decode(new Uint8Array(stderr)),
		files: after,
	};
}
