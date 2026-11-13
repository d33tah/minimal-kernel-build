#!/usr/bin/env node
// Compile objects of a CI kernel build again with clang.wasm (the page's
// in-browser compiler) and check that they equal CI's objects byte for byte.
//
//   node site/check-wasm-objects.mjs [--self-test] [--native-rerun] CLANG.wasm DIR OBJ...
//
// DIR is the build directory (the compile commands' working directory).
// OBJ is an object path relative to DIR: its compile command comes from
// kbuild's DIR/<dir>/.<name>.cmd (the `cmd_OBJ := ...` line, up to the
// first `;`), or is given as OBJ=COMMAND (e.g. from `make -n`). clang.wasm
// runs the command's exact argv under Node's WASI with the working
// directory DIR: "/" and DIR's absolute path both map to DIR, and PWD is
// DIR, so relative paths and DW_AT_comp_dir are what the native compiler
// saw. The reference is the build's object, or with --native-rerun (or
// when the .cmd line runs more after the compile) the same argv run again
// by the native clang in PATH: kbuild's recordmcount rewrites an object
// after the command its .cmd file keeps (Debian's config). The build's
// object, its time stamps and the dependency file are restored afterwards;
// a different object from clang.wasm is left next to it as OBJ.clang-wasm.
// --self-test: the first object compiled by clang.wasm at another
// optimization level (-O0 and -Os to -O2, others to -Os; the kernel does
// not build at -O0) must be reported as different.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
// Node 20 and 22 call WASI experimental; the warning says nothing here.
process.removeAllListeners('warning');
const { WASI } = await import('node:wasi');

// POSIX shell words of `s` up to the first unquoted `;`.
export function shellWords(s) {
  const words = [];
  let w = null, i = 0;
  const add = (c) => { w = (w ?? '') + c; };
  for (; i < s.length; i++) {
    const c = s[i];
    if (c === "'") {
      const j = s.indexOf("'", i + 1);
      if (j < 0) throw new Error('unterminated quote: ' + s);
      add(s.slice(i + 1, j)); i = j;
    } else if (c === '"') {
      let j = i + 1;
      w = w ?? '';
      for (; j < s.length && s[j] !== '"'; j++) {
        if (s[j] === '\\' && '"\\$`\n'.includes(s[j + 1])) j++;
        w += s[j];
      }
      if (j >= s.length) throw new Error('unterminated quote: ' + s);
      i = j;
    } else if (c === '\\') {
      add(s[++i]);
    } else if (c === ';' || c === '&' || c === '|') {
      break;
    } else if (/\s/.test(c)) {
      if (w !== null) { words.push(w); w = null; }
    } else {
      add(c);
    }
  }
  if (w !== null) words.push(w);
  return { words, rest: s.slice(i).replace(/^[;&|\s]+/, '').trim() };
}

function kbuildCommand(dir, obj) {
  const cmdFile = path.join(dir, path.dirname(obj), '.' + path.basename(obj) + '.cmd');
  const text = fs.readFileSync(cmdFile, 'utf8');
  const m = text.match(new RegExp('^(?:saved)?cmd_' + obj.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ' := (.*)$', 'm'));
  if (!m) throw new Error(`${cmdFile}: no cmd_${obj} line`);
  return m[1].replace(/\$\(pound\)/g, '#').replace(/\$\$/g, '$');
}

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// One compile in this process (the child of runWasm): writes
// {code, ms, mem} to RESULT when clang calls WASI proc_exit (before Node
// handles the exit) or returns from main.
async function wasmChild(wasmPath, dir, argvJson, result) {
  const argv = JSON.parse(argvJson);
  const module = await WebAssembly.compile(fs.readFileSync(wasmPath));
  const abs = fs.realpathSync(dir);
  const wasi = new WASI({
    version: 'preview1', args: argv, env: { PWD: abs }, preopens: { '/': abs, [abs]: abs },
  });
  const imports = wasi.getImportObject();
  const p1 = imports.wasi_snapshot_preview1;
  let inst, t;
  const done = (code) => fs.writeFileSync(result, JSON.stringify({
    code, ms: Math.round(performance.now() - t), mem: inst.exports.memory.buffer.byteLength }));
  const procExit = p1.proc_exit;
  p1.proc_exit = (code) => { done(code); return procExit(code); };
  inst = await WebAssembly.instantiate(module, imports);
  t = performance.now();
  wasi.start(inst);
  done(0);
}

