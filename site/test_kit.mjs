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
//       A kit with generators (the minified Linux: /init by nasm, the
//       initramfs by kitcpio.js): every object compiled runs them to CI's
//       bytes; /init's string edited reaches the bzImage; the unchanged text
//       as an edit gives CI's products; an assembly error names its line.
import fs from 'node:fs';
import os from 'node:os';
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
    // The largest wasm memory of a run (bytes), and which run it was.
    if (!this.peak || r.mem > this.peak[0]) this.peak = [r.mem, args[0]];
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
  // kitGrep against GNU grep: scripts/mksysmap's filter, and the other options.
  const syms = 'c1000000 T _text\nc1000010 t .Ltmp1\n00000000 a foo.c\nc1000020 T __crc_bar\nc1000030 W weak\n' +
    'c1000040 T $x\nc1000050 r __kstrtab_baz\nc1000060 r __kstrtabns_baz\nc1000070 t L0\nc1000080 T start_kernel\nno newline at the end';
  const GREP_CASES = [
    ['-v', '-e', ' [aNUw] ', '-e', ' \\$', '-e', ' \\.L', '-e', ' __crc_', '-e', ' __kstrtab_', '-e', ' __kstrtabns_', '-e', ' L0$'],
    ['-v', 'T'], ['-e', 'r __'], ['-E', 'T (_text|start)'], ['-F', '$x'], ['-vF', '.L'], ['-ve', 'kstrtab'], ['zzz'],
  ];
  for (const args of GREP_CASES) {
    const want = spawnSync('grep', args, { input: syms, encoding: 'latin1' });
    const got = kit.kitLatin1(kit.kitGrep(args, kit.kitBytes(syms)));
    if (want.status > 1) fail(`GNU grep ${JSON.stringify(args)} failed: ${want.stderr}`);
    else if (got !== want.stdout) fail(`grep ${JSON.stringify(args)}: ${JSON.stringify(got)}, GNU ${JSON.stringify(want.stdout)}`);
  }
  for (const [args, msg] of [[['-c', 'x'], 'unsupported option -c'], [['-v'], 'no pattern']]) {
    let e = null;
    try { kit.kitGrep(args, kit.kitBytes('a\n')); } catch (x) { e = x.message; }
    if (!e || !e.includes(msg)) fail(`grep ${JSON.stringify(args)} must fail with "${msg}": ${e}`);
  }
  console.log(`kitGrep: ${GREP_CASES.length} cases against GNU grep`);
  const k = { objects: [{ out: 'a.o', deps: [0, 2] }, { out: 'b.o', deps: [1, 2] }], files: [['a.c'], ['b.c'], ['x.h']] };
  const u = kit.kitUsers(k);
  if (JSON.stringify([...u]) !== JSON.stringify([['a.c', ['a.o']], ['x.h', ['a.o', 'b.o']], ['b.c', ['b.o']]]))
    fail('kitUsers: ' + JSON.stringify([...u]));
  console.log('kitUsers, kitSed errors checked');
  cpioUnit();
}

