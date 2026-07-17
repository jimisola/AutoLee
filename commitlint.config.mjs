// Conventional Commits enforcement (https://www.conventionalcommits.org/).
// ES module (.mjs) — the modern config format.
export default {
  extends: ["@commitlint/config-conventional"],
  rules: {
    // config-conventional defaults + "tools" (dev tooling / scripts).
    "type-enum": [
      2,
      "always",
      [
        "build",
        "chore",
        "ci",
        "docs",
        "feat",
        "fix",
        "perf",
        "refactor",
        "revert",
        "style",
        "test",
        "tools",
      ],
    ],
  },
};