// Each compile in its own Node process, which ends with clang's exit, run
// with V8's fast API calls off: with them, Node's WASI (Node 20.19 and
// 22.23) crashed the process (SIGSEGV, or a V8 fatal error) in every
// compile of Debian's kernel/sched/core.c (wasm memory 105 MiB), before or
// after clang's exit, and on later instances in one process; without them
// 5 of 5 runs were clean. clang's exit code is what counts; how Node
// exits is reported.
function runWasm(wasmPath, dir, argv) {
  const result = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'cwo-')), 'r.json');
  const r = spawnSync(process.execPath, [...process.execArgv, '--no-turbo-fast-api-calls', fileURLToPath(import.meta.url), '--wasm-child', wasmPath, dir, JSON.stringify(argv), result],
    { stdio: ['ignore', 'inherit', 'inherit'] });
  const out = fs.existsSync(result) ? JSON.parse(fs.readFileSync(result, 'utf8'))
    : { code: `none: node exited with ${r.signal ?? r.status}${r.error ? ' ' + r.error : ''} before clang did`, ms: 0, mem: 0 };
  if (r.status !== out.code) out.node = r.signal ?? r.status;
  fs.rmSync(path.dirname(result), { recursive: true, force: true });
  return out;
}

function runNative(dir, argv) {
  const r = spawnSync(argv[0], argv.slice(1), { cwd: dir, stdio: ['ignore', 'inherit', 'inherit'] });
  if (r.status !== 0) throw new Error('native compile failed: ' + argv.join(' '));
}

async function main(args) {
  const selfTest = args.includes('--self-test');
  const rerun = args.includes('--native-rerun');
  args = args.filter((a) => a !== '--self-test' && a !== '--native-rerun');
  const [wasmPath, dir, ...objs] = args;
  if (!objs.length) {
    console.error('usage: check-wasm-objects.mjs [--self-test] [--native-rerun] CLANG.wasm DIR OBJ[=COMMAND]...');
    process.exit(2);
  }
  let bad = 0;
  for (const spec of objs) {
    const eq = spec.indexOf('=');
    const obj = eq < 0 ? spec : spec.slice(0, eq);
    const { words: argv, rest } = shellWords(eq < 0 ? kbuildCommand(dir, obj) : spec.slice(eq + 1));
    const o = argv.indexOf('-o');
    if (o < 0 || argv[o + 1] !== obj) throw new Error(`${obj}: the command does not write it: ${argv.join(' ')}`);
    const objPath = path.join(dir, obj);
    const saved = fs.readFileSync(objPath);
    const st = fs.statSync(objPath);
    const md = argv.find((a) => a.startsWith('-Wp,-MMD,') || a.startsWith('-Wp,-MD,'));
    const depPath = md ? path.join(dir, md.split(',')[2]) : null;
    const depSaved = depPath && fs.existsSync(depPath) ? fs.readFileSync(depPath) : null;
    let ref = saved, refName = 'the build\'s object';
    try {
      if (rerun || rest) {
        runNative(dir, argv);
        ref = fs.readFileSync(objPath);
        refName = `native ${path.basename(argv[0])} run again (the build's object ` +
          (sha(ref) === sha(saved) ? 'is the same' : `differs: ${saved.length} B ${sha(saved)}`) + ')';
      }
      fs.rmSync(objPath);
      const r = runWasm(wasmPath, dir, argv);
      const out = fs.existsSync(objPath) ? fs.readFileSync(objPath) : null;
      const same = r.code === 0 && out !== null && sha(out) === sha(ref);
      console.log(`${same ? 'SAME' : 'DIFFERENT'} ${obj}: clang.wasm ${out ? out.length + ' B ' + sha(out) : 'no output (exit ' + r.code + ')'}, ` +
        `${refName} ${ref.length} B ${sha(ref)} (clang exit ${r.code}, ${r.ms} ms, wasm memory ${r.mem >> 20} MiB` +
        (r.node !== undefined ? `; node then exited with ${r.node}` : '') + ')');
      if (selfTest && obj === (objs[0].split('=')[0])) {
        const other = argv.map((a) => /^-O[0-9sz]?$/.test(a) ? (a === '-O0' || a === '-Os' ? '-O2' : '-Os') : a);
        if (!other.some((a, k) => a !== argv[k])) other.splice(1, 0, '-Os');
        fs.rmSync(objPath);
        const r2 = runWasm(wasmPath, dir, other);
        const out2 = fs.existsSync(objPath) ? fs.readFileSync(objPath) : null;
        if (r2.code !== 0 || out2 === null) throw new Error(`self-test: clang.wasm failed at the other -O level (exit ${r2.code})`);
        if (sha(out2) === sha(ref)) throw new Error(`self-test: ${obj} at another -O level compared equal`);
        console.log(`self-test: ${obj} at another -O level is reported as different (${sha(out2)})`);
      }
      if (!same) {
        bad++;
        // For a look at the difference (llvm-objdump, llvm-dwarfdump).
        if (out) fs.writeFileSync(objPath + '.clang-wasm', out);
      }
    } finally {
      fs.writeFileSync(objPath, saved);
      fs.utimesSync(objPath, st.atime, st.mtime);
      if (depPath) {
        if (depSaved) fs.writeFileSync(depPath, depSaved);
        else fs.rmSync(depPath, { force: true });
      }
    }
  }
  if (bad) {
    console.error(`${bad} of ${objs.length} objects differ`);
    process.exit(1);
  }
}

if (process.argv[1] && fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv[2] === '--wasm-child') await wasmChild(...process.argv.slice(3));
  else await main(process.argv.slice(2));
}
