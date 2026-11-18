// dbgmerge.js DbgMerge.types (plan E6a): the merged variables of a kernel
// rebuilt from a kit without debug sections are CI's, moved, plus the
// variables of the units compiled here that CI's list lacks (a global the
// edit added, one whose size changed), with their types copied into CI's
// type table (the two tables number their types differently), their
// sections and files named in CI's lists. CI's data is not changed.
// Usage: node site/test_dbgmerge.mjs
import assert from "node:assert/strict";
import { DbgMerge } from "./dbgmerge.js";
import { DWT } from "./dwarfread.js";
import { dwTypeName, dwTypeSize, dwWhere } from "./dwarflocals.js";

// CI's types.json: int, a struct s { int a; char *p; }.
const ci = {
	format: 2,
	types: [[DWT.base, "int", 4, 5], [DWT.base, "char", 1, 6], [DWT.ptr, 1, 4], [DWT.struct, "s", 8, [["a", 0, 0], ["p", 4, 2]], "a.c:1"]],
	vars: [["old_global", 0x1000, 0, 4, 0, 1, "", 0, 3], ["moved_struct", 0x1100, 3, 8, 1, 1, "", 0, 4],
		["gone", 0x9000, 0, 4, 0, 1]],
	sections: [".data", ".bss"],
	files: ["a.c"],
	shrunk: [[0x1104, 1, 0]],
};
const ciCopy = JSON.stringify(ci);

// The files made here (init/main.c compiled): another numbering, with
// an enum, a typedef, an array, a const and a function pointer.
const own = {
	format: 2,
	types: [[DWT.typedef, "u32", 3], [DWT.base, "int", 4, 5], [DWT.base, "char", 1, 6], [DWT.base, "unsigned int", 4, 7],
		[DWT.array, 2, [16]], [DWT.const, 4], [DWT.struct, "pair", 8, [["x", 0, 1], ["y", 4, 0]], "init/main.c:9"],
		[DWT.enum, "mode", 4, [["A", 0], ["B", 1]], 0, "init/main.c:3"], [DWT.func, 1, [1, 0], 0], [DWT.ptr, 8, 4],
		[DWT.struct, "pair", -1, null, "", 6], [DWT.ptr, 10, 4]],
	vars: [
		// The same variable CI has, where CI's moved to: not added twice.
		["old_global", 0x1010, 1, 4, 0, 1, "", 0, 7],
		// New globals of the edit.
		["e6a_count", 0x2000, 1, 4, 0, 1, "", 0, 20],
		["e6a_name", 0x2004, 5, 16, 2, 1, "", 0, 21],
		["e6a_pair", 0x2014, 6, 8, 1, 0, "", 0, 22],
		["e6a_mode", 0x201c, 7, 4, 1, 0],
		["e6a_fn", 0x2020, 9, 4, 0, 1, "", 1, 24],
		["e6a_n", 0x2024, 0, 4, 0, 5, "init_fn", 0, 25],
		["e6a_flag", 0x2028, 1, 1, 1, 8, "", 0, 26],
		// A pointer to a declaration (its definition in [5]); defined twice.
		["e6a_pp", 0x202c, 11, 4, 1, 1, "", 0, 27, [[1, 5]]],
	],
	sections: [".data", ".bss", ".rodata"],
	files: ["init/main.c", "include/linux/x.h"],
	shrunk: [[0x2028, -1, 1]],
};

const m = new DbgMerge(null, "own/", { dwarf: "ship", symbols: "s.json" }, []);
// Old [0x1000, 0x1200) moved by +0x10.
m.physOld = [[0x1000, 0x1200, 0x10]];
m.owned = [];
m.ownT = own;
const T = m.types(ci);

assert.equal(JSON.stringify(ci), ciCopy, "CI's data is not changed");
const by = new Map(T.vars.map((v) => [v[0], v]));
assert.deepEqual(T.vars.map((v) => v[1]), [...T.vars.map((v) => v[1])].sort((a, b) => a - b), "sorted by address");
assert.equal(T.vars.filter((v) => v[0] === "old_global").length, 1, "a variable CI has is not added again");
assert.equal(by.get("old_global")[1], 0x1010);
assert.equal(by.get("moved_struct")[1], 0x1110);
assert.ok(!by.has("gone"), "a variable that did not move is dropped");
const names = ["e6a_count", "e6a_name", "e6a_pair", "e6a_mode", "e6a_fn", "e6a_n", "e6a_flag"];
for (const n of names)
	assert.ok(by.has(n), n + " is in the merged list");
const ty = (n) => dwTypeName(T, by.get(n)[2]);
assert.equal(ty("e6a_count"), "int");
assert.equal(ty("e6a_name"), "const char [16]");
assert.equal(ty("e6a_pair"), "struct pair");
assert.equal(ty("e6a_mode"), "enum mode");
assert.equal(ty("e6a_fn"), "int (*)(int, u32)");
assert.equal(dwTypeSize(T, by.get("e6a_pair")[2]), 8);
const pair = T.types[by.get("e6a_pair")[2]];
assert.equal(dwTypeName(T, pair[3][0][2]), "int");
const u32 = T.types[by.get("e6a_mode")[2]][4];
assert.equal(dwTypeName(T, u32), "u32");
assert.equal(dwTypeName(T, T.types[u32][2]), "unsigned int");
// Sections and files by name in CI's lists.
assert.equal(T.sections[by.get("e6a_name")[4]], ".rodata");
assert.equal(T.sections[by.get("e6a_count")[4]], ".data");
assert.equal(T.sections[by.get("e6a_pair")[4]], ".bss");
assert.equal(T.files[by.get("e6a_count")[7]], "init/main.c");
assert.equal(T.files[by.get("e6a_fn")[7]], "include/linux/x.h");
assert.equal(by.get("e6a_n")[6], "init_fn", "a static local keeps its function");
// A shrunk global keeps its k, c; CI's moved.
assert.deepEqual(T.shrunk, [[0x1114, 1, 0], [0x2028, -1, 1]]);
// The page names an address of a new global by its member path.
assert.equal((dwWhere(T, 0x2018) || {}).path, "e6a_pair.y");
assert.equal((dwWhere(T, 0x2000) || {}).path, "e6a_count");
const decl = T.types[T.types[by.get("e6a_pp")[2]][1]];
assert.equal(decl[3], null);
assert.equal(decl[5], by.get("e6a_pair")[2], "a declaration points to the copied definition");
assert.deepEqual(by.get("e6a_pp")[9], [[T.files.indexOf("include/linux/x.h"), 5]]);
// CI's types keep their ids.
assert.deepEqual(T.types.slice(0, ci.types.length), ci.types);

// Without the files made here: CI's only.
const m2 = new DbgMerge(null, "own/", { dwarf: "ship", symbols: "s.json" }, []);
m2.physOld = m.physOld;
m2.owned = [];
const T2 = m2.types(ci);
assert.equal(T2.types, ci.types);
assert.deepEqual(T2.vars.map((v) => v[0]), ["old_global", "moved_struct"]);
console.log("test_dbgmerge: OK (" + T.vars.length + " variables, " + (T.types.length - ci.types.length) + " types copied)");
