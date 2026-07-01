---
title: "Benchmarks: 10k Entities in 23 µs"
description: "Stress-test results for the physics and networking paths, with methodology notes."
date: 2026-06-01
kind: benchmark
draft: true
---

## Headline numbers

_Draft — attach the raw data tables and plots._

| Metric | Result |
| --- | --- |
| 10,000 entities, physics step (CUDA) | **23 µs** |
| Network loop, average frame time | **70.15 µs** |

## Setup

- Hardware, build profile (client/server), compiler flags.
- How timing was captured and averaged.

## Physics path

- SoA layout, batch size, and why the cache behaviour matters.

## Network path

- Zero-copy injection and where the 70.15 µs is spent.

## Reproducing

- Commands and the commit hash the numbers were taken at.
