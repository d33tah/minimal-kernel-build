// While the page's scripts move to ES modules (R1 in the plan), the scripts
// that are still classic and the browser tests read a module's exports as
// globals: bridge(namespace) defines them on globalThis as getters of the
// live bindings (a setFoo export, if any, takes assignments to foo). Goes
// away once every page script is a module.
export function bridge(ns) {
	for (const k of Object.keys(ns)) {
		const set = ns["set" + k[0].toUpperCase() + k.slice(1)];
		Object.defineProperty(globalThis, k, { get: () => ns[k], set: typeof set === "function" ? set : undefined, configurable: true });
	}
}
