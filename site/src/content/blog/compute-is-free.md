---
title: "Compute Is Free: Running AI at Home, One Byte at a Time"
description: "Why AI is bound by data movement rather than arithmetic (weights, knowledge, energy, reproducibility), and how far a single machine can go."
date: 2026-09-25
kind: systems-paper
draft: true
---

_Draft outline. The prose is not written yet. Each section lists its key points, the figure or
demo it needs, and the sources it rests on. Figures are tagged **measured** (produced in the
Laplace repositories), **derived** (computed from a formula, never measured) or **vendor**
(claimed by a manufacturer, not independently checked)._

## Abstract

- **Thesis, in one sentence:** the bottleneck of AI is not arithmetic, it is moving bytes, and
  whoever controls that movement, from the model's weights to the knowledge it is given to read,
  can run at home what the cloud bills for.
- **After reading, you should be able to:**
  1. predict the maximum speed of a model on your own machine before downloading it;
  2. tell the optimisations that matter (fewer bytes read) from those that do not;
  3. explain why giving a model *less* text to read is both a saving and a better answer;
  4. say honestly what a local model can and cannot do.
- One law, four objects: **weights**, **knowledge**, the **KV cache**, and even **page tables**.

_TODO: one-paragraph hook._

## 1. Two price curves

**Key points**

- The price of a **fixed** capability collapses; the cost of the **frontier** rises and is sold
  at a loss. "Prices will go up" is wrong; "the frontier is subsidised" is right.
- What drives the first curve is precisely the optimisations of this article: quantisation,
  mixtures of experts, KV-cache compression, i.e. fewer bytes moved. Falling prices are the
  empirical proof of the thesis.
- What is frontier today reaches "fixed capability" within a year or two, and then fits on a
  machine you own.
- Sovereignty, stated as facts: your prompts leave for someone else's machine, and someone else
  sets the price. That holds for any provider, whatever its country.

**Numbers**

- Price at fixed capability: **÷9 to ÷900 per year** depending on the threshold; GPT-4-level MMLU
  went from **$37.50 to $0.18** per million tokens between March 2023 and February 2025. With the
  authors' own caveat: the fastest drops are the most recent, so they may not persist.
- **÷5 to ÷10 per year** at a given benchmark level; algorithmic efficiency alone **~3× per
  year**; frontier model costs **rising 3× to 18× per year**.
- OpenAI projects **$278 B** of negative free cash flow over 2026–2030, for $36 B of revenue in
  2026 (FT, via Reuters).

