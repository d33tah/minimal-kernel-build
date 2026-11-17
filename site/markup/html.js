// A JSX runtime that writes HTML (R3): the tabs' static markup is written
// in JSX (markup/pagepanel.jsx, markup/about.jsx, ...: a file with the
// pragmas `@jsxRuntime classic`, `@jsx h`, `@jsxFrag Fragment`), and h()
// turns it into an HTML string. build.mjs runs it at build time and writes
// the strings into index.html (from index.src.html, markup/static.js
// names the elements), so the browser parses and lays out the tabs while
// the bundle downloads, as before R3; the page does not load these files.
// React stays for the views that change (tabs.jsx, settings.jsx,
// quests.jsx, events.jsx, rebuild.jsx). The props are React's names
// (className, htmlFor, defaultValue, style objects), so a static file can
// become a React component unchanged.

const VOID = new Set(["area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"]);
const NAMES = { className: "class", htmlFor: "for", tabIndex: "tabindex", readOnly: "readonly", maxLength: "maxlength",
	colSpan: "colspan", rowSpan: "rowspan", autoComplete: "autocomplete", spellCheck: "spellcheck", autoCapitalize: "autocapitalize",
	enterKeyHint: "enterkeyhint", inputMode: "inputmode", contentEditable: "contenteditable", accessKey: "accesskey",
	autoFocus: "autofocus", viewBox: "viewBox", strokeWidth: "stroke-width", fillRule: "fill-rule", strokeLinecap: "stroke-linecap",
	strokeLinejoin: "stroke-linejoin", clipRule: "clip-rule", defaultValue: "value", defaultChecked: "checked" };

const escText = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const escAttr = (s) => String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;");
const cssName = (k) => (k.startsWith("--") ? k : k.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase()));

// A fragment's children: the HTML of each, in order.
const Fragment = (props) => ({ html: kids(props.children) });

// The markup has elements and text only (no lists, no conditional parts).
function kids(list) {
	let out = "";
	for (const k of list)
		out += typeof k === "object" ? k.html : escText(k);
	return out;
}

// h(tag, props, ...children) -> { html }: the element as HTML (an object,
// so a child's HTML is not escaped again as text).
function h(tag, props, ...children) {
	if (typeof tag === "function")
		return { html: tag({ ...props, children }).html ?? "" };
	let open = "<" + tag;
	let selected = null;
	for (const [k, v] of Object.entries(props || {})) {
		if (tag === "select" && k === "defaultValue") {
			selected = String(v);
			continue;
		}
		const name = NAMES[k] || k;
		if (k === "style")
			open += ' style="' + escAttr(Object.entries(v).map(([p, x]) => cssName(p) + ": " + x).join("; ")) + '"';
		else if (v === true)
			open += " " + name;
		else
			open += " " + name + '="' + escAttr(v) + '"';
	}
	open += ">";
	if (VOID.has(tag))
		return { html: open };
	let inner = kids(children);
	if (selected !== null)
		inner = inner.replace(new RegExp('(<option value="' + selected.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + '")'), "$1 selected");
	return { html: open + inner + "</" + tag + ">" };
}

export { h, Fragment };
