// store.js against what React's useSyncExternalStore needs (R3) and what the
// page relies on: getSnapshot() is the same object until a value changes and
// a new frozen one after; set() of the value already there tells nobody;
// listeners are told synchronously, a keyed one only for its keys; the
// unsubscribe works, also from inside a notification; a throwing listener
// does not stop the others; an unknown key throws.
// Usage: node site/test_store.mjs
import assert from "node:assert/strict";
import { Store } from "./store.js";

const s = new Store({ a: 1, b: "x", o: null });
const snap0 = s.getSnapshot();
assert.equal(s.getSnapshot(), snap0, "same snapshot while nothing changed");
assert.ok(Object.isFrozen(snap0));
assert.deepEqual({ ...snap0 }, { a: 1, b: "x", o: null });

const seen = [], keyed = [];
const off = s.subscribe((k) => seen.push(k));
s.subscribe((k) => keyed.push(k), ["b"]);
assert.equal(s.set("a", 1), false, "same value: no change");
assert.equal(s.getSnapshot(), snap0);
assert.deepEqual(seen, []);
assert.equal(s.set("a", 2), true);
assert.deepEqual(seen, ["a"], "told at once");
assert.deepEqual(keyed, [], "keyed listener: not its key");
const snap1 = s.getSnapshot();
assert.notEqual(snap1, snap0);
assert.equal(snap1.a, 2);
assert.equal(snap0.a, 1, "an old snapshot does not change");
s.set("b", "y");
assert.deepEqual(keyed, ["b"]);
assert.equal(s.get("b"), "y");
const obj = {};
s.set("o", obj);
assert.equal(s.getSnapshot().o, obj, "values are kept as they are");
s.set("a", NaN);
seen.length = 0;
s.set("a", NaN);
assert.deepEqual(seen, [], "NaN is the same value (Object.is)");
off();
s.set("a", 3);
assert.deepEqual(seen, [], "unsubscribed");

// removed by an earlier listener in the same notification: not called
const t = new Store({ v: 0 });
const calls = [];
let offB = null;
t.subscribe(() => { calls.push("A"); offB(); });
offB = t.subscribe(() => calls.push("B"));
t.set("v", 1);
assert.deepEqual(calls, ["A"]);

// a throwing listener: the others still run, set() returns, the error is reported after
const u = new Store({ v: 0 });
const ran = [];
u.subscribe(() => { throw new Error("boom"); });
u.subscribe(() => ran.push("after"));
let reported = null;
process.once("uncaughtException", (e) => { reported = e; });
assert.equal(u.set("v", 1), true);
assert.deepEqual(ran, ["after"]);
assert.throws(() => u.set("nope", 1), /no key nope/);
await new Promise((r) => setTimeout(r, 10));
assert.equal(reported && reported.message, "boom");

// subscribe and getSnapshot work unbound (useSyncExternalStore(s.subscribe, s.getSnapshot))
const { subscribe, getSnapshot } = s;
const before = getSnapshot();
let told = 0;
const off2 = subscribe(() => told++);
s.set("b", "z");
assert.equal(told, 1);
assert.notEqual(getSnapshot(), before);
off2();
console.log("store: snapshot, set, subscribe, keys, unsubscribe, errors OK");
