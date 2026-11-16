// React on the page's store (R3): the hook a component reads page.js's
// state with, mount() for a component that takes over an element of
// index.html, and the small pieces every view uses.
//
// A component re-renders only when a key it reads changes (useStore("tab"));
// nothing per instruction writes the store, so the stepper never wakes
// React (README.md, "Page state and API").

import { useCallback, useSyncExternalStore } from "react";
import { flushSync } from "react-dom";
import { createRoot } from "react-dom/client";
import { state } from "./page.js";
import { srcName, srcLinkTitle } from "./source.js";

// One value of page.js's store.
function useStore(key) {
	const subscribe = useCallback((fn) => state.subscribe(fn, [key]), [key]);
	const get = useCallback(() => state.get(key), [key]);
	return useSyncExternalStore(subscribe, get);
}

// Render a component into an element at once: app.js mounts the shell
// before init() wires the controls, which then exist.
function mount(el, element) {
	const root = createRoot(el);
	flushSync(() => root.render(element));
	return root;
}

// The "?" that explains (tips.js shows its text on hover, tap and focus).
function Tip({ text }) {
	return <span className="tip" tabIndex={0} role="button" data-tip={text} aria-expanded="false" aria-label="Explain"
		aria-controls="tip_pop">?</span>;
}

// A file:line link to the Source panel, as source.js's srcLinkEl makes
// them (its click is source.js's, by the data attributes).
function SrcLink({ file, line, text, pa = -1 }) {
	return <button type="button" className="srcloc srclink" data-tip={srcLinkTitle(file, line)} data-file={file} data-line={line}
		data-pa={pa}>{text || srcName({ file, line })}</button>;
}

export { useStore, mount, Tip, SrcLink };
