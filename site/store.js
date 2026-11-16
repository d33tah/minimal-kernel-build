// A state store: named values, listeners told when one changes.
//
// subscribe(fn, keys) adds a listener (fn(key) after a value of keys, or of
// any key without them, changed) and returns the function that removes it;
// getSnapshot() returns a frozen object of every value, the same object
// until a value changes. Those two are what React's useSyncExternalStore
// takes (R3: useSyncExternalStore(state.subscribe, state.getSnapshot)).
// set(key, value) changes a value and tells the listeners at once, unless
// it is the value already there (Object.is). A listener that throws does
// not stop the others or the set(); its error is reported afterwards.
//
// The page's store is page.js's `state`; nothing per instruction writes it
// (see "Page state and API" in README.md).

class Store {
	constructor(values) {
		this.values = { ...values };
		this.snap = null;
		this.listeners = new Set();
		this.subscribe = this.subscribe.bind(this);
		this.getSnapshot = this.getSnapshot.bind(this);
	}

	get(key) {
		return this.values[key];
	}

	set(key, value) {
		if (!(key in this.values))
			throw new Error("store: no key " + key);
		if (Object.is(this.values[key], value))
			return false;
		this.values[key] = value;
		this.snap = null;
		this.notify(key);
		return true;
	}

	getSnapshot() {
		if (this.snap === null)
			this.snap = Object.freeze({ ...this.values });
		return this.snap;
	}

	subscribe(fn, keys = null) {
		const l = { fn, keys: keys && new Set(keys) };
		this.listeners.add(l);
		return () => this.listeners.delete(l);
	}

	notify(key) {
		for (const l of [...this.listeners]) {
			if ((l.keys && !l.keys.has(key)) || !this.listeners.has(l))
				continue; // not its key, or removed by a listener before it
			try {
				l.fn(key);
			} catch (e) {
				setTimeout(() => { throw e; });
			}
		}
	}
}

export { Store };
