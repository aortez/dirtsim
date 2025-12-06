// ESLint flat config for Sparkle Duck web resources.
// See: https://eslint.org/docs/latest/use/configure/configuration-files-new

import globals from "globals";

export default [
    {
        files: ["src/server/web/**/*.js"],
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
            // Possible errors.
            "no-console": "off",           // Console is fine for debugging.
            "no-debugger": "warn",
            "no-dupe-args": "error",
            "no-dupe-keys": "error",
            "no-duplicate-case": "error",
            "no-empty": "warn",
            "no-extra-semi": "error",
            "no-func-assign": "error",
            "no-irregular-whitespace": "error",
            "no-unreachable": "error",
            "no-unsafe-negation": "error",
            "valid-typeof": "error",

            // Best practices.
            "curly": ["warn", "multi-line"],
            "eqeqeq": ["warn", "smart"],
            "no-caller": "error",
            "no-eval": "error",
            "no-implied-eval": "error",
            "no-loop-func": "warn",
            "no-multi-spaces": "warn",
            "no-new-wrappers": "error",
            "no-redeclare": "error",
            "no-unused-expressions": "warn",
            "no-useless-escape": "warn",
            "no-with": "error",

            // Variables.
            "no-shadow": "warn",
            "no-undef": "error",
            "no-unused-vars": ["warn", { "vars": "all", "args": "none" }],
            "no-use-before-define": ["error", { "functions": false }],

            // Stylistic (light touch - don't be too opinionated).
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
