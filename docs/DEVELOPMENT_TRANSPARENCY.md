# Development transparency

This document exists because the maintainer believes you deserve to know how
this project was actually made.

## AI-assisted development

Alek's Ultimate NX Edition was developed with **significant AI assistance**.
The tools used across development include:

- **Claude / Claude Code** (Anthropic)
- **ChatGPT** (OpenAI)
- **OpenAI Codex**

AI assistants were used for, among other things:

- investigating and navigating the large decompiled codebase
- debugging (including root-causing real-hardware rendering and timing bugs)
- planning and implementing features
- reviewing code changes
- comparing this tree against upstream projects
- writing and translating documentation
- repetitive development work (builds, hashing, staging, release preparation)

## What was done manually

AI did not run this project. The maintainer personally:

- set the project direction and decided every feature
- made all design and scope decisions (what ships, what waits, what is cut)
- tested every release candidate on **real Nintendo Switch hardware**
- validated rendering, performance, audio and UX visually and by playing
- captured all screenshots on real hardware
- decided what is and is not ready to publish

Bugs were confirmed and regressions caught on real hardware, not just in
emulators or by static analysis. Where an AI proposed a change, the change was
reviewed, built, and hardware-tested before being accepted.

## What this project is not

- It is **not** an auto-generated codebase. The overwhelming majority of the
  code is the upstream decompilation and the upstream native-port projects
  credited in [../CREDITS.md](../CREDITS.md), written by their human authors.
- AI assistance applies to **this edition's Switch-specific layer and release
  work**, not to the upstream projects, whose authorship is entirely their
  own.
- Nothing about AI use changes the licensing of upstream code, which remains
  under its original licenses.

## Why say all this?

Because pretending otherwise would be dishonest, and because this project may
be useful to others precisely *as* an example of AI-assisted homebrew
development done with real-hardware validation. If you fork this project, you
are welcome to develop it any way you like — but this edition will always be
transparent about how it was built.