// kitCpio against usr/gen_init_cpio (the minified Linux's, compiled with cc)
// on the same input lines: the initramfs list of CI and edge cases.
function cpioUnit() {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'mkb-cpio-'));
  try {
    const tool = path.join(tmp, 'gen_init_cpio');
    const cc = spawnSync('cc', ['-O2', '-o', tool, new URL('../minified/usr/gen_init_cpio.c', import.meta.url).pathname], { encoding: 'utf8' });
    if (cc.status !== 0) throw new Error('cc gen_init_cpio.c: ' + cc.stderr);
    const files = new Map([['elo/Makefile', enc.encode('all:\n')], ['elo/init', new Uint8Array(163).map((_, i) => i * 7)],
                           ['e/odd', enc.encode('abc')], ['e/empty', new Uint8Array(0)], ['e/four', enc.encode('abcd')]]);
    for (const [p, b] of files) {
      fs.mkdirSync(path.join(tmp, path.dirname(p)), { recursive: true });
      fs.writeFileSync(path.join(tmp, p), b);
    }
    const cases = [
      ['file /Makefile elo/Makefile 644 0 0', 'file /init elo/init 755 0 0', 'dir /dev 755 0 0', 'nod /dev/console 600 0 0 c 5 1'],
      ['file /a/odd-name e/odd 4755 1000 100', 'file /empty e/empty 600 0 0', 'file /x e/four 644 65534 65534', 'dir /a 700 1 2',
       'nod /dev/tty1 620 0 5 c 4 1', 'file   /spaced\te/odd   644 0 0  '],
      [],
    ];
    for (const list of cases) {
      const want = spawnSync(tool, [], { cwd: tmp, input: list.map((l) => l + '\n').join('') });
      const got = kit.kitCpio(list, files);
      if (want.status !== 0) fail(`gen_init_cpio ${JSON.stringify(list)}: ${want.stderr}`);
      else if (Buffer.compare(Buffer.from(got), want.stdout) !== 0)
        fail(`kitCpio ${JSON.stringify(list)}: ${got.length} B, gen_init_cpio ${want.stdout.length} B, other bytes`);
    }
    // Refused by both (the C tool's sscanf takes some malformed numbers,
    // which kitCpio refuses too).
    for (const [list, msg, both] of [[['slink /a b 777 0 0'], 'unsupported line: slink', true], [['file /a e/none 644 0 0'], 'cannot read: e/none', true],
                                     [[' file /a e/odd 644 0 0'], 'unsupported line', true], [['nod /a 600 0 0 b 1 1'], 'unsupported line', true],
                                     [['file /a e/odd 698 0 0'], 'unsupported line', false]]) {
      let e = null;
      try { kit.kitCpio(list, files); } catch (x) { e = x.message; }
      if (!e || !e.includes(msg)) fail(`kitCpio ${JSON.stringify(list)} must fail with "${msg}": ${e}`);
      if (both && spawnSync(tool, [], { cwd: tmp, input: list[0] + '\n' }).status === 0) fail(`gen_init_cpio took ${JSON.stringify(list)}`);
    }
    console.log(`kitCpio: ${cases.length} lists equal to gen_init_cpio's archives, 5 refused lines`);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
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
    const badStep = r.steps.flatMap((s, i) => (s.outputs || []).filter(([, h, w]) => h !== w).map(([p]) => `step ${i + 1} ${s.tool} ${p}`))
      .concat(r.gen.filter((g) => g.sha256 !== g.want).map((g) => `generator of ${g.out}`));
    if (k.gen && (compile === 'all') !== (r.gen.length === k.gen.length))
      fail(`${name}: compile ${compile} ran ${r.gen.length} of ${k.gen.length} generators`);
    const badProd = r.products.filter(([, h, , w]) => h !== w);
    console.log(`${name}: ${r.fetched} blobs (${r.fetchedBytes} B), ${r.objects.length} objects compiled ` +
                `(${badObj.length} different) in ${Math.round(r.compileMs)} ms, ${r.steps.length} steps in ${Math.round(r.linkMs)} ms; ` +
                r.products.map(([p, h, n, w]) => `${p} ${n} B ${h === w ? 'SAME' : 'DIFFERENT'}`).join(', '));
    console.log(`${name}: largest wasm memory ${(cc.peak[0] / 2 ** 20).toFixed(0)} MiB (${cc.peak[1]}); ` +
                `Node RSS ${(process.memoryUsage().rss / 2 ** 20).toFixed(0)} MiB after the build`);
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
    if (k.gen)
      await generators(clang, k, src, name);
    // A kit of stripped objects (Debian): the same source compiled again
    // (with recordmcount after it) is CI's object before the strip.
    if (first.gsha256) {
      const cc3 = new NodeTools();
      await cc3.load(clang);
      const u = await kit.kitBuild(cc3, k, src, { compile: 'changed', edits: { [cpath]: text } });
      const bad = u.objects.filter((o) => o.sha256 !== o.wantg);
      for (const o of bad) fail(`${name}: ${o.out} compiled again ${o.sha256}, CI's before the strip ${o.wantg}`);
      console.log(`${name}: ${cpath} compiled again unchanged: ${u.objects.length - bad.length} of ${u.objects.length} objects are CI's -g bytes ` +
                  `(post steps: ${first.post ? first.post.map((p) => p.argv[0]).join(', ') : 'none'})`);
    }
  }
}

// The minified Linux's /init (plan E9): elo/init.nasm edited, unchanged,
// and with an error.
async function generators(clang, k, src, name) {
  const asm = k.gen.find((g) => g.tool === 'nasm');
  const spath = asm.inputs[0];
  const text = dec.decode(await src.get(k.files.find((f) => f[0] === spath)[1]));
  const cpio = k.gen.find((g) => g.tool === 'cpio').outputs[0][0];
  const users = kit.kitUsers(k).get(cpio);
  const run = async (edit) => {
    const cc = new NodeTools();
    await cc.load(clang);
    try {
      return await kit.kitBuild(cc, k, src, { compile: 'changed', edits: { [spath]: edit }, keep: k.products.map((p) => p[0]) });
    } catch (x) {
      return { error: x.message };
    }
  };
  const msg = 'Hello from an edited /init!';
  const e = await run(text.replace("'Hello, World!'", `'${msg}'`));
  if (e.error) {
    fail(`${name}: ${spath} edited: ${e.error}`);
  } else {
    const img = e.files.find(([p]) => p === k.shipped.path)[1];
    const has = Buffer.from(img).includes(Buffer.from(msg + '\n'));
    if (e.gen.length !== k.gen.length || e.gen.some((g) => g.sha256 === g.want) || !has ||
        JSON.stringify(e.objects.map((o) => o.out)) !== JSON.stringify(users) || e.products.some(([, h, , w]) => h === w))
      fail(`${name}: ${spath} edited: generators ${JSON.stringify(e.gen)}, objects ${e.objects.map((o) => o.out)} (users of ${cpio}: ${users}), ` +
           `string in the bzImage ${has}, products ${JSON.stringify(e.products)}`);
    console.log(`${name}: ${spath} edited: ${e.gen.map((g) => g.out + ' ' + g.size + ' B').join(', ')} made again, ${e.objects.map((o) => o.out)} compiled, ` +
                `the string in the bzImage, ${Math.round(e.ms)} ms`);
  }
  const u = await run(text);
  if (u.error || u.gen.some((g) => g.sha256 !== g.want) || u.products.some(([, h, , w]) => h !== w))
    fail(`${name}: ${spath} unchanged as an edit: ${u.error || JSON.stringify([u.gen, u.products])}`);
  else
    console.log(`${name}: ${spath} unchanged as an edit: /init, the initramfs, vmlinux and the bzImage are CI's bytes`);
  const lines = text.split('\n');
  const at = lines.findIndex((l) => l.includes('int 0x80')) + 1;
  lines.splice(at, 0, '    mov eax, [nosuchlabel]');
  const bad = await run(lines.join('\n'));
  const want = `${spath}:${at + 1}: error:`;
  if (!bad.error || !bad.error.includes(want) || !bad.error.includes('exited with 1'))
    fail(`${name}: an assembly error must fail with "${want}": ${bad.error || 'it built'}`);
  else
    console.log(`${name}: assembly error: ${bad.error.slice(0, 160)}`);
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
