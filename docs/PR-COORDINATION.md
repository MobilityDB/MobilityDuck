# PR coordination policy

Before changing any source file in MobilityDuck, **check the open PR list first**.
The same applies across the ecosystem (MobilityDB, JMEOS, PyMEOS, MobilitySpark,
MEOS-API, MobilityDB-Deck, etc.) when a change in one repo could land before
related changes in a sibling.

## The rule

```
gh -R MobilityDB/MobilityDuck pr list --state open --limit 30
```

Read the titles. Open the diff of any PR whose title touches your area. Read
the body for policy decisions. Only then make a code change.

## Why

Two recent failure modes that this policy prevents:

1. **Duplicated work.** Several `consolidate/*` parity PRs were open
   (`#97`, `#98`, `#99`, `#100`, `#102`, `#103`, `#104`) covering temporal-ops,
   tspatial-predicates, tgeompoint-ops, analytics, tile/bin, aggregates, and
   the geo type triple. A parallel parity stream pushed equivalent commits
   directly to `main`, creating a 6-commit overlap that has to be untangled
   before either side can merge.

2. **Reverted policies reintroduced.** PR #111 (`fix/per-thread-meos-init`)
   moved MEOS init out of `LoadInternal` and onto a per-thread guard,
   *and* established the project policy of timezone-neutral test assertions
   (`stbox_eq()`, `=` round-trips, `asText(...)` comparisons). A separate
   stream simultaneously committed `fix(tz): single-timezone model — extension
   forces both MEOS and DuckDB to Europe/Brussels` to `LoadInternal` —
   exactly the policy PR #111 was reverting. The two will conflict at merge,
   and any test files pinned to `+01` need rewriting either way.

The PR queue carries the project's current direction. The cost of skimming it
is a few seconds; the cost of a merge conflict on a 50-file consolidation PR
is hours.

## How to apply

1. **Before any commit**:
   - `gh pr list --state open` in the affected repo.
   - For PRs whose titles touch your area, run `gh pr view <num> --json title,body,files --jq '{title, body, files: [.files[].path]}'`.
   - If your change duplicates the surface or conflicts with a policy
     decision: stop, comment on the PR or coordinate, do not push.

2. **Before pushing new commits to `main`**: confirm none of the open
   `consolidate/*` PRs cover the same surface. Treat the consolidation
   branches as pending merges.

3. **Before adding a new policy** (init order, threading, error handling,
   timezone, type-rename status): grep open PRs for the same area. If a PR
   is changing the policy you're about to set, don't.

4. **When parallel sessions are mentioned**: assume one is currently running
   in the same checkout. Use `git worktree list`, `git status`,
   `gh pr list` to see what they're touching. Don't push to `main` of an
   org repo while a parallel session is doing the same.

5. **For follow-up commits to a PR you've already opened**: also check
   whether other open PRs would conflict with your follow-up — the original
   PR's existence doesn't immunize follow-ups.

## What this policy is NOT

- It is **not** "ask the user before every commit." Local exploratory work
  that you don't push is fine.
- It is **not** "wait for all PRs to merge." Independent work that doesn't
  touch the same files or policies is fair game.
- The trigger is **file-level overlap** with open PRs, plus **policy-level
  overlap** (init order, error handling, type-rename status, test patterns).

## Cross-ecosystem variant

The same rule applies across the ecosystem when a change in one repo could
land before related changes in a sibling repo:

- MobilityDB MEOS API change → check JMEOS / PyMEOS / MobilityDuck /
  MEOS-API for in-flight syncs.
- MobilityDuck binding addition → check MobilitySpark / MobilityPySpark for
  parity expectations.
- Rename or removal in MEOS C → check all binding repos' open PRs for
  cherry-picks of the rename.

The "Cross-platform uniformity" policy covers the *post-merge* obligation
(update all bindings before removing a name); this policy covers the
*pre-commit* obligation (don't start work that an open PR has already
covered or is reversing).

## One PR = one commit = one feature

Two complementary obligations apply to every PR:

1. **Minimise PR count.** Before opening a new PR, check open PRs
   (`gh pr list`) and consolidate the new work into an existing PR if
   the surface is topic-coherent. Five small PRs that add binding
   registrations for the same family of MEOS functions should be one PR,
   not five.

2. **Squash each PR to a single commit before review.** The squashed
   message becomes the merge commit message, so write it carefully:
   subject = the PR title's "what changed", body = rationale and
   reviewer-facing notes.

### Squash recipe

```sh
git checkout <branch>
tree=$(git write-tree)
parent=$(git merge-base HEAD origin/main)
msg=$(cat <<'EOF'
<subject — what changed>

<body — why, and reviewer-facing notes>
EOF
)
newhash=$(git commit-tree -p $parent -m "$msg" $tree)
git reset --hard $newhash
git push --force-with-lease origin <branch>
```

This preserves authorship metadata (every commit's author stays as the
original author) while collapsing the history.

### Why

Reviewer cost is the dominant cost. One consolidated PR with one commit
means the maintainer reads one diff, interprets one CI run, makes one
merge decision. Three small PRs with three commits each multiplies that
by nine. Single-commit PRs also make `git revert` precise (one commit =
one feature = one revert) and eliminate ordering ambiguity inside the
PR's history.

### Exceptions

- Branches already exceeding 20 commits — merging as-is is cheaper than
  squashing.
- Cherry-picked commits that need to remain attributed to their original
  author with the original commit hash for archaeology.
- Truly orthogonal work (a docs PR and an unrelated code PR should stay
  separate).
- Dependency-chained PRs where the second PR genuinely needs to merge
  after the first.
