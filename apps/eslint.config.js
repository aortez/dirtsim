// ESLint flat config for Sparkle Duck web resources.
// See: https://eslint.org/docs/latest/use/configure/configuration-files-new

import globals from "globals";

export default [
    {
        files: ["apps/src/server/web/**/*.js", "src/server/web/**/*.js"],
        languageOptions: {
            ecmaVersion: 2020,
            sourceType: "script",
            globals: {
                ...globals.browser,
                // WebRTC globals.
                RTCPeerConnection: "readonly",
                RTCSessionDescription: "readonly",
            }
        },
        rules: {
            // =================================================================
            // Possible Errors
            // =================================================================
            "no-console": "off",              // Console is fine for debugging.
            "no-constant-condition": "error", // if (true) or while (1).
            "no-debugger": "warn",
            "no-dupe-args": "error",
            "no-dupe-keys": "error",
            "no-duplicate-case": "error",
            "no-empty": "warn",
            "no-extra-semi": "error",
            "no-func-assign": "error",
            "no-irregular-whitespace": "error",
            "no-loss-of-precision": "error",  // Numbers too precise for JS.
            "no-self-compare": "error",       // x === x is always true.
            "no-template-curly-in-string": "warn", // "${x}" should use backticks.
            "no-unmodified-loop-condition": "error", // Infinite loop prevention.
            "no-unreachable": "error",
            "no-unsafe-negation": "error",
            "valid-typeof": "error",

            // =================================================================
            // Async/Promise Safety
            // =================================================================
            "no-async-promise-executor": "error",    // async in new Promise() is dangerous.
            "no-promise-executor-return": "error",   // Return in executor is confusing.
            "require-atomic-updates": "warn",        // Race condition prevention.

            // =================================================================
            // Best Practices
            // =================================================================
            "consistent-return": "warn",      // Functions should consistently return or not.
            "curly": ["warn", "multi-line"],
            "default-case": "warn",           // Switch should have default.
            "eqeqeq": ["warn", "smart"],
            "no-caller": "error",
            "no-eval": "error",
            "no-implied-eval": "error",
            // "no-implicit-globals" - Disabled, script uses globals for HTML onclick.
            "no-loop-func": "warn",
            "no-multi-spaces": "warn",
            "no-new-wrappers": "error",
            "no-redeclare": "error",
            "no-return-assign": "error",      // return x = 5 is usually a typo.
            "no-throw-literal": "error",      // throw "error" -> throw new Error().
            "no-unused-expressions": "warn",
            "no-useless-escape": "warn",
            "no-with": "error",

            // =================================================================
            // Variables - Scope and declaration issues.
            // =================================================================
            "no-shadow": "warn",
            "no-undef": "error",
            "no-unused-vars": ["error", {
                "vars": "all",
                "args": "none",
                "varsIgnorePattern": "^_",
                "caughtErrorsIgnorePattern": "^_"
            }],
            "no-use-before-define": ["error", { "functions": false }],

            // =================================================================
            // Complexity
            // =================================================================
            "max-depth": ["warn", 5],                // Max nesting depth.
            "max-nested-callbacks": ["warn", 4],     // Prevent callback hell.
            "complexity": ["warn", 20],              // Cyclomatic complexity limit.

            "brace-style": ["warn", "1tbs", { "allowSingleLine": true }],
            "comma-dangle": ["warn", "never"],
            "comma-spacing": "warn",
            "indent": ["warn", 4, { "SwitchCase": 1 }],
            "keyword-spacing": "warn",
            "no-trailing-spaces": "warn",
            "semi": ["error", "always"],
            "space-before-blocks": "warn",
            "space-infix-ops": "warn"
        }
    }
];
