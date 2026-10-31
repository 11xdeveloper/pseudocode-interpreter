// Sets up a folder for the npm package tests, as a user's project would be:
//   node install.mjs <package.tgz> <folder>
// Copies these tests into <folder>, installs the test tools from
// package-lock.json, then installs the packed package from its tarball, so
// the tests use exactly the files that `npm publish` would upload.

import { cpSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const [tarball, folder] = process.argv.slice(2).map(path => resolve(path));
if (!tarball || !folder) {
	console.error('Usage: node install.mjs <package.tgz> <folder>');
	process.exit(1);
}
const here = dirname(fileURLToPath(import.meta.url));

rmSync(folder, { recursive: true, force: true });
mkdirSync(folder, { recursive: true });
cpSync(here, folder, {
	recursive: true,
	filter: source => !source.startsWith(join(here, 'node_modules')),
});

// npm is beside the Node.js running this script.
const npm = join(dirname(process.execPath), process.platform === 'win32' ? 'npm.cmd' : 'npm');
const run = (...args) => execFileSync(npm, args, {
	cwd: folder,
	stdio: 'inherit',
	shell: process.platform === 'win32',
	env: { ...process.env, PATH: dirname(process.execPath) + (process.platform === 'win32' ? ';' : ':') + process.env.PATH },
});
run('ci', '--no-audit', '--no-fund');
run('install', '--no-save', '--no-audit', '--no-fund', tarball);
console.log(`Installed ${tarball} in ${folder}`);
