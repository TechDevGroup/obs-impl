# Claude Code Project Configuration

## Primary Directive: Skill-First Approach

**CRITICAL**: Before reading source files to understand conventions, architecture, or implementation details, ALWAYS consult the skill documents in `.claude/` first.

### Available Skills

| Skill | Path | Purpose |
|-------|------|---------|
| **Coding Conventions** | `.claude/coding-conventions/SKILL.md` | Code style, formatting, naming, commit messages |
| **Multi-Output Stages** | `.claude/multi-output-stages/SKILL.md` | Feature implementation plan, architecture notes |

### Workflow

1. **Check skill documents first** - They contain consolidated knowledge extracted from source files
2. **Update skill documents** - When you discover new information during implementation, update the relevant skill document
3. **Read source only when necessary** - For specific implementation details not covered in skill documents
4. **Prefer skill references** - In responses, cite skill documents rather than raw source locations when possible

### Rationale

- Reduces redundant source exploration across sessions
- Maintains consistent understanding of the codebase
- Builds institutional knowledge that persists
- Speeds up future development work

---

## Project Overview

This is a fork of [OBS Studio](https://github.com/obsproject/obs-studio) for implementing multi-output stage functionality.

### Feature Goal

Enable a single OBS instance to manage multiple "stages" (canvases) with different aspect ratios, routing each to independent output targets. See `.claude/multi-output-stages/SKILL.md` for full specification.

### Key Technologies

- **Core Library**: C (libobs)
- **Frontend**: C++17 with Qt 6
- **Build System**: CMake
- **Platforms**: Windows, macOS, Linux

---

## Development Guidelines

### Before Making Changes

1. Read `.claude/coding-conventions/SKILL.md` for style requirements
2. Review relevant sections of `.claude/multi-output-stages/SKILL.md` for architecture context
3. Check earmarked investigation items in skill docs

### When Implementing

1. Follow Linux kernel coding style (tabs, 120 columns, brace placement)
2. Use existing patterns from the codebase
3. Add appropriate signal handlers for new objects
4. Maintain reference counting discipline

### After Implementing

1. Update skill documents with new findings
2. Mark completed investigation items
3. Document any architectural decisions

---

## Quick Commands

```bash
# Build (after cmake configuration)
cmake --build build

# Format code
./build-aux/format-code.sh

# Run tests
ctest --test-dir build
```

---

## Repository Structure

```
obs-impl/
├── .claude/                    # Claude skill documents
│   ├── coding-conventions/     # Style guide
│   └── multi-output-stages/    # Feature implementation
├── libobs/                     # Core C library
├── frontend/                   # Qt UI application
├── plugins/                    # Plugin modules
├── deps/                       # Dependencies
├── cmake/                      # CMake modules
└── CLAUDE.md                   # This file
```

---

## Contact & Resources

- Original OBS Docs: https://obsproject.com/docs
- OBS Discord: https://obsproject.com/discord
- Fork Repo: https://github.com/techdevgroup/obs-impl
