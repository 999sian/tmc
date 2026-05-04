---
name: documentation-style-docs
description: "Use when creating or updating project documentation in the same architecture and writing style as FogStages.md: title + visual block, index, introduction, implementation plan, code locations table, TODO, and limitations/bugs. Trigger on requests like create docs, write documentation, add design document, feature spec, system write-up, or architecture note."
---

# Documentation Style Docs

Create markdown documentation that mirrors the structure, tone, and readability patterns used in `docs/Features/FogStages.md`.

## Use This Skill When

- The user asks to create new documentation.
- The user asks to rewrite or expand a feature/design doc.
- The user requests a technical write-up intended for contributors.

## Output Contract

Produce a markdown file with the following sections and order unless the user asks for a different structure.

1. H1 title with a concise, memorable phrase.
2. Optional centered visual block (gif/image) using HTML if an asset exists.
3. Horizontal rule.
4. Index section with anchor links to major sections.
5. Introduction section.
6. Plan section describing behavior and design intent.
7. Code Locations section as a table.
8. TODO section.
9. Limitations & Bugs section.

## Style Rules

- Keep language practical and contributor-focused.
- Explain player-facing impact first, then implementation details.
- Prefer short paragraphs and compact bullet lists.
- Use tables for behavior matrices and file responsibilities.
- Use clear section headings and predictable ordering.
- Keep claims concrete and verifiable in code.

## Architecture Pattern

Follow this architecture pattern by default.

- Introduction: problem statement, why current behavior is weak, and design goal.
- Plan: staged model or system model with an explicit legend/table.
- Code Locations: map each feature slice to concrete function names and file paths.
- TODO: leave explicit placeholders for future follow-up.
- Limitations & Bugs: document known constraints and invite issue reports.

## Code Locations Table Format

Use this 3-column format when possible.

| Feature | Location | Description |
|--------|----------|-------------|
| Feature slice | `FunctionName` in `path/to/file.c` | What this code is responsible for |

## Authoring Workflow

1. Identify the target mechanic/system and its player-facing goals.
2. Locate the relevant implementation files and core entry-point functions.
3. Build a behavior table for visibility states, stages, or modes.
4. Draft sections in the standard order.
5. Verify links and symbols match repository names exactly.

## Quality Checklist

- Headings and index anchors are valid.
- File/function references exist in the repository.
- Behavior table has no ambiguous state transitions.
- TODO and limitations are present even if brief.
- Tone matches contributor documentation, not marketing copy.