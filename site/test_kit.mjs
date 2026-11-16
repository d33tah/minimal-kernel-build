#!/usr/bin/env node
// Tests of kit.js (plan E3) in Node.
//
//   node site/test_kit.mjs --unit
//       kitSed against GNU sed (sed in PATH) on the scripts the kits run
//       and on edge cases; kitUsers; kitBuild's errors (a fake toolchain).
//   node site/test_kit.mjs --build CLANG.wasm KITS NAME... [--compile all]
//       the kit KITS/NAME.json built here with clang.wasm and the kit's
//       modules under wasi.js (what the page's Worker runs): every step's
//       output, every compiled object and the products must be CI's
//       bytes. Then one edit (the first object's source with a function
//       added) compiled with compile "changed" must give another product.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';

import { MemFS, wasiRun } from './wasi.js';
import * as kit from './kit.js';

const enc = new TextEncoder(), dec = new TextDecoder();
let failures = 0;
const fail = (msg) => { failures++; console.log('FAIL ' + msg); };

// The page's ClangWorker (clang.js) in this process: the same requests
// on one MemFS, modules from files.
class NodeTools {
  constructor() { this.fs = new MemFS(); this.modules = new Map(); this.runs = []; }
  async load(file, name = 'clang') { this.modules.set(name, new WebAssembly.Module(fs.readFileSync(file))); }
  async write(files) { for (const [p, d] of files) this.fs.writeFile(p, d); return { written: files.length }; }
  async read(paths) { return { files: paths.map((p) => [p, this.fs.readFile(p)]) }; }
  async remove(paths) { return { removed: paths.filter((p) => this.fs.remove(p)).length }; }
  async mkdir(paths) { for (const p of paths) this.fs.mkdirs(MemFS.parts(p)); return { made: paths.length }; }
  async run(args, { cwd = '/', env = {}, stdin = '', outputs = [], module = 'clang' } = {}) {
    const mod = this.modules.get(module);
    if (!mod) throw new Error('no module ' + module + ' is loaded');
    const t = performance.now();
    const r = wasiRun(mod, this.fs, { args, cwd, env, stdin: typeof stdin === 'string' ? enc.encode(stdin) : stdin });
    this.runs.push(args[0]);
    return { code: r.code, stdout: dec.decode(r.stdout), stderr: dec.decode(r.stderr), stdoutBytes: r.stdout.slice(),
             mem: r.mem, ms: performance.now() - t, files: outputs.map((p) => [p, this.fs.readFile(p)]) };
  }
}

function dirSource(kits) {
  return {
    async kit(name) { return JSON.parse(fs.readFileSync(path.join(kits, name + '.json'), 'utf8')); },
    async get(sha) {
      const b = new Uint8Array(zlib.gunzipSync(fs.readFileSync(path.join(kits, 'blobs', sha + '.gz'))));
      if (await kit.kitHash(b) !== sha) throw new Error('blob ' + sha + ': other bytes');
      return b;
    },
    moduleUrl(sha) { return path.join(kits, 'blobs', sha + '.wasm'); },
  };
}

// --- kitSed against GNU sed ---

// cases [what, args, input]: the scripts the kits run (hello's Makefile,
// kbuild) and edge cases; errors [args, message]: scripts kitSed refuses.
// test_kit.py runs both in the browsers too.
const { cases: SED_CASES, errors: SED_ERRORS } = JSON.parse(fs.readFileSync(new URL('testdata/sed-cases.json', import.meta.url), 'utf8'));

function unit() {
  const gnu = spawnSync('sed', ['--version'], { encoding: 'utf8' });
  if (!/GNU sed/.test(gnu.stdout || '')) throw new Error('GNU sed is not in PATH');
  for (const [, args, input] of SED_CASES) {
    const want = spawnSync('sed', args, { input, encoding: 'latin1' });
    const got = kit.kitLatin1(kit.kitSed(args, kit.kitBytes(input)));
    if (want.status !== 0) fail(`GNU sed ${JSON.stringify(args)} failed: ${want.stderr}`);
    else if (got !== want.stdout) fail(`sed ${JSON.stringify(args)} on ${JSON.stringify(input)}: ${JSON.stringify(got)}, GNU ${JSON.stringify(want.stdout)}`);
  }
  // Not both empty: the voffset.h script's output as written by hand.
  const vo = kit.kitLatin1(kit.kitSed(SED_CASES[1][1], kit.kitBytes(SED_CASES[1][2])));
  if (vo !== '#define VO__text _AC(0xc1000000,UL)\n#define VO___bss_start _AC(0xc1200000,UL)\n#define VO__end _AC(0xc1300000,UL)\n')
    fail('voffset.h script: ' + JSON.stringify(vo));
  console.log(`kitSed: ${SED_CASES.length} scripts against GNU sed`);
  // Bytes stay bytes.
  const bin = new Uint8Array(256).map((_, i) => i);
  const round = kit.kitSed(['s/x/x/'], bin);
  if (round.length !== 256 || round.some((b, i) => b !== i)) fail('kitSed changed bytes it did not match');
  for (const [args, msg] of SED_ERRORS) {
    let e = null;
    try { kit.kitSed(args, kit.kitBytes('a\n')); } catch (x) { e = x.message; }
    if (!e || !e.includes(msg)) fail(`sed ${JSON.stringify(args)} must fail with "${msg}": ${e}`);
  }
  const k = { objects: [{ out: 'a.o', deps: [0, 2] }, { out: 'b.o', deps: [1, 2] }], files: [['a.c'], ['b.c'], ['x.h']] };
  const u = kit.kitUsers(k);
  if (JSON.stringify([...u]) !== JSON.stringify([['a.c', ['a.o']], ['x.h', ['a.o', 'b.o']], ['b.c', ['b.o']]]))
    fail('kitUsers: ' + JSON.stringify([...u]));
  console.log('kitUsers, kitSed errors checked');
}