**Figure:** _TODO: the two curves (check the licence of Epoch AI's data before reproducing it)._

**Sources:** Epoch AI 2025 · Gundlach et al. 2025 · FT via Reuters 2026.

## 2. Compute is free

**Key points**

- **The energy ladder:** an operation costs a few picojoules; a DRAM access costs one to two
  nanojoules, **100 to 200 times more**. The processor spends its life waiting for memory.
- **Decoding an LLM** reads every **active** weight to produce one token, for **two
  floating-point operations per weight read**. The arithmetic is negligible; the reading is not.
- **The formula anyone can apply:**

  $$
  \text{tokens/s}_{\max} = \frac{\text{memory bandwidth}}{\text{bytes of active weights}},
  \qquad \text{real} \approx 50 \text{ to } 75\,\%\ \text{of that ceiling}
  $$

- **Two regimes:** prefill (reading the prompt) is compute-bound; decoding is memory-bound.
  Section 7 comes back to this: providers split the two phases across machines.
- **The same law in our own code (measured):** a hidden 1 MB `memset` per chunk per frame (radix
  sort counters zeroed on the stack) made the physics step grow from 2.7 to 5.8 ms; fixed, it
  holds at 0.052–0.064 ms, a 50–100× gain. Not one operation fewer, only fewer bytes.
- Nuance for a single user: at batch 1, decoding is memory-bound without necessarily saturating
  the bandwidth.

**Try it at home:** _TODO: read your machine's bandwidth (spec **and** a STREAM-like measure),
take the size of two model files, predict tokens/s, then run `llama-bench -n 128` and compare.
Two models of very different sizes, or the law does not show._

**Figure:** _TODO: the energy ladder on a log scale; the roofline with prefill and decode._

**Sources:** Horowitz 2014 · *The Memory Bandwidth Ladder* · *Memory-Bound but Not
Bandwidth-Limited* (2026) · RooflineBench (2026) · LplPlugin wiki, *Performance Results*.

## 3. Fitting a big model

**Key points**

- **Capacity and speed are two different numbers.** What fits depends on *total* parameters;
  speed depends on *active* parameters. A dense model ties them together; a mixture of experts
  (MoE) separates them.
- **Quantisation** is the thesis applied directly: fewer bytes per weight, more tokens per second,
  at a quality cost that must be stated.
- **MoE:** Mixtral has 47 B parameters, 13 B active (2 experts out of 8); DeepSeek-V2 has 236 B,
  21 B active. The 236 B model takes 3.4× the memory of a dense 70 B and generates 3.3× faster
  (**derived**).
- **The KV cache is bytes too:** it grows with the context; DeepSeek-V2's multi-head latent
  attention cuts it by 93.3 %.
- **Read only what is used:** *LLM in a flash* runs models up to twice the size of DRAM.
- **Prefetching experts, and what the router actually knows:**
  - within a block, the router's answer is **exact but arrives at the last moment**: a Mixtral
    expert at 4-bit is ~88 MB, ~12 ms of NVMe (**derived**);
  - one block ahead requires a retrained gate (*Pre-gated MoE*) or a guess (~60–70 % recall);
  - one token ahead is exact only when routing hashes the token itself (*Hash Layers*), which
    neither Mixtral nor DeepSeek does;
  - so the design is hybrid: speculate one block ahead, let the router's exact answer cancel wrong
    prefetches, cache recent experts.
- **The Laplace idea:** the expert is a page. Map the whole model and let a page fault pull an
  expert straight off NVMe, inside the kernel's memory manager. An idea on the roadmap, not code.

### Kernel aside: paging, the OS idea AI reinvented

- *PagedAttention* (vLLM) pages the KV cache like virtual memory: near-zero waste, 2–4× throughput.
- *vAttention* goes the other way and lets the virtual-memory system do the paging: up to 1.23×
  more. The layer that knows how to page is the system's.
- **The hidden cost of an address:** big-memory workloads lose up to 10 % of their cycles to TLB
  misses even with large pages; a direct segment brings that under 0.5 %.
- **In LplKernel (measured):** a single address space and contiguous arenas point towards direct
  segments, but every mapping is still a 4 KiB page, since the boot code never sets CR4.PSE.

**Figure:** _TODO: dense vs MoE: one number vs two._

**Sources:** Jiang et al. 2024 (Mixtral) · DeepSeek-AI 2024 (DeepSeek-V2) · Dai et al. 2024
(DeepSeekMoE) · Shazeer et al. 2017 · Alizadeh et al. 2023 · Hwang et al. 2024 · Eliseev & Mazur
2023 · Roller et al. 2021 · Kwon et al. 2023 · Prabhu et al. 2024 · Basu et al. 2013 · Haas &
Leis 2023.

## 4. Knowledge without a database

**Key points**

- **A model only knows what is in its weights**; the rest must be fetched, and fetching lets a
  *small* model know a lot: RETRO matches GPT-3 with **25× fewer parameters** by retrieving from
  2 trillion tokens.
- **Stuffing the context costs and does not work well:** every context token grows the KV cache
  (section 3), and models use the middle of a long context badly. Precise retrieval is an
  optimisation twice over: fewer bytes, better answers.
- **The zoo, without a trial:** vector and hybrid search, GraphRAG, the *LLM Wiki* (whose author
  gives its own limit: ~100 sources, hundreds of pages), composite retrieval ("SQL to filter,
  vectors to rank").
- **The Laplace answer, knowledge baked into an image:** sorted sections, identifiers that travel
  verbatim, a structured filter first and resemblance second, read by memory-mapping instead of
  loading, by the same code on the host and in ring 0. Composite retrieval without a server.
- **Why it is cheap (measured):** 19.6 M catalogue records baked with 7 MB of RAM; a query over the
  3.71 GB image answers under a 512 MB memory limit that kills a 1 GiB allocation; anonymous memory
  stays at 220 KB during the scan.
- **The project indexes itself (measured):** 92 documents, 533 identifiers, 0 broken references;
  anything in it can be found without opening a document.
- **What is missing, stated plainly:** the resemblance half (no signature section in the format
  yet); the rung between the exact filter and the writing model (section 5); and writes, since a
  user's memory is written while a baked image is read.
- **State of the art on compact indexes:** DiskANN (a billion points on 64 GB of RAM and an SSD),
  LEANN (an index at ~5 % of the data), Faiss.

**Try it at home:** _TODO: `lpl-ask` on the project corpus, then on the catalogue under
`systemd-run --user -p MemoryMax=512M`, after showing that a 1 GiB allocation is killed there._

**Figure:** _TODO: stuffed context vs baked knowledge: what enters the KV cache in each case._

**Sources:** Borgeaud et al. 2021 (RETRO) · Liu et al. 2023 (*Lost in the Middle*) · Karpathy
2026 (LLM Wiki) · Subramanya et al. 2019 (DiskANN) · Wang et al. 2025 (LEANN) · Douze et al. 2024
(Faiss) · Roman, composite retrieval (practitioner video).

## 5. Reliability without size

**Key points**

- **Why the same prompt at temperature 0 gives two answers online:** not "concurrency plus
  floating point" alone, but the missing **batch invariance**: other users change the batch
  size, and reductions change order with it.
- **Floating point is not the villain on its own:** basic IEEE operations are exact; the culprits
  are fused multiply-add, x87's 80-bit registers, libm and reduction order. In-house story: a
  `tanf` in `Camera.o` made host and kernel disagree, replaced by CORDIC.
- **Two industry answers:** batch-invariant kernels, or a verifier that replays tokens and rolls
  back those that diverge.
- **The Laplace answer, by construction (measured):** an integer transformer running in ring 0
  that produces the same signature as the host. Tiny and untrained: it proves the arithmetic,
  not usefulness.
- **Reliability comes from the language, not the size (measured):** under a grammar, a model that
  learned nothing emits a valid tool call 8 times out of 8, and 0 out of 8 without.
- **Determinism is an optimisation:** cacheable, testable, replayable; no retries means fewer
  tokens means fewer bytes.

### Not speaking at all: the System 1 decider

- Generating T tokens reads the weights T times; a head that returns a choice, a score or a
  probability reads them once, so a decision emitted as ~20 tokens of JSON costs ~20 extra weight
  reads (**derived**).
- The industry has noticed: typed, non-autoregressive "System One" models return choices and
  probabilities with a confidence (**vendor**: 70–500 ms, hosted); a small classifier can choose
  the retrieval strategy; a router can choose between a weak and a strong model on a probability.
- The project already has System 1s without naming them: the MoE router, the satellite's wake
  word, its two-threshold voice detector.
- ⚠ A percentage means something only once calibrated, and modern networks are poorly
  calibrated. Measure first, then write thresholds.
- Teaser for another article: the same local, integer head could drive game NPCs, whose state is
  authoritative.

**Try it at home:** _TODO: the grammar test (with and without the grammar), then the QEMU boot
printing the same signature as the host._

**Sources:** He / Thinking Machines 2025 · Gond et al. 2026 (LLM-42) · TypeSafe AI 2026 (vendor)
· Jeong et al. 2024 (Adaptive-RAG) · Ong et al. 2024 (RouteLLM) · Kadavath et al. 2022 · Guo et al.
2017 · Yu et al. 2024 · LplPlugin wiki, *Performance Results* (CORDIC).

## 6. The energy of waiting

**Key points**

- **A local assistant spends almost its whole life waiting** for a wake word or a question; the
  energy that matters is the energy of waiting.
- **Sleeping is an algorithm:** `HLT` sleeps until the next interrupt; `MONITOR`/`MWAIT` wakes on
  a **memory write**, and re-reading after arming closes the race; the sleep-state hint is read
  from CPUID, never guessed.
- **Measure, do not promise (measured):** the satellite profile is awake a few thousandths of the
  time. But sleeping in C1 or C6 gives the same duty cycle for a different power draw. Say what
  the instrument cannot see.
- **Comparing with the cloud at an equal boundary:** a median Gemini text prompt costs 0.24 Wh,
  counting the host, idle machines and the data centre; energy per prompt fell 33× in twelve
  months. Configuration alone can save over 40 % without changing what is computed.

### Kernel aside: the tick, the APIC and the costly IPI

- The periodic tick wakes a core a thousand times a second, work or not; a one-shot APIC timer
  says "wake me at this instant" instead: that is the tickless kernel.
- ⚠ And tickless is refused while a world runs: stopping the tick would make the simulation
  advance at the pace of the machine's idleness, breaking determinism. Energy and reproducibility
  meet on a single register.
- **An IPI costs, and can block:** TLBs are not kept coherent across cores, so unmapping a page
  interrupts the others and waits for their answer.
- **In-house story (measured):** a boot test marked a phantom processor online; the first page
  unmapping broadcast a TLB-shootdown IPI and waited forever. Fix: a bounded wait and a counter.
- Waking on a memory write is precisely *not needing* an IPI to wake a core.

**Try it at home:** _TODO: a wall wattmeter: idle for five minutes with the model loaded, then a
long generation; joules per token = (P_gen − P_idle) × duration ÷ tokens, reported per request and
per token, with P_idle published too._

**Sources:** Elsworth et al. 2025 (Google) · Chung et al. 2025 (ML.ENERGY) · Samsi et al. 2023 ·
Amit, Tai & Wei 2020 · Intel SDM, `MWAIT`.

## 7. What the cloud does with its bytes

**Key points**

- Prefill and decoding do not share a bottleneck (section 2), so providers serve them on
  **different machines**.
- Splitting the phases forces the **KV cache to travel** between machines, over RDMA, at
  800 Gbit/s.
- A large provider's serving architecture is organised around moving one object, the KV cache.
  **A local machine that does both phases in one place never pays that trip.**
- Honest caveat: the split pays off *because* thousands of users are served at once; locally the
  advantage is simplicity, not raw speed.

**Numbers:** phase splitting gives 1.4× throughput at 20 % lower cost, or 2.35× at equal cost;
disaggregation serves 7.4× more requests within latency targets; a KV-cache-centric design moves
the cache by GPUDirect RDMA and handles 75 % more requests on real workloads.

**Figure:** _TODO: the KV cache's trip: cloud (prefill → RDMA → decode) vs local (one machine)._

**Sources:** Patel et al. 2024 (Splitwise) · Zhong et al. 2024 (DistServe) · Qin et al. 2024
(Mooncake).

## 8. Everything is connected

**Key points**

- One law, four objects: moving **weights**, **knowledge**, a **KV cache** or a **page table**.
- The four repositories carry the same law: LplKernel (the layer that pages, sleeps and drives
  DMA), LplPlugin (the deterministic engine), LplAssistant (the local model, its grammar, its
  memory), LplKnowledge (baked knowledge, readable in ring 0).
- For those who fear being replaced: what AI does not replace is knowing what happens under the
  model, the layer this article just showed.
- For those who trust too much: a local model under a grammar, citing baked knowledge, measured
  and reproducible, is an AI you can check.
- Call to action: compute your machine's tokens per second tonight, and look at what it draws
  while idle.

**Figure:** _TODO: the four repositories and the law that runs through them._

## Limits and open questions

- A local model is weaker than a frontier model, and it should be said first.
- The ring-0 transformer is tiny and untrained: it proves arithmetic, not usefulness.
- There is no joule measurement in the repositories yet.
- Every **derived** figure above stays derived until measured on real hardware.
- LplKnowledge does half of what retrieval needs: the structured filter and memory-mapped reads
  work; resemblance and writes do not yet.

## Measurements to take before publishing

- [ ] Predicted vs measured tokens/s on two models (section 2).
- [ ] Determinism of `llama.cpp` at temperature 0: 50 runs, distinct outputs counted, varying the
      thread count (section 5).
- [ ] Joules per token at the wall, idle subtracted and published (section 6).
- [ ] Later, once the resemblance section exists: a baked image vs pgvector on the same memories:
      latency, resident memory of both processes, energy (section 4).

## References

**Physics and economics**

- M. Horowitz, [*Computing's Energy Problem (and what we can do about it)*](https://gwern.net/doc/cs/hardware/2014-horowitz-2.pdf), ISSCC 2014.
- B. Cottier et al., [*LLM inference prices have fallen rapidly but unequally across tasks*](https://epoch.ai/data-insights/llm-inference-price-trends), Epoch AI, 2025.
- H. Gundlach et al., [*The Price of Progress: Price Performance and the Future of AI*](https://arxiv.org/abs/2511.23455), arXiv 2511.23455.
- Reuters, reporting the Financial Times, [*OpenAI forecasts cash burn near $280 billion by 2030*](https://gvwire.com/2026/09/19/openai-forecasts-cash-burn-near-280-billion-by-2030-ft-reports/), 2026 (republished by GV Wire).
- [*The Memory Bandwidth Ladder: What Actually Decides How Fast Your LLM Runs*](https://mlechner.substack.com/p/the-memory-bandwidth-ladder-what) (blog post).
- [*Memory-Bound but Not Bandwidth-Limited*](https://arxiv.org/pdf/2605.30571), arXiv 2605.30571. _TODO: read before citing._
- [*RooflineBench*](https://arxiv.org/pdf/2602.11506), arXiv 2602.11506. _TODO: read before citing._

**Models, experts and offloading**

- N. Shazeer et al., [*Outrageously Large Neural Networks: The Sparsely-Gated Mixture-of-Experts Layer*](https://arxiv.org/abs/1701.06538), 2017.
- A. Q. Jiang et al., [*Mixtral of Experts*](https://arxiv.org/abs/2401.04088), 2024.
- D. Dai et al., [*DeepSeekMoE*](https://arxiv.org/abs/2401.06066), 2024.
- DeepSeek-AI, [*DeepSeek-V2*](https://arxiv.org/abs/2405.04434), 2024.
- K. Alizadeh et al., [*LLM in a flash*](https://arxiv.org/abs/2312.11514), ACL 2024.
- R. Hwang et al., [*Pre-gated MoE*](https://arxiv.org/abs/2308.12066), ISCA 2024.
- A. Eliseev, D. Mazur, [*Fast Inference of Mixture-of-Experts Language Models with Offloading*](https://arxiv.org/abs/2312.17238), 2023.
- S. Roller et al., [*Hash Layers For Large Sparse Models*](https://arxiv.org/abs/2106.04426), 2021.

**Paging, the TLB and storage**

- W. Kwon et al., [*Efficient Memory Management for Large Language Model Serving with PagedAttention*](https://arxiv.org/abs/2309.06180), SOSP 2023.
- R. Prabhu et al., [*vAttention*](https://arxiv.org/abs/2405.04437), ASPLOS 2025.
- A. Basu et al., [*Efficient Virtual Memory for Big Memory Servers*](https://research.cs.wisc.edu/multifacet/papers/isca13_direct_segment.pdf), ISCA 2013.
- N. Amit, A. Tai, M. Wei, [*Don't shoot down TLB shootdowns!*](https://nadav.amit.zone/publications/pdfs/amit2020tlb.pdf), EuroSys 2020.
- G. Haas, V. Leis, [*What Modern NVMe Storage Can Do, And How To Exploit It*](https://www.vldb.org/pvldb/vol16/p2090-haas.pdf), PVLDB 16(9), 2023.

**Knowledge and retrieval**

- S. Borgeaud et al., [*Improving language models by retrieving from trillions of tokens*](https://arxiv.org/abs/2112.04426) (RETRO), 2021.
- N. F. Liu et al., [*Lost in the Middle: How Language Models Use Long Contexts*](https://arxiv.org/abs/2307.03172), TACL.
- A. Karpathy, [*llm-wiki*](https://gist.github.com/karpathy/442a6bf555914893e9891c11519de94f) (gist), 2026.
- S. Jayaram Subramanya et al., [*DiskANN*](https://papers.nips.cc/paper/2019/hash/09853c7fb1d3f8ee67a61b6bf4a7f8e6-Abstract.html), NeurIPS 2019.
- Y. Wang et al., [*LEANN: A Low-Storage Vector Index*](https://arxiv.org/abs/2506.08276), 2025.
- M. Douze et al., [*The Faiss library*](https://arxiv.org/abs/2401.08281), 2024.
- J. Roman, [*Le futur du RAG n'est pas le GraphRAG ou les Seconds Cerveaux*](https://www.youtube.com/watch?v=fA63mhCq6BI) (practitioner video, in French).

**Determinism and deciding without speaking**

- H. He / Thinking Machines, [*Defeating Nondeterminism in LLM Inference*](https://thinkingmachines.ai/blog/defeating-nondeterminism-in-llm-inference/), 2025.
- R. Gond et al., [*LLM-42: Enabling Determinism in LLM Inference with Verified Speculation*](https://arxiv.org/abs/2601.17768), 2026.
- TypeSafe AI, [*Introducing System One Models & Jev*](https://typesafe.ai/blog/introducing-system-one-models-and-jev), 2026 (vendor).
- S. Jeong et al., [*Adaptive-RAG*](https://arxiv.org/abs/2403.14403), NAACL 2024.
- I. Ong et al., [*RouteLLM*](https://arxiv.org/abs/2406.18665), 2024.
- S. Kadavath et al., [*Language Models (Mostly) Know What They Know*](https://arxiv.org/abs/2207.05221), 2022.
- C. Guo et al., [*On Calibration of Modern Neural Networks*](https://arxiv.org/abs/1706.04599), ICML 2017.
- P. Yu et al., [*Distilling System 2 into System 1*](https://arxiv.org/abs/2407.06023), 2024.

**Energy**

- C. Elsworth et al., [*Measuring the environmental impact of delivering AI at Google Scale*](https://arxiv.org/abs/2508.15734), 2025.
- J.-W. Chung et al., [*The ML.ENERGY Benchmark*](https://arxiv.org/abs/2505.06371), 2025.
- S. Samsi et al., [*From Words to Watts*](https://arxiv.org/abs/2310.03003), 2023.
- Intel, [*MWAIT: Monitor Wait*](https://www.felixcloutier.com/x86/mwait) (SDM excerpt).

**Serving at scale**

- P. Patel et al., [*Splitwise: Efficient generative LLM inference using phase splitting*](https://arxiv.org/abs/2311.18677), ISCA 2024.
- Y. Zhong et al., [*DistServe*](https://arxiv.org/abs/2401.09670), OSDI 2024.
- R. Qin et al., [*Mooncake: A KVCache-centric Disaggregated Architecture for LLM Serving*](https://arxiv.org/abs/2407.00079), 2024.

**This project**

- [LplKernel](https://github.com/MasterLaplace/LplKernel) · [LplPlugin](https://github.com/MasterLaplace/LplPlugin) · [LplAssistant](https://github.com/MasterLaplace/LplAssistant) · [LplKnowledge](https://github.com/MasterLaplace/LplKnowledge)
- [LplPlugin wiki: Performance Results](https://github.com/MasterLaplace/LplPlugin/wiki/Performance-Results)
