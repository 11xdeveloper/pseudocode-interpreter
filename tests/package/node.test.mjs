// Tests the installed npm package in Node.js: the library, imported by its
// name as a user would, and the `pseudocode` command it installs.
//   node --test node.test.mjs      (in the folder install.mjs set up)

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { runPseudocode } from 'pseudocode-interpreter';

const require = createRequire(import.meta.url);
const packageJson = require.resolve('pseudocode-interpreter/package.json');
const metadata = JSON.parse(readFileSync(packageJson, 'utf8'));
const here = dirname(fileURLToPath(import.meta.url));

const sumTo = n => `OUTPUT SumTo(${n})
FUNCTION SumTo(N : INTEGER) RETURNS INTEGER
   IF N = 0 THEN
      RETURN 0
   ENDIF
   RETURN N + SumTo(N - 1)
ENDFUNCTION
`;
const runaway = `OUTPUT F(1)
FUNCTION F(N : INTEGER) RETURNS INTEGER
   RETURN F(N + 1)
ENDFUNCTION
`;

test('package.json names the package and its entry points', () => {
	assert.equal(metadata.name, 'pseudocode-interpreter');
	assert.equal(metadata.license, 'MIT');
	assert.match(metadata.version, /^\d+\.\d+\.\d+$/);
	assert.equal(typeof metadata.bin.pseudocode, 'string');
});

test('runs a program with input', async () => {
	const result = await runPseudocode('DECLARE Name : STRING\nINPUT Name\nOUTPUT "Hello, ", Name\n', { input: 'Ada\n' });
	assert.deepEqual(result, { exitCode: 0, stdout: 'Hello, Ada\n', stderr: '', files: {} });
});

test('reads the files it is given and returns the files it writes', async () => {
	const result = await runPseudocode(`DECLARE Line : STRING
OPENFILE "In.txt" FOR READ
READFILE "In.txt", Line
CLOSEFILE "In.txt"
OPENFILE "Out.txt" FOR WRITE
WRITEFILE "Out.txt", Line & "!"
CLOSEFILE "Out.txt"
`, { files: { 'In.txt': 'from Node\n' } });
	assert.equal(result.exitCode, 0, result.stderr);
	assert.deepEqual(result.files, { 'In.txt': 'from Node\n', 'Out.txt': 'from Node!\n' });
});

test('errors found before running are exit status 1', async () => {
	const result = await runPseudocode('OUTPUT 1 +\n', { fileName: 'Broken.pseudo' });
	assert.equal(result.exitCode, 1);
	assert.match(result.stderr, /^Broken\.pseudo:1:/);
	assert.equal(result.stdout, '');
});

test('runtime errors are exit status 2, after the output so far', async () => {
	const result = await runPseudocode('OUTPUT "before"\nOUTPUT 1 DIV 0\n');
	assert.equal(result.exitCode, 2);
	assert.equal(result.stdout, 'before\n');
	assert.match(result.stderr, /R004/);
});

test('INPUT past the given input is R014', async () => {
	const result = await runPseudocode('DECLARE A : STRING\nINPUT A\nINPUT A\n', { input: 'one\n' });
	assert.equal(result.exitCode, 2);
	assert.match(result.stderr, /R014/);
});

test('command line options are passed on', async () => {
	const seeded = () => runPseudocode('OUTPUT RAND(1000)\n', { args: ['--seed', '7'] });
	assert.equal((await seeded()).stdout, (await seeded()).stdout);
	const endless = await runPseudocode('WHILE TRUE DO\nENDWHILE\n', { args: ['--max-steps', '1000'] });
	assert.equal(endless.exitCode, 2);
	assert.match(endless.stderr, /R012/);
});

test('runaway recursion is R011, not an exception', async () => {
	const result = await runPseudocode(runaway);
	assert.equal(result.exitCode, 2);
	assert.match(result.stderr, /R011/);
	assert.equal((await runPseudocode(sumTo(100))).stdout, '5050\n');
});

test('runs are independent, even side by side', async () => {
	await runPseudocode('OPENFILE "Keep.txt" FOR WRITE\nCLOSEFILE "Keep.txt"\n');
	const fresh = await runPseudocode('OPENFILE "Keep.txt" FOR READ\n');
	assert.equal(fresh.exitCode, 2);
	assert.match(fresh.stderr, /R010/);

	const results = await Promise.all([1, 2, 3, 4].map(n => runPseudocode(sumTo(n * 10))));
	assert.deepEqual(results.map(r => r.stdout), ['55\n', '210\n', '465\n', '820\n']);
});

test('UTF-8 text in and out', async () => {
	const result = await runPseudocode('DECLARE S : STRING\nINPUT S\nOUTPUT "→ ", S\n', { input: 'café ✓\n' });
	assert.equal(result.stdout, '→ café ✓\n');
});

// ---------- the pseudocode command ----------

// The command npm linked into node_modules/.bin, as `npx pseudocode` runs it.
function pseudocode(args, { input = '', cwd } = {}) {
	const bin = join(here, 'node_modules', '.bin', process.platform === 'win32' ? 'pseudocode.cmd' : 'pseudocode');
	return spawnSync(bin, args, { input, cwd, encoding: 'utf8', shell: process.platform === 'win32' });
}

function programFile(name, source) {
	const folder = mkdtempSync(join(tmpdir(), 'pseudocode-package-'));
	writeFileSync(join(folder, name), source);
	return { folder, path: join(folder, name) };
}

test('the command runs a program with standard input', () => {
	const { path } = programFile('Hello.pseudo', 'DECLARE Name : STRING\nINPUT Name\nOUTPUT "Hello, ", Name\n');
	const result = pseudocode([path], { input: 'Ada\n' });
	assert.equal(result.status, 0, result.stderr);
	assert.equal(result.stdout, 'Hello, Ada\n');
});

test('the command uses real files beside the program', () => {
	const { folder, path } = programFile('Copy.pseudo', `DECLARE Line : STRING
OPENFILE "In.txt" FOR READ
READFILE "In.txt", Line
CLOSEFILE "In.txt"
OPENFILE "Out.txt" FOR WRITE
WRITEFILE "Out.txt", Line & "!"
CLOSEFILE "Out.txt"
`);
	writeFileSync(join(folder, 'In.txt'), 'on disk\n');
	const result = pseudocode([path], { cwd: tmpdir() });
	assert.equal(result.status, 0, result.stderr);
	assert.equal(readFileSync(join(folder, 'Out.txt'), 'utf8'), 'on disk!\n');
});

test('the command recurses as deeply as the native interpreter', () => {
	const deep = programFile('Deep.pseudo', sumTo(5000));
	const result = pseudocode([deep.path]);
	assert.equal(result.status, 0, result.stderr);
	assert.equal(result.stdout, '12502500\n');

	const endless = programFile('Endless.pseudo', runaway);
	const stopped = pseudocode([endless.path]);
	assert.equal(stopped.status, 2);
	assert.match(stopped.stderr, /R011.*\n.*\n.*\n\s+in function F \(line 3\)/);
});

test('the command reports its exit status', () => {
	const broken = programFile('Broken.pseudo', 'OUTPUT 1 +\n');
	assert.equal(pseudocode([broken.path]).status, 1);
	const failing = programFile('Failing.pseudo', 'OUTPUT 1 DIV 0\n');
	assert.equal(pseudocode([failing.path]).status, 2);
	const usage = pseudocode(['--no-such-option']);
	assert.equal(usage.status, 1);
	assert.match(usage.stderr + usage.stdout, /Usage/);
});
