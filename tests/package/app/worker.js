import { runPseudocode } from 'pseudocode-interpreter';

// Errors are sent back too: an exception in onmessage does not reach the page.
onmessage = async ({ data }) => {
	try {
		postMessage(await runPseudocode(data.source, data.options));
	}
	catch (error) {
		postMessage({ error: String(error) });
	}
};
