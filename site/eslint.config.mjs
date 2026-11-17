// The site's scripts are ES modules that name what they use (R1 in the plan):
// no shared globals, no import cycles; React components follow the rules of
// hooks (R3). CI runs this (pages.yml, job "lint"):
//   cd site && npm ci && npx eslint .
// window.MKB (debug.js) is the page's one global of its own; libv86.js (V86)
// is a classic script of v86, loaded before the modules.
import importPlugin from "eslint-plugin-import";
import reactHooks from "eslint-plugin-react-hooks";
import globals from "globals";

// A module that makes a global by assigning to window/globalThis/self.
const NEW_GLOBAL = [
	{
		selector: "AssignmentExpression > MemberExpression.left[object.name=/^(window|globalThis|self)$/][property.name!=/^on[a-z]+$/]",
		message: "No globals: export it (R1). window.MKB in debug.js is the one debug/test handle.",
	},
	{
		selector: "CallExpression[callee.object.name='Object'][callee.property.name=/^define(Property|Properties)$/] > Identifier.arguments:first-child[name=/^(window|globalThis|self)$/]",
		message: "No globals: export it (R1). window.MKB in debug.js is the one debug/test handle.",
	},
];

const rules = {
	"no-undef": "error",
	"no-implicit-globals": ["error", { lexicalBindings: true }],
	"no-restricted-syntax": ["error", ...NEW_GLOBAL],
	"import/no-cycle": ["error", { ignoreExternal: true }],
	"import/named": "error",
	"import/no-unresolved": "error",
};

export default [
	{ ignores: ["node_modules/**", "v86/**", "iced/**", "clang/**", "clang-native/**", "kernels/**", "jscov/**", "test-screenshots/**",
		"app.bundle.js"] },
	{
		// The page's modules and its React components (R3: *.jsx, bundled by
		// build.mjs).
		files: ["*.js", "*.jsx"],
		languageOptions: {
			sourceType: "module",
			ecmaVersion: 2024,
			parserOptions: { ecmaFeatures: { jsx: true } },
			globals: { ...globals.browser, V86: "readonly" },
		},
		plugins: { import: importPlugin },
		settings: { "import/resolver": { node: { extensions: [".js", ".jsx", ".mjs"] } } },
		rules,
	},
	{
		// The tabs' static markup (JSX through markup/html.js), which
		// build.mjs runs at build time to write index.html.
		files: ["markup/*.js", "markup/*.jsx"],
		languageOptions: {
			sourceType: "module",
			ecmaVersion: 2024,
			parserOptions: { ecmaFeatures: { jsx: true } },
		},
		plugins: { import: importPlugin },
		settings: { "import/resolver": { node: { extensions: [".js", ".jsx", ".mjs"] } } },
		rules,
	},
	{
		// React's rules of hooks and the React Compiler's checks of
		// components (pure render, refs, effects), on the files that hold
		// components and hooks (kernel.js's useImage is no hook).
		files: ["*.jsx"],
		...reactHooks.configs.flat.recommended,
		rules: { ...reactHooks.configs.flat.recommended.rules, "react-hooks/exhaustive-deps": "error" },
	},
	{
		// Module Workers.
		files: ["dwarfworker.js", "genworker.js", "clangworker.js", "snapworker.js"],
		languageOptions: { globals: { ...globals.worker } },
	},
	{
		// Node: the generators and the Node tests (they also import page modules).
		files: ["gen-dwarf.js", "gen-src.js", "*.mjs"],
		languageOptions: { sourceType: "module", ecmaVersion: 2024, globals: { ...globals.node } },
		plugins: { import: importPlugin },
		rules,
	},
	{
		// The service worker is a classic script in its own global scope (no
		// page globals); browsers that run a service worker module are fewer.
		files: ["sw.js"],
		languageOptions: { sourceType: "script", globals: { ...globals.serviceworker } },
		rules: { "no-implicit-globals": "off", "import/no-cycle": "off" },
	},
];
