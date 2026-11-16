# Release notes

Each release's message on the [Releases page](https://github.com/mcawful/zexion-ap/releases) is
written for players, by hand. This folder holds the part that changes with each release: one
file per version, `<version>.md` (`1.1.0.md`). The notes live here, beside the developer docs,
because they are written and reviewed with the code, in the pull request that raises the
version, and the release gate can check them there.

## What the message is

`.github/workflows/release.yml` publishes the message `tools/release_notes.py` builds:

1. `<version>.md`, as written, except for its "Updating from" section (in 2).
2. The install guide, [`.github/release-footer.md`](../../.github/release-footer.md): installing,
   then "Updating from" the previous release, with this release's own update steps, if any,
   after the guide's.
3. **Full list of changes:** the compare link from the previous release.

To see it before it is published:

```sh
.venv/Scripts/python tools/release_notes.py 1.2.0 --previous v1.1.0
```

`1.0.0.md` is the exception: the first release had nothing to update from, so it carries its
own install steps, and its message has no install guide. `tests/test_release_notes.py` checks
that `1.0.0.md` and `1.1.0.md` rebuild their messages exactly as published.

## When they are written

In the pull request that raises the version (`docs/DEVELOPING.md`, "Branches and releases"):
copy [`TEMPLATE.md`](TEMPLATE.md) to `<version>.md` and draft it from the pull requests merged
into `develop` since the last release. The owner approves the wording there. The release gate
(`tools/release_check.py`) refuses a release whose notes are missing, empty, unfinished (a
`TODO` from the template or a `{{...}}` left in), or with an "Updating from" section that is
malformed (not a list of steps, or more than one) or not from the last release.

## How they read

[`1.1.0.md`](1.1.0.md) is the example to follow.

- **Written for players**, not for us. Say what they will see or can now do.
- **Lead with what changes for them.** The first words of each line are the point: "See who sent
  you a disk or an Intel Chip", then a sentence of detail.
- **One or two lines each**, in plain words. One change per line; leave out what a player never
  meets.
- **None of our own words:** no pull request numbers, commit hashes, the helper, the client's
  insides, offsets, review round numbers, or tests.
- **Fixes say what was wrong, from the player's side,** and that it no longer happens: "An Intel
  Chip you receive while the client is disconnected now still shows its hint", not what the
  code did.
- **Sections:** "What's new" for things a player can now do or will notice, "Fixes" for what
  was broken. Leave out a section with nothing in it.
- **"Updating from <previous>" only when a player must do something** beyond the install
  guide's steps (it already says to finish the seed first, replace all three files together and
  connect the client once). It is a list of those steps only, and the gate checks it names the
  last release.
- American English.
