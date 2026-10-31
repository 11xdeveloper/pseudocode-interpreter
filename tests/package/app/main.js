// A page built by Vite that uses the package the way an app would: imported
// by its name, on the page and from a module worker. browser.mjs checks the
// results it writes into #results, or the error that stopped it.
import { runPseudocode } from 'pseudocode-interpreter';

const program = 'DECLARE Name : STRING\nINPUT Name\nOUTPUT "Hello, ", Name\n';

function inWorker(source, options) {
	const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
	return new Promise((resolve, reject) => {
		worker.onmessage = event => resolve(event.data);
		worker.onerror = event => reject(new Error(event.message));
		worker.postMessage({ source, options });
	}).finally(() => worker.terminate());
}

let results;
try {
	results = {
		page: await runPseudocode(program, { input: 'page\n' }),
		worker: await inWorker(program, { input: 'worker\n' }),
	};
}
catch (error) {
	results = { page: { error: String(error) }, worker: {} };
}
document.getElementById('results').textContent = JSON.stringify(results);
