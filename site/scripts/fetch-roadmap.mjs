import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ROADMAP_REPOSITORIES, roadmapFromIssues } from "./roadmap-from-issues.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const OUT = join(here, "..", "src", "generated", "roadmap-from-issues.json");
const GITHUB_GRAPHQL = "https://api.github.com/graphql";
const REQUEST_TIMEOUT_MS = 30_000;

const ISSUES_QUERY = `
  query ($owner: String!, $name: String!, $after: String) {
    repository(owner: $owner, name: $name) {
      issues(first: 100, after: $after, orderBy: { field: CREATED_AT, direction: ASC }) {
        pageInfo { hasNextPage endCursor }
        nodes {
          id number url title body state stateReason
          labels(first: 20) { totalCount nodes { name } }
          subIssues(first: 100) { totalCount nodes { id url repository { nameWithOwner } } }
          blockedBy(first: 100) { totalCount nodes { id } }
          closedByPullRequestsReferences(first: 20, includeClosedPrs: false) { nodes { state } }
        }
      }
    }
  }
`;

try {
  const token = process.env.GITHUB_TOKEN || process.env.GH_TOKEN;
  if (!token) throw new Error("neither GITHUB_TOKEN nor GH_TOKEN is set");

  const issues = await issuesOfEveryRepository(token);
  const items = roadmapFromIssues(issues);
  write({ from: "issues", readAt: new Date().toISOString(), items });
  console.log(`[roadmap] ${items.length} items from ${issues.length} issues of ${ROADMAP_REPOSITORIES.length} repositories -> ${OUT}`);
} catch (error) {
  write({ from: "fallback", reason: error.message });
  warn(`the roadmap could not be built from the issues, so the page shows the hand-written fallback (src/data/roadmap.ts): ${error.message}`);
}

async function issuesOfEveryRepository(token) {
  const readings = await Promise.allSettled(ROADMAP_REPOSITORIES.map((repository) => issuesOf(repository, token)));
  const failures = readings.filter((reading) => reading.status === "rejected").map((reading) => reading.reason.message);
  if (failures.length > 0) throw new Error(failures.join("; "));
  return readings.flatMap((reading) => reading.value);
}

async function issuesOf(repository, token) {
  const issues = [];
  let after = null;
  do {
    const page = await issuesPage(repository, after, token);
    page.nodes.forEach(warnAboutSubIssuesTheRoadmapDoesNotRead);
    issues.push(...page.nodes.map((node) => issueFromNode(node, repository)));
    after = page.pageInfo.hasNextPage ? page.pageInfo.endCursor : null;
  } while (after !== null);
  return issues;
}

async function issuesPage(repository, after, token) {
  const [owner, name] = repository.split("/");
  const response = await fetch(GITHUB_GRAPHQL, {
    method: "POST",
    headers: {
      authorization: `bearer ${token}`,
      "content-type": "application/json",
      "user-agent": "laplace-portfolio-roadmap",
    },
    body: JSON.stringify({ query: ISSUES_QUERY, variables: { owner, name, after } }),
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  }).catch((error) => {
    throw new Error(`no answer from GitHub for ${repository}: ${error.cause?.message ?? error.message}`);
  });

  if (!response.ok) {
    const { message = response.statusText } = await response.json().catch(() => ({}));
    throw new Error(`GitHub answered ${response.status} (${message}) for ${repository}`);
  }
  const answer = await response.json();
  if (answer.errors) {
    throw new Error(`GitHub refused the query for ${repository}: ${answer.errors.map((error) => error.message).join("; ")}`);
  }
  return answer.data.repository.issues;
}

function issueFromNode(node, repository) {
  return {
    id: node.id,
    repository,
    number: node.number,
    url: node.url,
    title: node.title,
    body: node.body,
    state: node.state,
    stateReason: node.stateReason,
    labels: everyNode(node, "labels").map((label) => label.name),
    subIssues: everyNode(node, "subIssues").map((subIssue) => subIssue.id),
    blockedBy: everyNode(node, "blockedBy").map((blocker) => blocker.id),
    openPullRequests: node.closedByPullRequestsReferences.nodes.filter((pullRequest) => pullRequest.state === "OPEN").length,
  };
}

function warnAboutSubIssuesTheRoadmapDoesNotRead(node) {
  for (const subIssue of node.subIssues.nodes) {
    const repository = subIssue.repository.nameWithOwner;
    if (!ROADMAP_REPOSITORIES.includes(repository)) {
      warn(`${node.url} has a sub-issue in ${repository}, a repository the roadmap does not read: ${subIssue.url}`);
    }
  }
}

function everyNode(issueNode, connectionName) {
  const connection = issueNode[connectionName];
  if (connection.totalCount > connection.nodes.length) {
    throw new Error(
      `${issueNode.url} has ${connection.totalCount} ${connectionName}, more than the ${connection.nodes.length} ` +
        `the query reads at once: raise that page size in scripts/fetch-roadmap.mjs`,
    );
  }
  return connection.nodes;
}

function write(roadmap) {
  mkdirSync(dirname(OUT), { recursive: true });
  writeFileSync(OUT, `${JSON.stringify(roadmap, null, 2)}\n`, "utf8");
}

function warn(message) {
  console.log(process.env.GITHUB_ACTIONS === "true" ? `::warning title=Roadmap::${message}` : `[roadmap] WARNING: ${message}`);
}
