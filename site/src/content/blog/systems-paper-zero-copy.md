---
title: "Zero-Copy from NIC to GPU inside a Kernel-Space Engine"
description: "A systems-paper outline: how LplPlugin, running in ring-0 of LplKernel, collapses the network→render latency chain."
date: 2026-06-30
kind: systems-paper
draft: true
---

## Abstract

_Draft outline — fill with the real numbers and diagrams._

LplPlugin is a dual-build (client|server) game and graphics engine compiled natively into
LplKernel and executed in ring-0. This paper describes the data path that carries a UDP packet
from the network interface to the GPU without intermediate copies, and the memory and concurrency
architecture that makes it safe.

## 1. Motivation

- The classic path (NIC → kernel socket buffer → user copy → engine → GPU upload) crosses the
  syscall boundary twice and copies the payload several times.
- By placing the engine in kernel space, the boundary disappears; the remaining problem is
  eliminating the copies.

## 2. Architecture

- **Physical memory:** deterministic Free-List PMM (client) vs. throughput allocator (server).
- **Paging:** dynamic mapping of DMA regions shared between NIC and GPU.
- **SoA ECS:** contiguous component storage feeding the GPU upload directly.

_TODO: insert the Mermaid pipeline diagram from `docs/`._

## 3. Concurrency model

- Lock-free SPSC ring between the IRQ producer and the main-loop consumer.
- Acquire/release barriers to defeat CPU reordering.
- Generational IDs to make entity handles ABA-safe.

## 4. Evaluation

- 10,000 entities physics step: **23 µs** (CUDA path).
- Network loop average frame time: **70.15 µs**.

_TODO: describe the bench harness, hardware, and variance._

## 5. Lessons

- What the kernel-space placement bought us, and what it cost.
