// Tests the installed npm package in headless Chromium:
//   node browser.mjs <tests/programs> <build-wasm>
// (in the folder install.mjs set up, after `vite build` has made dist/)
//
// 1. Every program test, run through runPseudocode from the package and
//    compared exactly with its .expected file.
// 2. The package from a module Web Worker.
// 3. The behaviour only a page has (wasm/README.md): shallower recursion,
//    input given up front, files in memory.
// 4. The Vite-built app in dist/, which imports the package by name.
// 5. build-wasm/example.html.
// Needs Playwright's Chromium: npx playwright install chromium

import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const [programs, build] = process.argv.slice(2).map(path => path && resolve(path));
if (!programs || !build) {
	console.error('Usage: node browser.mjs <tests/programs> <build-wasm>');
	process.exit(1);
}
const here = dirname(fileURLToPath(import.meta.url));
const packageDir = dirname(createRequire(import.meta.url).resolve('pseudocode-interpreter/package.json'));

// Programs that are meant to behave differently in a page, and how.
const PageDifferences = {
	// SumTo(5000) nests deeper than a page's stack allows (wasm/README.md).
	'language/recursion': result => result.exitCode === 2 && /R011/.test(result.stderr),
};

let failures = 0;
function check(name, ok, detail = '') {
	console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${ok || !detail ? '' : '\n     ' + detail}`);
	if (!ok) failures++;
}

// ---------- a static file server ----------

const Types = {
	'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
	'.wasm': 'application/wasm', '.json': 'application/json', '.css': 'text/css',
};
const roots = { pkg: packageDir, dist: join(here, 'dist'), build };
const server = createServer((request, response) => {
	const url = new URL(request.url, 'http://localhost');
	if (url.pathname === '/') {
		response.writeHead(200, { 'Content-Type': 'text/html' });
		return response.end('<!doctype html><title>blank</title>');
	}
	const [, root, ...rest] = url.pathname.split('/');
	const base = roots[root];
	let file = base && normalize(join(base, ...rest.map(decodeURIComponent)));
	if (file && existsSync(file) && statSync(file).isDirectory()) file = join(file, 'index.html');
	if (!file || !file.startsWith(base) || !existsSync(file)) {
		response.writeHead(404);
		return response.end('not found');
	}
	response.writeHead(200, { 'Content-Type': Types[extname(file)] ?? 'application/octet-stream' });
	response.end(readFileSync(file));
});
await new Promise(ready => server.listen(0, '127.0.0.1', ready));
const origin = `http://127.0.0.1:${server.address().port}`;

// ---------- the program tests, read as tests/runner/ProgramTests.cpp does ----------

function directives(source) {
	const result = { args: [], input: '' };
	for (let line of source.split('\n')) {
		if (line.endsWith('\r')) line = line.slice(0, -1);
		if (!line.startsWith('//')) break;
		const text = line.slice(2).trimStart();
		const value = name => (text.startsWith(name) ? text.slice(name.length).replace(/^ /, '') : null);
		let v;
		if ((v = value('ARGS:')) !== null) result.args.push(...v.split(/\s+/).filter(Boolean));
		else if ((v = value('STDIN-CRLF:')) !== null) result.input += v + '\r\n';
		else if ((v = value('STDIN:')) !== null) result.input += v + '\n';
	}
	return result;
}

function format(result) {
	const section = text => {
		text = text.replace(/\r\n/g, '\n');
		return text && !text.endsWith('\n') ? text + '\n[no newline at end]\n' : text;
	};
	return `exit: ${result.exitCode}\n--- stdout\n${section(result.stdout)}--- stderr\n${section(result.stderr)}`;
}

function programTests() {
	const tests = [];
	const walk = folder => {
		for (const entry of readdirSync(folder, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
			const path = join(folder, entry.name);
			if (entry.isDirectory() && !entry.name.endsWith('.files')) walk(path);
			if (!entry.isFile() || !entry.name.endsWith('.pseudo')) continue;
			const stem = path.slice(0, -'.pseudo'.length);
			const source = readFileSync(path, 'utf8');
			const files = {};
			if (existsSync(stem + '.files')) {
				for (const name of readdirSync(stem + '.files')) {
					files[name] = readFileSync(join(stem + '.files', name), 'utf8');
				}
			}
			tests.push({
				name: stem.slice(programs.length + 1).replaceAll('\\', '/'),
				fileName: entry.name, source, files, ...directives(source),
				expected: readFileSync(stem + '.expected', 'utf8').replace(/\r\n/g, '\n'),
			});
		}
	};
	walk(programs);
	return tests;
}

// ---------- the tests ----------

const browser = await chromium.launch();
try {
	const page = await browser.newPage();
	const errors = [];
	page.on('pageerror', error => errors.push(String(error)));
	page.on('console', message => message.type() === 'error' && errors.push(message.text()));
	console.log(`Chromium ${browser.version()}, package from ${packageDir}`);

	// 1. Every program test.
	await page.goto(`${origin}/`);
	const tests = programTests();
	const results = await page.evaluate(async tests => {
		const { runPseudocode } = await import('/pkg/pseudocode.mjs');
		const results = [];
		for (const { source, input, args, files, fileName } of tests) {
			try {
				results.push(await runPseudocode(source, { input, args, files, fileName }));
			}
			catch (error) {
				results.push({ exitCode: `threw ${error}`, stdout: '', stderr: '' });
			}
		}
		return results;
	}, tests.map(({ expected, ...test }) => test));
	tests.forEach((test, i) => {
		const actual = format(results[i]);
		if (PageDifferences[test.name]) {
			check(`program ${test.name} (differs in a page, as documented)`, PageDifferences[test.name](results[i]), actual);
			return;
		}
		const a = actual.split('\n'), e = test.expected.split('\n');
		const line = a.findIndex((text, j) => text !== e[j]);
		check(`program ${test.name}`, actual === test.expected,
			`line ${line + 1}: expected ${JSON.stringify(e[line])}, got ${JSON.stringify(a[line])}`);
	});

	// 2 and 3. A worker, and what only a page does.
	const page2 = await page.evaluate(async () => {
		const { runPseudocode } = await import('/pkg/pseudocode.mjs');
		// The worker sends back errors too: an exception in onmessage does
		// not reach the page, which would wait for ever.
		const code = `import { runPseudocode } from '${location.origin}/pkg/pseudocode.mjs';
			onmessage = async ({ data }) => {
				try { postMessage(await runPseudocode(data.source, data.options)); }
				catch (error) { postMessage({ exitCode: 'threw ' + error, stdout: '', stderr: '', files: {} }); }
			};`;
		const worker = new Worker(URL.createObjectURL(new Blob([code], { type: 'text/javascript' })), { type: 'module' });
		const ask = (source, options = {}) => new Promise(resolve => {
			const timer = setTimeout(() => resolve({ exitCode: 'no answer from the worker', stdout: '', stderr: '', files: {} }), 20000);
			worker.onmessage = event => { clearTimeout(timer); resolve(event.data); };
			worker.onerror = event => { clearTimeout(timer); resolve({ exitCode: 'threw ' + event.message, stdout: '', stderr: '', files: {} }); };
			worker.postMessage({ source, options });
		});
		const copy = 'DECLARE L : STRING\nOPENFILE "In.txt" FOR READ\nREADFILE "In.txt", L\nCLOSEFILE "In.txt"\n'
			+ 'OPENFILE "Out.txt" FOR WRITE\nWRITEFILE "Out.txt", L & "!"\nCLOSEFILE "Out.txt"\n';
		const workerHello = await ask('DECLARE N : STRING\nINPUT N\nOUTPUT "Hi ", N\n', { input: 'Bo\n' });
		const workerFiles = await ask(copy, { files: { 'In.txt': 'from page\n' } });
		worker.terminate();

		const sumTo = n => `OUTPUT S(${n})\nFUNCTION S(N : INTEGER) RETURNS INTEGER\n   IF N = 0 THEN\n      RETURN 0\n   ENDIF\n   RETURN N + S(N - 1)\nENDFUNCTION\n`;
		return {
			workerHello, workerFiles,
			deep: await runPseudocode(sumTo(500)),
			runaway: await runPseudocode('OUTPUT F(1)\nFUNCTION F(N : INTEGER) RETURNS INTEGER\n   RETURN F(N + 1)\nENDFUNCTION\n'),
			after: await runPseudocode(sumTo(100)),
			noInput: await runPseudocode('DECLARE A : STRING\nINPUT A\nINPUT A\n'),
			kept: (await runPseudocode('OPENFILE "Keep.txt" FOR WRITE\nCLOSEFILE "Keep.txt"\n')).files,
			fresh: await runPseudocode('OPENFILE "Keep.txt" FOR READ\n'),
			limited: await runPseudocode('OUTPUT "Hello"\nOUTPUT "World"\n', { maxOutput: 8 }),
		};
	});
	check('worker runs a program with input', page2.workerHello.stdout === 'Hi Bo\n', JSON.stringify(page2.workerHello));
	check('worker reads given files and returns written ones',
		page2.workerFiles.files['Out.txt'] === 'from page!\n', JSON.stringify(page2.workerFiles));
	check('recursion 500 deep runs in a page', page2.deep.stdout === '125250\n', JSON.stringify(page2.deep));
	check('runaway recursion is R011, exit status 2',
		page2.runaway.exitCode === 2 && /R011/.test(page2.runaway.stderr), JSON.stringify(page2.runaway));
	check('the next run works after the stack ran out', page2.after.stdout === '5050\n', JSON.stringify(page2.after));
	check('INPUT past the given input is R014',
		page2.noInput.exitCode === 2 && /R014/.test(page2.noInput.stderr), JSON.stringify(page2.noInput));
	check('files do not carry over between runs',
		'Keep.txt' in page2.kept && page2.fresh.exitCode === 2 && /R010/.test(page2.fresh.stderr), JSON.stringify(page2.fresh));

	check('output over maxOutput stops with R015',
		page2.limited.exitCode === 2 && page2.limited.stdout === 'Hello\nWo' && /R015/.test(page2.limited.stderr),
		JSON.stringify(page2.limited));

	// 4. The app Vite built, importing the package by name.
	await page.goto(`${origin}/dist/`);
	const bundled = await page.waitForFunction(() => document.getElementById('results').textContent !== '', null, { timeout: 30000 })
		.then(async () => JSON.parse(await page.textContent('#results')))
		.catch(error => ({ page: { error: String(error) }, worker: {} }));
	check('bundled app runs a program on the page', bundled.page.stdout === 'Hello, page\n', JSON.stringify(bundled.page));
	check('bundled app runs a program in its worker', bundled.worker.stdout === 'Hello, worker\n', JSON.stringify(bundled.worker));

	// 5. The example page.
	await page.goto(`${origin}/build/example.html`);
	await page.waitForSelector('#run:not([disabled])', { timeout: 30000 });
	await page.click('#run');
	await page.waitForFunction(() => document.getElementById('stdout').textContent !== '', null, { timeout: 30000 })
		.catch(() => {});
	check('example.html runs its program',
		(await page.textContent('#stdout')) === 'What is your name?\nHello, Ada (1)\nHello, Ada (2)\nHello, Ada (3)\n'
		&& (await page.textContent('#stderr')) === '', await page.textContent('#stdout'));

	check('no errors in the browser console', errors.length === 0, errors.join('\n     '));
}
finally {
	await browser.close();
	server.close();
}
console.log(failures ? `${failures} failed` : 'All passed');
process.exit(failures ? 1 : 0);
