---
title: "The Impostor Problem: Killing ABA with Generational IDs"
description: "A technical postmortem on a subtle entity-reuse bug in the ECS, and the atomic generational-handle fix."
date: 2026-06-15
kind: postmortem
draft: true
---

## The symptom

_Draft outline — replace with the real war story._

An entity would occasionally receive a message meant for a long-dead entity that once held the
same slot. The classic **ABA problem**: a handle is freed, the slot is reused, and a stale handle
now points at a different entity that looks identical.

## Root cause

- Handles were raw indices into the component arrays.
- Reuse of a slot made an old handle silently valid again.

## The fix: generational IDs

- Pack a `(index, generation)` pair into each handle.
- Bump the generation atomically on free/reuse.
- A lookup validates the generation before dereferencing — stale handles are rejected.

```c
// handle = (generation << INDEX_BITS) | index
```

## Why it had to be atomic

- The free/reuse path races with lookups on other cores.
- Discuss the memory ordering used and why.

## Aftermath

- What testing caught it, and the invariant now enforced.
