# Contributing to Shammy

Thanks for wanting to help. You do not need to contribute code to be useful.

## Report a problem

Use the [GitHub issue tracker](https://github.com/TragicWarrior/shammy/issues).

Please include:

- What you did, what you expected, and what happened
- OS and Shammy version (or git commit)
- Backend and model, if the bug is in chat, tools, or streaming
- Relevant log lines or a screenshot for UI bugs

Search existing issues first. One problem per issue.

## Contribute code

1. Fork the repository.
2. Create a branch from `main`.
3. Make a focused change.
4. Open a pull request against `main`.

If the change is large (new settings, a new artifact type, a new network tool), open an issue first so we can agree on the approach.

By submitting a pull request, you license your contribution under the same MIT License as the rest of Shammy (`LICENSE`).

### C++

New and edited C++ must use **Allman** braces: the opening `{` goes on its own line. Do not write `if (x) {`. Keep `if` / `for` / `while` bodies in braces even when they are a single statement.

The repo `.clang-format` is LLVM-based with `BreakBeforeBraces: Allman`, 4-space indent, and a 100-column limit. Run that on C++ you touch. Target **Qt 6.4** / C++20; do not require a newer Qt than that without discussion.

QML and JavaScript in `.qml` files should match the surrounding file. Those files are not Allman; do not restyle a whole QML file to make a one-line fix.

### Visual conventions

Shammy is meant to look like a quiet ChatGPT / Claude desktop client: charcoal surfaces, light type, almost no chroma. Color lives in `qml/Theme.qml`. Use those tokens (`Theme.bg`, `Theme.panel`, `Theme.sidebar`, `Theme.text`, `Theme.muted`, `Theme.border`, `Theme.hairline`, `Theme.hover`, `Theme.selected`, and the few semantic colors such as `Theme.danger` and `Theme.warning`). Do not introduce a new fill, stroke, or accent because it “pops.” The product accent is near-white on dark (near-black on light), not a brand hue. The green shamrock is the exception; leave it as the mark, not a general UI green.

Layout is restrained: 12 / 18 / 26 corner radii, 1px hairlines, 12px muted captions, 13–16px labels. Buttons are rounded rectangles or pills already used in Settings, the composer, and menus. Prefer those over new control chrome, drop shadows, gradients, or dense icon toolbars. Light theme must keep working whenever you touch color. If you change layout, check the main chat, sidebar, settings sheet, and artifact pane so a local tweak does not break a sibling view.

### Tests and hygiene

- `cmake --build build -j && ctest --test-dir build --output-on-failure` should still pass.
- Put logic that can be tested without the GUI in `src/` and add or extend a test under `tests/`.
- Do not commit `build/`, editor junk, API keys, personal config, or sample data files (CSVs, exports, screenshots of your chats).
- Keep the PR scoped. Unrelated refactors belong in their own branch.

## Questions

If you are unsure whether something is a bug or a feature, file an issue anyway. That is easier to redirect than a surprise pull request.
