const PROJECT_BY_REPOSITORY = Object.freeze({
  "MasterLaplace/LplKernel": "kernel",
  "MasterLaplace/LplPlugin": "plugin",
  "Christian-guajardo/LplAssistant": "assistant",
  "MasterLaplace/LplKnowledge": "knowledge",
  "MasterLaplace/.github": "infra",
  "MasterLaplace/LplCraftSkills": "forge",
});

const PROJECT_BY_TRACK_KEY_LETTER = Object.freeze({
  k: "kernel",
  c: "convergence",
  p: "plugin",
  a: "assistant",
  n: "knowledge",
  d: "docs",
  i: "infra",
  x: "forge",
});

const TRACK_READING_ORDER = Object.freeze([
  ...["T-k-foundations", "kernel-p5", "T-k-platform", "kernel-p6", "T-k-advanced", "T-k-hardening", "T-k-tooling", "T-k-datamove"],
  ...["T-c-modelb", "T-c-convergence", "T-c-gates-plugin", "T-c-gates-assistant", "T-c-gates-knowledge"],
  ...["T-p-foundations", "plugin-p3", "bci-phase4", "plugin-p5", "T-p-immersion", "T-p-render", "T-p-animation"],
  ...["T-p-worlds", "T-p-director", "T-p-editor", "T-p-tooling"],
  ...["T-a-jarvis", "T-a-research", "T-a-voice", "T-a-caine"],
  ...["T-n-alexandrie", "T-n-history", "T-n-archive"],
  ...["T-d-book", "T-d-docs", "T-d-papers"],
  ...["T-i-infra", "T-x-forge"],
]);

const ROADMAP_MARKER = /<!--\s*roadmap:(\S+?)\s*-->/;
const TRACK_KEY_LETTER = /^T-([a-z])-/;

export const ROADMAP_REPOSITORIES = Object.freeze(Object.keys(PROJECT_BY_REPOSITORY));

export function roadmapFromIssues(issues) {
  const issueById = new Map(issues.map((issue) => [issue.id, issue]));
  const tracks = issues.filter((issue) => issue.labels.includes("track"));
  if (tracks.length === 0) {
    throw new Error(`no issue carries the label "track" among the ${issues.length} issues read`);
  }
  const liveTracks = tracks.filter((track) => !isAbandoned(track)).sort(byReadingOrder);

  const shownItems = liveTracks.flatMap((track) => {
    const project = projectOfTrack(track);
    return subIssuesOf(track, issueById)
      .filter((issue) => !isAbandoned(issue))
      .map((issue) => ({ issue, project, track: track.title, steps: descendantsOf(issue, issueById) }));
  });

  const shownItemIdByIssueId = new Map();
  for (const { issue, steps } of shownItems) {
    for (const issueOrStep of [issue, ...steps]) shownItemIdByIssueId.set(issueOrStep.id, itemIdOf(issue));
  }

  return shownItems.map((shownItem) => roadmapItemOf(shownItem, shownItemIdByIssueId));
}

function roadmapItemOf({ issue, project, track, steps }, shownItemIdByIssueId) {
  const id = itemIdOf(issue);
  const status = statusOf(issue, steps);
  const dependsOn = [
    ...new Set(issue.blockedBy.map((blockerId) => shownItemIdByIssueId.get(blockerId))),
  ].filter((blocker) => blocker !== undefined && blocker !== id);

  return {
    id,
    title: issue.title,
    project,
    track,
    status,
    progress: status === "done" ? undefined : progressOf(steps),
    detail: firstParagraphUnder("## What is asked", issue.body),
    dependsOn: dependsOn.length > 0 ? dependsOn : undefined,
    url: issue.url,
  };
}

function projectOfTrack(track) {
  const keyLetter = TRACK_KEY_LETTER.exec(roadmapKeyOf(track) ?? "")?.[1];
  const project = keyLetter ? PROJECT_BY_TRACK_KEY_LETTER[keyLetter] : PROJECT_BY_REPOSITORY[track.repository];
  if (project) return project;
  throw new Error(
    `the track "${track.title}" (${track.url}) belongs to no project: ` +
      `its key has no known T-<letter>- prefix and its repository is not one the roadmap reads`,
  );
}

function byReadingOrder(first, second) {
  return readingRankOf(first) - readingRankOf(second) || first.number - second.number;
}

function readingRankOf(track) {
  const rank = TRACK_READING_ORDER.indexOf(roadmapKeyOf(track));
  return rank === -1 ? TRACK_READING_ORDER.length : rank;
}

function statusOf(issue, steps) {
  if (isDone(issue)) return "done";
  if (isIdea(issue)) return "idea";
  if (steps.some(isDone) || issue.openPullRequests > 0) return "in-progress";
  return "planned";
}

function progressOf(steps) {
  const counted = steps.filter((step) => isDone(step) || (step.state === "OPEN" && !isIdea(step)));
  if (counted.length === 0) return undefined;
  return Math.round((100 * counted.filter(isDone).length) / counted.length);
}

function itemIdOf(issue) {
  return roadmapKeyOf(issue) ?? `${issue.repository.split("/")[1]}-${issue.number}`;
}

function roadmapKeyOf(issue) {
  return ROADMAP_MARKER.exec(issue.body)?.[1];
}

function subIssuesOf(issue, issueById) {
  return issue.subIssues.map((id) => issueById.get(id)).filter((subIssue) => subIssue !== undefined);
}

function descendantsOf(issue, issueById) {
  return subIssuesOf(issue, issueById).flatMap((subIssue) => [subIssue, ...descendantsOf(subIssue, issueById)]);
}

function firstParagraphUnder(heading, markdown) {
  const lines = markdown.split(/\r?\n/).map((line) => line.trim());
  const headingIndex = lines.indexOf(heading);
  if (headingIndex === -1) return undefined;

  const paragraph = [];
  for (const line of lines.slice(headingIndex + 1)) {
    if (line !== "") paragraph.push(line);
    else if (paragraph.length > 0) break;
  }
  return paragraph.length > 0 ? paragraph.join(" ") : undefined;
}

function isDone(issue) {
  return issue.state === "CLOSED" && (issue.stateReason === "COMPLETED" || issue.stateReason === null);
}

function isAbandoned(issue) {
  return issue.state === "CLOSED" && !isDone(issue);
}

function isIdea(issue) {
  return issue.labels.includes("idea");
}