async function errors() {
  // kitBuild's own refusals and a failing step, with a fake toolchain.
  const tk = { format: 1, name: 't', dir: '/b', files: [['a.c', 'x', 1, 0]], objects: [], products: [],
               modules: {}, steps: [{ tool: 'clang', argv: ['ld.lld'], inputs: [], outputs: [], fresh: [], mkdir: [] }] };
  const fake = new NodeTools();
  fake.run = async () => ({ code: 1, stderr: 'ld.lld: error: no input files', stdoutBytes: new Uint8Array(0), files: [] });
  const src = { get: async () => new Uint8Array(1), moduleUrl: () => 'x' };
  for (const [opts, msg] of [[{ edits: { 'b.c': 'x' } }, 'not a file of the kit'], [{ compile: 'some' }, 'compile is false'],
                             [{}, 'exited with 1: ld.lld: error: no input files']]) {
    let e = null;
    try { await kit.kitBuild(fake, tk, src, opts); } catch (x) { e = x.message; }
    if (!e || !e.includes(msg)) fail(`kitBuild ${JSON.stringify(opts)} must fail with "${msg}": ${e}`);
  }
  tk.steps[0].tool = 'mkpiggy';
  let e = null;
  try { await kit.kitBuild(fake, tk, src, {}); } catch (x) { e = x.message; }
  if (!e || !e.includes('no module mkpiggy')) fail('a step of a module the kit lacks: ' + e);
  console.log('kitBuild errors checked');
}

async function build(clang, kits, names, compile) {
  const src = dirSource(kits);
  for (const name of names) {
    const k = await src.kit(name);
    const cc = new NodeTools();
    await cc.load(clang);
    const r = await kit.kitBuild(cc, k, src, { compile });
    const badObj = r.objects.filter((o) => o.sha256 !== o.want);
    const badStep = r.steps.flatMap((s, i) => (s.outputs || []).filter(([, h, w]) => h !== w).map(([p]) => `step ${i + 1} ${s.tool} ${p}`));
    const badProd = r.products.filter(([, h, , w]) => h !== w);
    console.log(`${name}: ${r.fetched} blobs (${r.fetchedBytes} B), ${r.objects.length} objects compiled ` +
                `(${badObj.length} different) in ${Math.round(r.compileMs)} ms, ${r.steps.length} steps in ${Math.round(r.linkMs)} ms; ` +
                r.products.map(([p, h, n, w]) => `${p} ${n} B ${h === w ? 'SAME' : 'DIFFERENT'}`).join(', '));
    for (const o of badObj) fail(`${name}: object ${o.out} ${o.sha256}, CI's ${o.want}`);
    for (const s of badStep) fail(`${name}: ${s} differs from CI's`);
    for (const [p, h, , w] of badProd) fail(`${name}: ${p} ${h}, CI's ${w}`);
    // An edit: the kernel's main C file with a function added (kept by
    // "used" through the minified Linux's LTO).
    const first = k.objects.find((o) => ['init/main.c', 'boot/hello.c'].includes(k.files[o.deps[0]][0]));
    const cpath = k.files[first.deps[0]][0];
    const text = dec.decode(await src.get(k.files[first.deps[0]][1]));
    const cc2 = new NodeTools();
    await cc2.load(clang);
    const add = '\n__attribute__((used)) int mkb_e3_edit(void);\n__attribute__((used)) int mkb_e3_edit(void) { return 3; }\n';
    const e = await kit.kitBuild(cc2, k, src, { compile: 'changed', edits: { [cpath]: text + add } });
    const users = kit.kitUsers(k).get(cpath);
    if (e.objects.length !== users.length || e.objects[0].sha256 === e.objects[0].want)
      fail(`${name}: the edit of ${cpath} compiled ${e.objects.length} objects (${users.length} include it), first ${e.objects[0]?.sha256}`);
    const same = e.products.filter(([, h, , w]) => h === w).map((p) => p[0]);
    if (same.length) fail(`${name}: ${cpath} edited, still CI's ${same}`);
    console.log(`${name}: ${cpath} edited: ${e.objects.length} object compiled, products ${e.products.map(([p, , n]) => p + ' ' + n + ' B').join(', ')}, all different`);
  }
}

const argv = process.argv.slice(2);
if (argv[0] === '--unit') {
  unit();
  await errors();
} else if (argv[0] === '--build') {
  const compile = argv.includes('--compile') ? argv[argv.indexOf('--compile') + 1] : false;
  const rest = argv.slice(1).filter((a, i, all) => a !== '--compile' && all[i - 1] !== '--compile');
  await build(rest[0], rest[1], rest.slice(2), compile);
} else {
  console.error('usage: test_kit.mjs --unit | --build CLANG.wasm KITS NAME... [--compile all]');
  process.exit(2);
}
if (failures) {
  console.log(`${failures} failures`);
  process.exit(1);
}
console.log('PASS');
