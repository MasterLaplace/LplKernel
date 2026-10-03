import { test } from "node:test";
import assert from "node:assert/strict";
import { roadmapFromIssues } from "./roadmap-from-issues.mjs";

function issue(repository, number, fields = {}) {
  return {
    id: `I_${repository.split("/")[1]}_${number}`,
    repository,
    number,
    url: `https://github.com/${repository}/issues/${number}`,
    title: `Issue ${number}`,
    body: "",
    state: "OPEN",
    stateReason: null,
    labels: [],
    subIssues: [],
    blockedBy: [],
    openPullRequests: 0,
    ...fields,
  };
}

function body(asked, key) {
  return `## What is asked\n\n${asked}\n\nWhy it matters: a reason.\n\n## What is true today\n\nNothing yet.\n\n<!-- roadmap:${key} -->\n`;
}

const KERNEL = "MasterLaplace/LplKernel";
const PLUGIN = "MasterLaplace/LplPlugin";
const done = { state: "CLOSED", stateReason: "COMPLETED" };
const abandoned = { state: "CLOSED", stateReason: "NOT_PLANNED" };
const duplicate = { state: "CLOSED", stateReason: "DUPLICATE" };
const closedWithoutReason = { state: "CLOSED", stateReason: null };
const id = (repository, number) => `I_${repository.split("/")[1]}_${number}`;

const issues = [
  issue(KERNEL, 1, {
    title: "Kernel foundations",
    body: body("Everything the kernel stands on.", "T-k-foundations"),
    labels: ["track"],
    subIssues: [id(KERNEL, 2), id(KERNEL, 3), id(KERNEL, 8), id(KERNEL, 9), id(KERNEL, 10), id(KERNEL, 11), id(KERNEL, 15)],
  }),
  issue(KERNEL, 2, {
    title: "Bare-bones kernel",
    body: body("Boot with GRUB.", "kernel-p1"),
    subIssues: [id(KERNEL, 14)],
    ...done,
  }),
  issue(KERNEL, 14, { title: "A follow-up left open" }),
  issue(KERNEL, 11, { title: "Duplicate item", body: body("Said twice.", "kernel-duplicate"), ...duplicate }),
  issue(KERNEL, 15, { title: "Closed long ago", body: body("Before reasons.", "kernel-legacy"), ...closedWithoutReason }),
  issue(KERNEL, 3, {
    title: "Memory management",
    body: body("Paging and a heap.\nThe paragraph wraps on a second line.", "kernel-p4"),
    subIssues: [id(KERNEL, 4), id(KERNEL, 5), id(KERNEL, 6), id(KERNEL, 7)],
  }),
  issue(KERNEL, 4, { title: "Step done", ...done, subIssues: [id(KERNEL, 13)] }),
  issue(KERNEL, 5, { title: "Step open" }),
  issue(KERNEL, 6, { title: "Step idea", labels: ["idea"] }),
  issue(KERNEL, 7, { title: "Step abandoned", ...abandoned }),
  issue(KERNEL, 13, { title: "Step below a step", ...done }),
  issue(KERNEL, 8, { title: "A kernel idea", body: body("Maybe one day.", "kernel-idea"), labels: ["idea"] }),
  issue(KERNEL, 9, { title: "Abandoned item", body: body("No longer.", "kernel-dropped"), ...abandoned }),
  issue(KERNEL, 10, {
    title: "Scheduler",
    body: body("Run tasks.", "kernel-p6"),
    openPullRequests: 1,
    blockedBy: [id(KERNEL, 4), id(KERNEL, 3), id(KERNEL, 2), id(KERNEL, 1), id(KERNEL, 9), id(KERNEL, 10)],
  }),
  issue(PLUGIN, 1, {
    title: "Brain-computer interface",
    body: body("Read the brain.", "bci-phase4"),
    labels: ["track"],
    subIssues: [id(KERNEL, 12)],
  }),
  issue(KERNEL, 12, { title: "BCI input in ring 0", blockedBy: [id(KERNEL, 5)] }),
  issue(KERNEL, 20, {
    title: "Gates for the engine",
    body: body("Every gate.", "T-c-gates"),
    labels: ["track"],
    subIssues: [id(KERNEL, 21)],
  }),
  issue(KERNEL, 21, { title: "Gate one", body: body("The first gate.", "gate-one") }),
  issue(KERNEL, 18, {
    title: "New kernel track",
    body: body("Not ranked yet.", "T-k-new"),
    labels: ["track"],
    subIssues: [id(KERNEL, 19)],
  }),
  issue(KERNEL, 19, { title: "New work", body: body("Something new.", "new-work") }),
  issue(KERNEL, 45, {
    title: "Platform",
    body: body("ACPI and timers.", "T-k-platform"),
    labels: ["track"],
    subIssues: [id(KERNEL, 46)],
  }),
  issue(KERNEL, 46, { title: "ACPI tables", body: body("Walk the tables.", "platform-acpi") }),
  issue(KERNEL, 50, {
    title: "Device drivers",
    body: body("Native drivers.", "kernel-p5"),
    labels: ["track"],
    subIssues: [id(KERNEL, 51)],
  }),
  issue(KERNEL, 51, { title: "NVMe driver", body: body("Store bytes.", "drivers-nvme") }),
  issue(KERNEL, 30, { title: "A bug outside the roadmap", labels: ["type:bug"] }),
  issue(KERNEL, 40, {
    title: "Ring-3 user space",
    body: body("Processes in ring 3.", "T-k-ring3"),
    labels: ["track"],
    subIssues: [id(KERNEL, 41)],
    ...abandoned,
  }),
  issue(KERNEL, 41, { title: "System calls", body: body("A syscall table.", "kernel-syscalls") }),
];

