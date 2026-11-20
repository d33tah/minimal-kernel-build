#!/usr/bin/env node
// Tests of hpelf.js (plan E7, hot patching) in Node with clang.wasm and
// the build kits.
//
//   node site/test_hotpatch.mjs CLANG.wasm KITS KERNELS NAME...
//       per kernel NAME (hello, minified-linux): its kit's unit compiled
//       twice as the page does (hpArgv), with the shipped text and with
//       an edit; hpPlan over KERNELS/NAME/symbols.json must patch the
//       edited function with a jump to a copy whose relocations point at
//       the kernel's symbols, and refuse the edits a patch cannot apply
//       (data added, a signature changed, nothing changed, no room).
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

import { MemFS, wasiRun } from './wasi.js';
import { kitCompile, kitHash } from './kit.js';
import { hpPlan, hpLookup, hpArgv, hpProto, hpElf, hpIndex, hpDesc, hpLines } from './hpelf.js';

const enc = new TextEncoder(), dec = new TextDecoder();
let failures = 0;
const fail = (msg) => { failures++; console.log('FAIL ' + msg); };
const BASE = 0x60000;

class NodeTools {
  constructor() { this.fs = new MemFS(); this.modules = new Map(); }
  async load(file, name = 'clang') { this.modules.set(name, new WebAssembly.Module(fs.readFileSync(file))); }
  async write(files) { for (const [p, d] of files) this.fs.writeFile(p, d); return { written: files.length }; }
  async read(paths) { return { files: paths.map((p) => [p, this.fs.readFile(p)]) }; }
  async remove(paths) { return { removed: paths.filter((p) => this.fs.remove(p)).length }; }
  async mkdir(paths) { for (const p of paths) this.fs.mkdirs(MemFS.parts(p)); return { made: paths.length }; }
  async run(args, { cwd = '/', env = {}, stdin = '', outputs = [], module = 'clang' } = {}) {
    const t = performance.now();
    const r = wasiRun(this.modules.get(module), this.fs, { args, cwd, env, stdin: enc.encode(stdin) });
    return { code: r.code, stdout: dec.decode(r.stdout), stderr: dec.decode(r.stderr), ms: performance.now() - t,
             files: outputs.map((p) => [p, this.fs.readFile(p)]) };
  }
}

function dirSource(kits) {
  return {
    async get(sha) {
      const b = new Uint8Array(zlib.gunzipSync(fs.readFileSync(path.join(kits, 'blobs', sha + '.gz'))));
      if (await kitHash(b) !== sha) throw new Error('blob ' + sha + ': other bytes');
      return b;
    },
  };
}

