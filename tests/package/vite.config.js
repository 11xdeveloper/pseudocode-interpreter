// Builds app/ into dist/, with relative paths so it can be served anywhere.
// The build fails unless Vite found the package's .wasm file and copied it
// into dist/: if it cannot find a file, Vite leaves the URL as it is and
// says nothing, and the page fails only when it runs.

const requireWasm = {
	name: 'require-wasm',
	generateBundle(options, bundle) {
		if (!Object.keys(bundle).some(name => name.endsWith('.wasm'))) {
			this.error('The build has no .wasm file: Vite did not find pseudocode-core.wasm beside pseudocode-core.mjs');
		}
	},
};

export default {
	root: 'app',
	base: './',
	logLevel: 'warn',
	build: { outDir: '../dist', emptyOutDir: true, target: 'es2022' },
	worker: { format: 'es', plugins: () => [requireWasm] },
	plugins: [requireWasm],
};