const roadmap = roadmapFromIssues(issues);
const item = (itemId) => roadmap.find((it) => it.id === itemId);

test("the direct sub-issues of a track are its items, in the track's order", () => {
  assert.deepEqual(
    roadmap.filter((it) => it.track === "Kernel foundations").map((it) => it.id),
    ["kernel-p1", "kernel-p4", "kernel-idea", "kernel-p6", "kernel-legacy"],
  );
});

test("tracks follow the reading order the mapping lists, and unlisted tracks come after, by issue number", () => {
  assert.deepEqual(
    [...new Set(roadmap.map((it) => it.track))],
    ["Kernel foundations", "Device drivers", "Platform", "Brain-computer interface", "New kernel track", "Gates for the engine"],
  );
});

test("an item carries its track's title, its issue's title and its issue's address", () => {
  assert.equal(item("kernel-p4").track, "Kernel foundations");
  assert.equal(item("kernel-p4").title, "Memory management");
  assert.equal(item("kernel-p4").url, "https://github.com/MasterLaplace/LplKernel/issues/3");
});

test("an issue without a roadmap marker is named after its repository and number", () => {
  assert.equal(item("LplKernel-12").title, "BCI input in ring 0");
});

test("the project comes from the letter in the track's key, else from the track's repository", () => {
  assert.equal(item("kernel-p1").project, "kernel");
  assert.equal(item("gate-one").project, "convergence");
  assert.equal(item("LplKernel-12").project, "plugin");
});

test("closed as completed is done, an open idea is an idea, an open issue is planned", () => {
  assert.equal(item("kernel-p1").status, "done");
  assert.equal(item("kernel-idea").status, "idea");
  assert.equal(item("gate-one").status, "planned");
});

test("a closed issue without a reason, from before GitHub recorded one, is done", () => {
  assert.equal(item("kernel-legacy").status, "done");
});

test("an item closed as duplicate is left out", () => {
  const foundations = roadmap.filter((it) => it.track === "Kernel foundations").map((it) => it.id);
  assert.ok(foundations.includes("kernel-legacy"));
  assert.ok(!foundations.includes("kernel-duplicate"));
});

test("an open issue with a done step, or with an open pull request, is in progress", () => {
  assert.equal(item("kernel-p4").status, "in-progress");
  assert.equal(item("kernel-p6").status, "in-progress");
});

test("an item closed as not planned is left out, and its siblings stay", () => {
  const foundations = roadmap.filter((it) => it.track === "Kernel foundations").map((it) => it.id);
  assert.ok(foundations.includes("kernel-p6"));
  assert.ok(!foundations.includes("kernel-dropped"));
});

test("a track closed as not planned is left out with its items", () => {
  assert.equal(item("kernel-syscalls"), undefined);
  assert.ok(!roadmap.some((it) => it.track === "Ring-3 user space"));
});

test("progress counts every step below an item, leaving out ideas and abandoned steps", () => {
  assert.equal(item("kernel-p4").progress, 67);
});

test("a done item shows no progress, even with sub-issues still open", () => {
  assert.equal(item("kernel-p1").progress, undefined);
});

test("a blocker becomes the item that shows it, never the item itself, a track or an abandoned item", () => {
  assert.deepEqual(item("kernel-p6").dependsOn, ["kernel-p4", "kernel-p1"]);
  assert.deepEqual(item("LplKernel-12").dependsOn, ["kernel-p4"]);
  assert.equal(item("gate-one").dependsOn, undefined);
});

test("the detail is the first paragraph under What is asked", () => {
  assert.equal(item("kernel-p1").detail, "Boot with GRUB.");
  assert.equal(item("kernel-p4").detail, "Paging and a heap. The paragraph wraps on a second line.");
  assert.equal(item("LplKernel-12").detail, undefined);
});

test("a track that belongs to no project is refused, and named", () => {
  const stray = issue("MasterLaplace/LplVesuvius", 1, { title: "Somewhere else", labels: ["track"] });
  assert.throws(() => roadmapFromIssues([stray]), /Somewhere else.*LplVesuvius\/issues\/1/);
});

test("a set of issues without any track is refused rather than shown as an empty roadmap", () => {
  assert.throws(() => roadmapFromIssues([issue(KERNEL, 30)]), /no issue carries the label "track"/);
});