// The edits per kernel: [what, file, (text) => edited text, expect]
// expect: { changed: [names] } or { why: RegExp }.
const CASES = {
  hello: [
    ['put_line swaps the case', 'boot/hello.c', (t) => t.replace('(unsigned char)*s++;', '(unsigned char)(*s++ ^ 0x20);'),
      { changed: ['put_line'], line: ['boot/hello.c', 17] }],
    ['hello_main prints another string', 'boot/hello.c', (t) => t.replace('"Hello, World!"', '"Hello, Patch!"'),
      { changed: ['hello_main'], string: 'Hello, Patch!', calls: ['put_line'] }],
    ['a variable added', 'boot/hello.c', (t) => t.replace('static void put_line', 'static int hp_count;\n\nstatic void put_line')
      .replace('while (*s)', 'hp_count++;\n\twhile (*s)'), { why: /changes the data in \.bss: it adds hp_count/ }],
    ['a signature changed', 'boot/hello.c', (t) => t.replace('static void put_line(int row, const char *s)', 'static void put_line(int row, const char *s, int attr)')
      .replace('ATTR << 8', 'attr << 8').replace(/put_line\(row( \+ 1)?, ("[^"]*")\)/g, 'put_line(row$1, $2, ATTR)'),
      { why: /signature of put_line/ }],
    ['only a comment', 'boot/hello.c', (t) => t.replace('/* The whole', '/* Hot patch test. The whole'), { why: /changes no function's code/ }],
    ['no room', 'boot/hello.c', (t) => t.replace('(unsigned char)*s++;', '(unsigned char)(*s++ ^ 0x20);'), { why: /reserved memory has 8 left/, room: 8 }],
  ],
  'minified-linux': [
    ['console_on_rootfs writes a line', 'init/main.c', (t) => t.replace(/(void __init console_on_rootfs\(void\)\n\{[\s\S]*?init_dup\(file\);\n\tinit_dup\(file\);\n\tinit_dup\(file\);\n)/,
      '$1\tkernel_write(file, "Hot patched!\\n", 13, NULL);\n'), { changed: ['console_on_rootfs'], shared: true, string: 'Hot patched!\n', calls: ['kernel_write'] }],
    // The kernel is linked with LTO (the page passes lto): a new call is refused.
    ['the same with LTO', 'init/main.c', (t) => t.replace(/(void __init console_on_rootfs\(void\)\n\{[\s\S]*?init_dup\(file\);\n\tinit_dup\(file\);\n\tinit_dup\(file\);\n)/,
      '$1\tkernel_write(file, "Hot patched!\\n", 13, NULL);\n'), { why: /calls \S+, which the replaced code did not: the kernel was linked with LTO/, lto: true }],
  ],
  'debian-6.1-686-clang': [
    ['/proc/version says it was patched', 'fs/proc/version.c', (t) => t.replace('\tseq_printf(m, linux_proc_banner,', '\tseq_puts(m, "Hot patched /proc/version\\n");\n\tseq_printf(m, linux_proc_banner,'),
      { changed: ['version_proc_show'], string: 'Hot patched /proc/version\n', calls: ['seq_puts', 'seq_printf'] }],
    ['run_init_process (pr_debug: static keys)', 'init/main.c', (t) => t.replace('"Run %s as init process\\n"', '"Run %s as init (hot patched)\\n"'),
      { why: /run_init_process uses static keys/ }],
  ],
};

async function kernel(cc, kits, kernels, name) {
  const kit = JSON.parse(fs.readFileSync(path.join(kits, name + '.json'), 'utf8'));
  const src = dirSource(kits);
  const S = JSON.parse(fs.readFileSync(path.join(kernels, name, 'symbols.json'), 'utf8'));
  // The reserved memory's virtual address: the kernel maps physical a at a + koff.
  const base = BASE + S.symbols[0].vaddr - S.symbols[0].paddr;
  for (const [what, file, edit, want] of CASES[name]) {
    const t0 = Date.now();
    const o = kit.objects.find((x) => kit.files[x.deps[0]][0] === file);
    const shipped = dec.decode(await src.get(kit.files[o.deps[0]][1]));
    const edited = edit(shipped);
    if (edited === shipped) { fail(`${name} ${what}: the edit did not apply`); continue; }
    const objs = [];
    for (const [k, text] of [['before', shipped], ['after', edited]]) {
      const r = await kitCompile(cc, kit, src, o, hpArgv(o.argv, o.out + '.hp-' + k + '.o'), { [file]: text });
      if (r.code !== 0) { fail(`${name} ${what}: ${k} compile: ${r.stderr.slice(0, 400)}`); break; }
      objs.push(r.bytes);
    }
    if (objs.length < 2) continue;
    const plan = hpPlan(objs[0], objs[1], { base, room: want.room || 0x20000, lookup: hpLookup(S.symbols, null, file), src: [shipped, edited], lto: !!want.lto });
    const ms = Date.now() - t0;
    if (want.why) {
      if (plan.ok || !want.why.test(plan.why)) fail(`${name} ${what}: want a refusal ${want.why}, got ${plan.ok ? 'a plan of ' + plan.changed : plan.why}`);
      else console.log(`ok ${name} ${what}: refused: ${plan.why} (${ms} ms)`);
      continue;
    }
    if (!plan.ok) { fail(`${name} ${what}: ${plan.why}`); continue; }
    // Functions placed with it in a section of several (.init.text) may
    // count as changed too: the assembler resolved their calls inside it.
    const extra = plan.changed.filter((n) => !want.changed.includes(n));
    if (want.changed.some((n) => !plan.changed.includes(n)) || extra.some((n) => !want.shared)) {
      fail(`${name} ${what}: changed ${plan.changed}, want ${want.changed}`);
      if (process.env.HP_DEBUG) debugDiff(objs, plan.changed);
    }
    const sym = (n) => S.symbols.find((s) => s.name === n);
    // Every jump: at the kernel's function, e9 to the copy.
    for (const j of plan.jumps) {
      const f = plan.funcs.find((x) => x.name === j.name);
      const rel = new DataView(j.bytes.buffer).getInt32(1, true);
      if (j.bytes[0] !== 0xe9 || j.va !== sym(j.name).vaddr || ((j.va + 5 + rel) >>> 0) !== f.va)
        fail(`${name} ${what}: jump of ${j.name} at ${j.va.toString(16)} to ${((j.va + 5 + rel) >>> 0).toString(16)}, copy at ${f.va.toString(16)}`);
    }
    // The copy's calls (e8 rel32) reach the kernel's functions named.
    const code = plan.code, dv = new DataView(code.buffer);
    const targets = new Set();
    for (let i = 0; i + 5 <= code.length; i++) if (code[i] === 0xe8) targets.add((base + i + 5 + dv.getInt32(i + 1, true)) >>> 0);
    for (const c of want.calls || [])
      if (!targets.has(sym(c).vaddr)) fail(`${name} ${what}: the copy does not call ${c} at ${sym(c).vaddr.toString(16)}`);
    if (want.string && !dec.decode(code).includes(want.string)) fail(`${name} ${what}: the copy has no string ${JSON.stringify(want.string)}`);
    // The copy's line rows: inside the copied functions, the edited file's lines.
    const L = hpLines(objs[1], plan.at, kit.dir + '/');
    const inCopy = (va) => plan.funcs.some((f) => va >= f.va && va < f.va + f.size);
    if (!L || !L.rows.length || L.rows.some(([va, f]) => f >= 0 && !inCopy(va) && !plan.sections.some((s) => va >= s.va && va < s.va + s.size)))
      fail(`${name} ${what}: line rows ${L && JSON.stringify(L.rows.slice(0, 6))} not in the copy`);
    else if (want.line && !L.rows.some(([va, f, l]) => inCopy(va) && L.files[f] === want.line[0] && l === want.line[1]))
      fail(`${name} ${what}: no row ${want.line} in ${JSON.stringify(L.rows.map(([va, f, l]) => [va.toString(16), L.files[f], l]).slice(0, 12))}`);
    console.log(`ok ${name} ${what}: ${plan.changed} -> ${plan.funcs.map((f) => f.name + '@' + f.va.toString(16)).join(' ')}, ${code.length} bytes, ` +
      `${plan.sections.length} sections${plan.notes.length ? '; ' + plan.notes.join('; ') : ''} (${ms} ms)`);
  }
}

// HP_DEBUG=1: what differs in each changed function's two compiles.
function debugDiff(objs, names) {
  const [A, B] = objs.map((b) => hpIndex(hpElf(b)));
  for (const n of names) {
    const fa = A.funcs.get(n), fb = B.funcs.get(n);
    if (!fa || !fb) continue;
    const rels = (o, f) => f.sec.rels.filter((r) => r.off >= f.value && r.off < f.value + f.size).map((r) => (r.off - f.value) + '=' + hpDesc(o, f.sec, r));
    const ra = rels(A, fa), rb = rels(B, fb);
    const ba = fa.sec.data.subarray(fa.value, fa.value + fa.size), bb = fb.sec.data.subarray(fb.value, fb.value + fb.size);
    console.log('  ' + n + ' bytes: ' + Buffer.from(ba).toString('hex') + '\n  ' + ' '.repeat(n.length) + '        ' + Buffer.from(bb).toString('hex') +
      ' (section ' + fa.sec.index + ' of ' + A.bySec.get(fa.sec.index).length + ' symbols)');
    console.log('  ' + n + ': sizes ' + fa.size + ' ' + fb.size + ', sections ' + fa.sec.name + ' ' + fb.sec.name +
      ', relocations only before: ' + ra.filter((x) => !rb.includes(x)).slice(0, 5) + ' | only after: ' + rb.filter((x) => !ra.includes(x)).slice(0, 5));
  }
}

function unit() {
  const c = 'static int x;\nint f(int a)\n{\n\treturn a;\n}\n/* g(int b) { */\nstatic inline long g(int b, char *p) { return b; }\nstruct s { int (*h)(void); };\n';
  const want = [['f', 'int f(int a)'], ['g', 'static inline long g(int b, char *p)'], ['h', null], ['nope', null]];
  for (const [n, w] of want)
    if (hpProto(c, n) !== w) fail(`hpProto ${n}: ${JSON.stringify(hpProto(c, n))}, want ${JSON.stringify(w)}`);
  console.log('hpProto: ' + want.length + ' cases');
}

async function main() {
  const [clang, kits, kernels, ...names] = process.argv.slice(2);
  unit();
  const cc = new NodeTools();
  await cc.load(clang);
  for (const n of names) await kernel(cc, kits, kernels, n);
  if (failures) { console.log(failures + ' failures'); process.exit(1); }
  console.log('test_hotpatch: OK');
}

main().catch((e) => { console.log('FAIL ' + (e.stack || e)); process.exit(1); });
