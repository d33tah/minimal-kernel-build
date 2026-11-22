// React on the page's store (R3): the hook a component reads page.js's
// state with, mount() for a component that takes over an element of
// index.html, and the small pieces every view uses.
//
// A component re-renders only when a key it reads changes (useStore("tab"));
// nothing per instruction writes the store, so the stepper never wakes
// React (README.md, "Page state and API").

import { useCallback, useLayoutEffect, useRef, useSyncExternalStore } from "react";
import { flushSync } from "react-dom";
import { createRoot, hydrateRoot } from "react-dom/client";
import { state } from "./page.js";
import { srcName, srcLinkTitle } from "./srclinks.js";

// One value of page.js's store.
function useStore(key) {
	const subscribe = useCallback((fn) => state.subscribe(fn, [key]), [key]);
	const get = useCallback(() => state.get(key), [key]);
	return useSyncExternalStore(subscribe, get);
}

// A view's controller (A1): an object of the fields a tab shows, which its
// module changes and then calls changed(); its component reads it with
// useController(c) and renders again. changed() draws at once (flushSync:
// a test or a handler reads the DOM right after), and costs nothing while
// the tab is not mounted (no subscriber). Fields may be anything the
// module keeps; the component reads them, never copies them.
function controller(fields) {
	const subs = new Set();
	const c = {
		...fields,
		atLoad: { ...fields }, // what a build-time render saw (useField)
		rev: 0,
		subscribe(fn) {
			subs.add(fn);
			return () => subs.delete(fn);
		},
		changed() {
			c.rev++;
			if (subs.size)
				flushSync(() => {
					for (const fn of subs)
						fn();
				});
		},
	};
	return c;
}

function useController(c) {
	return useSyncExternalStore(c.subscribe, () => c.rev);
}

// One field of a controller: the component renders again only when the
// module put another value there (===), not at every changed() (the
// Machine tab's parts, drawn with every frame while the machine runs).
// Hydrating, it renders the field's first value (what the build-time
// render saw), then the one the module has set since.
function useField(c, key) {
	return useSyncExternalStore(c.subscribe, () => c[key], () => c.atLoad[key]);
}

// Render a component into an element at once: app.js mounts the shell
// before init() wires the controls, which then exist.
function mount(el, element) {
	const root = createRoot(el);
	flushSync(() => root.render(element));
	return root;
}

// Take over an element whose markup build.mjs rendered from the same
// component (prerender.jsx). React renders it in the background (not at
// once: the page's init goes on meanwhile) and attaches to the elements
// that are there instead of making them.
function hydrate(el, element) {
	return hydrateRoot(el, element);
}

// DOM nodes a module's helper builds (a variable's declaration with its
// links, a function's facts), shown by a component: they are put into a
// span with display: contents after each render that made them anew.
function DomNodes({ nodes }) {
	const ref = useRef(null);
	useLayoutEffect(() => {
		ref.current.replaceChildren(...nodes);
	}, [nodes]);
	return <span ref={ref} className="domnodes" />;
}

// The "?" that explains (tips.js shows its text on hover, tap and focus).
function Tip({ text }) {
	return <span className="tip" tabIndex={0} role="button" data-tip={text} aria-expanded="false" aria-label="Explain"
		aria-controls="tip_pop">?</span>;
}

// A file:line link to the Source panel, as srclinks.js's srcLinkEl makes
// them (its click is source.js's, by the data attributes).
function SrcLink({ file, line, text, pa = -1 }) {
	return <button type="button" className="srcloc srclink" data-tip={srcLinkTitle(file, line)} data-file={file} data-line={line}
		data-pa={pa}>{text || srcName({ file, line })}</button>;
}

export { useStore, controller, useController, useField, mount, hydrate, DomNodes, Tip, SrcLink };
