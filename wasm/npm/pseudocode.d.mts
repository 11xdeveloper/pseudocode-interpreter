// Types for pseudocode.mjs, the interpreter compiled to WebAssembly.

export interface RunOptions {
	/**
	 * All of the program's input, given up front: each INPUT reads one line.
	 * Once it runs out, INPUT stops the program with runtime error R014.
	 */
	input?: string;
	/** Command line options, such as `['--seed', '42']` or `['--max-steps', '1000000']`. */
	args?: string[];
	/** Text files to put in the program's folder before it runs, as `{ name: text }`. */
	files?: Record<string, string>;
	/** The program's file name, used in error messages. Defaults to `program.pseudo`. */
	fileName?: string;
}

export interface RunResult {
	/** 0 if the program ran to the end, 1 for errors found before running, 2 for runtime errors. */
	exitCode: number;
	/** What the program output. */
	stdout: string;
	/** Error and warning messages. */
	stderr: string;
	/** The text files in the program's folder afterwards, including those it wrote, as `{ name: text }`. */
	files: Record<string, string>;
}

/**
 * Runs a pseudocode program given as text, in a fresh interpreter, and
 * returns its results. A run blocks the thread it is on until the program
 * ends, so a web page should call this from a Web Worker, or pass
 * `--max-steps` so that an endless loop stops with an error.
 */
export function runPseudocode(source: string, options?: RunOptions): Promise<RunResult>;
