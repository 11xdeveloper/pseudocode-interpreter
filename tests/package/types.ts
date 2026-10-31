// Checks the package's TypeScript types: tsc -p . reports any error here.
// Each @ts-expect-error line must be an error, or tsc reports that too.

import { runPseudocode, type RunOptions, type RunResult } from 'pseudocode-interpreter';

const options: RunOptions = {
	input: 'Ada\n',
	args: ['--seed', '42'],
	files: { 'Scores.txt': '12\n15\n' },
	fileName: 'Hello.pseudo',
};
const result: RunResult = await runPseudocode('OUTPUT "Hello"', options);
const exitCode: number = result.exitCode;
const output: string = result.stdout + result.stderr;
const written: string | undefined = result.files['Out.txt'];
await runPseudocode('OUTPUT 1');

// @ts-expect-error the source is required
await runPseudocode();
// @ts-expect-error args is a list of strings
await runPseudocode('OUTPUT 1', { args: '--strict' });
// @ts-expect-error files hold text
await runPseudocode('OUTPUT 1', { files: { 'A.txt': 42 } });
// @ts-expect-error there is no such option
await runPseudocode('OUTPUT 1', { stdin: '' });
// @ts-expect-error exitCode is a number
const wrong: string = result.exitCode;

export { exitCode, output, written, wrong };
