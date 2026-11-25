#!/usr/bin/env python3
"""Fetch and stage the third-party apps Hadal bundles.

    python3 scripts/bundle_apps.py fetch     # get the pinned source and put the apps in applications_user/
    python3 scripts/bundle_apps.py list      # what is bundled
    python3 scripts/bundle_apps.py clean     # remove what `fetch` put in applications_user/
    python3 scripts/bundle_apps.py notices   # write THIRD_PARTY_APPS.md from the apps' own manifests
    python3 scripts/bundle_apps.py update    # move the pins to the sources' newest commits
    python3 scripts/bundle_apps.py update --check   # only say what that would change
    python3 scripts/bundle_apps.py update --new     # also list the app folders upstream that are not bundled

applications/bundled_apps.json pins the source (Unleashed's all-the-plugins collection, at one
commit) and lists each app folder in it. `fetch` puts those folders in applications_user/, which the
build compiles against Hadal's own SDK and, like any app, packs into the update package.

`update` is how the apps get their newer versions: a pin never moves by itself, because an upstream
commit that breaks the build, or ships a broken app, must not reach a device unseen. It reads what
changed between the pin and the source's newest commit (the folder trees and manifests only, a few
MB however large the repository is), says which bundled apps changed, which app ids changed, and
which app folders upstream are not bundled (marking those Hadal builds itself, which are never
bundled), then moves the pin. It never drops an app: if a bundled folder is gone from the newer
commit, that source's pin stays where it is and the folder is named.
After it: `fetch`, build, and run scripts/update_package_check.py (which refuses a package missing
a bundled app, or with an app built for a newer API than the firmware has), then try the apps that
changed on a device.

The apps are other people's work under their own licences: the source repository is public, each
folder keeps its own LICENSE file, and nothing here modifies them. applications_user/ is not tracked
by git, so the repository stays small and the apps stay the originals.
"""

import argparse
import json
import os
import re
import shutil
import stat
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(REPO, "applications", "bundled_apps.json")
STAGE = os.path.join(REPO, "applications_user")
CACHE = os.path.join(REPO, "build", "bundled_apps_source")
MARKER = ".hadal_bundled"  # written into each staged folder, so `clean` removes only what we put there


def load_lock(path=LOCK):
    with open(path) as f:
        lock = json.load(f)
    problems = check_lock(lock)
    if problems:
        raise SystemExit("bundled_apps.json is not valid:\n  " + "\n  ".join(problems))
    return lock


MAIN_SOURCE = "unleashed"  # lock["source"]; others are named in lock["sources"]


def lock_sources(lock):
    """Every source by name: the main one, and those an app may name with "source"."""
    sources = {MAIN_SOURCE: lock.get("source", {})}
    sources.update(lock.get("sources", {}))
    return sources


def check_lock(lock):
    """Reasons the lock file cannot be trusted, empty if it is fine."""
    problems = []
    sources = lock_sources(lock)
    for name, source in sources.items():
        label = "source" if name == MAIN_SOURCE else f"sources.{name}"
        if not re.fullmatch(r"https://[^\s]+", source.get("repo", "")):
            problems.append(f"{label}.repo must be an https URL")
        if not re.fullmatch(r"[0-9a-f]{40}", source.get("commit", "")):
            problems.append(f"{label}.commit must be a full 40 character commit hash (a branch would drift)")
    seen_paths, seen_ids, seen_names = set(), set(), set()
    for app in lock.get("apps", []):
        if app.get("source", MAIN_SOURCE) not in sources:
            problems.append(f"{app.get('path')} names an unknown source {app.get('source')!r}")
        path = app.get("path", "")
        parts = path.split("/")
        if not path or path.startswith("/") or ".." in parts:
            problems.append(f"unsafe or empty app path: {path!r}")
            continue
        key = (app.get("source", MAIN_SOURCE), path)
        if key in seen_paths:
            problems.append(f"app path listed twice: {path}")
        seen_paths.add(key)
        name = parts[-1]
        if name in seen_names:
            problems.append(f"two app folders would both be staged as {name}")
        seen_names.add(name)
        if not app.get("appids"):
            problems.append(f"{path} lists no app ids")
        for appid in app.get("appids", []):
            if appid in seen_ids:
                problems.append(f"app id listed twice: {appid}")
            seen_ids.add(appid)
        # parts of a folder left out of what is staged (files the app would install on the card)
        for omit in app.get("omit", []):
            omit_parts = omit.split("/")
            if not omit or omit.startswith("/") or ".." in omit_parts:
                problems.append(f"unsafe or empty omit path in {path}: {omit!r}")
        # files copied in from Hadal's own tree after staging (an asset the app's own folder in
        # the source collection does not carry, such as a shared icon other apps ship their own
        # copy of): {dest relative to the staged folder: source relative to the repo root}
        for dest, src in app.get("assets", {}).items():
            if not dest or dest.startswith("/") or ".." in dest.split("/"):
                problems.append(f"unsafe or empty assets dest in {path}: {dest!r}")
            if not src or src.startswith("/") or ".." in src.split("/") or not os.path.isfile(os.path.join(REPO, src)):
                problems.append(f"{path} assets.{dest}: {src!r} is not a file in the repo")
    return problems


def git(*args, cwd=None):
    subprocess.run(["git", *args], cwd=cwd, check=True, stdout=subprocess.DEVNULL)


def rmtree_force(path):
    """Remove a folder tree, the read-only object files git keeps in a clone included: Windows
    refuses to delete those, and a cache that could not be removed could not be fetched again."""

    def retry(func, target, _exc_info):
        os.chmod(target, stat.S_IWRITE)
        func(target)

    shutil.rmtree(path, onerror=retry)


def fetch_source(lock, name=MAIN_SOURCE):
    """A source's pinned commit, checked out in its cache folder. The main source is checked out
    whole; another only for the folders its apps list (a sparse, blobless fetch: Momentum's app
    repository is over a gigabyte, the few folders taken from it a fraction of that)."""
    source = lock_sources(lock)[name]
    repo, commit = source["repo"], source["commit"]
    cache = CACHE if name == MAIN_SOURCE else f"{CACHE}_{name}"
    paths = None
    if name != MAIN_SOURCE:
        paths = sorted(a["path"] for a in lock["apps"] if a.get("source", MAIN_SOURCE) == name)
    wanted = "\n".join(paths or []) + "\n"
    paths_file = os.path.join(cache, ".git", "hadal_paths")
    if os.path.isdir(os.path.join(cache, ".git")):
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=cache, capture_output=True, text=True).stdout.strip()
        had = open(paths_file).read() if os.path.exists(paths_file) else "\n"
        if head == commit and had == wanted:
            return cache
        rmtree_force(cache)
    os.makedirs(cache)
    git("init", "-q", cwd=cache)
    git("remote", "add", "origin", repo, cwd=cache)
    print(f"fetching {repo} at {commit[:8]} ...")
    if paths is None:
        git("fetch", "-q", "--depth", "1", "origin", commit, cwd=cache)
    else:
        git("sparse-checkout", "init", "--no-cone", cwd=cache)
        git("sparse-checkout", "set", "--no-cone", *[f"/{p}/" for p in paths], cwd=cache)
        git("fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", commit, cwd=cache)
    git("checkout", "-q", "FETCH_HEAD", cwd=cache)
    with open(paths_file, "w") as f:
        f.write(wanted)
    return cache


def staged_folders():
    if not os.path.isdir(STAGE):
        return []
    return [d for d in sorted(os.listdir(STAGE)) if os.path.exists(os.path.join(STAGE, d, MARKER))]


def clean():
    for name in staged_folders():
        rmtree_force(os.path.join(STAGE, name))
    return len(staged_folders())


def fetch():
    lock = load_lock()
    caches = {name: fetch_source(lock, name) for name in lock_sources(lock)}
    os.makedirs(STAGE, exist_ok=True)
    clean()
    count = 0
    builtin = builtin_apps()
    duplicates = []
    for app in lock["apps"]:
        src = os.path.join(caches[app.get("source", MAIN_SOURCE)], app["path"])
        if not os.path.isfile(os.path.join(src, "application.fam")):
            raise SystemExit(f"{app['path']} is not an app folder in the pinned source")
        dest = os.path.join(STAGE, os.path.basename(app["path"]))
        if os.path.exists(dest):
            raise SystemExit(f"{dest} exists and is not ours; move it away first")
        shutil.copytree(src, dest, ignore=shutil.ignore_patterns(".git", ".github"))
        # what the lock says to leave out (e.g. firmware images for tools Hadal does not ship)
        for omit in app.get("omit", []):
            target = os.path.join(dest, *omit.split("/"))
            if os.path.isdir(target):
                shutil.rmtree(target)
            elif os.path.exists(target):
                os.remove(target)
            else:
                raise SystemExit(f"{app['path']}: nothing to omit at {omit}")
        # files the lock adds in: the app's own source folder did not carry them, so its build
        # would otherwise be missing an icon or similar asset other bundled apps ship their own
        # copy of
        for asset_dest, asset_src in app.get("assets", {}).items():
            target = os.path.join(dest, *asset_dest.split("/"))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(REPO, asset_src), target)
        with open(os.path.join(dest, MARKER), "w") as f:
            f.write(app["path"] + "\n")
        count += 1
        # a module Hadal builds itself is not bundled again from a collection (by id: it would not
        # even build; by name: it would be there twice, under two ids)
        with open(os.path.join(dest, "application.fam"), errors="replace") as f:
            ids_names = external_apps(f.read())
        if is_builtin([i for i, _ in ids_names], [n for _, n in ids_names], builtin):
            duplicates.append(app["path"])
    if duplicates:
        raise SystemExit(
            "these bundled apps are modules Hadal builds itself, by id or by name; take them out of "
            "applications/bundled_apps.json (an app Hadal builds is not bundled again):\n  " + "\n  ".join(duplicates)
        )
    print(f"staged {count} apps in applications_user/; build as usual")


def save_lock(lock, path=LOCK):
    # the file's own shape (one space of indent, no final newline), so a diff shows only the change
    with open(path, "w", newline="\n") as f:
        f.write(json.dumps(lock, indent=1))


def tip_of(repo):
    """The commit a repository's default branch is at now."""
    out = subprocess.run(["git", "ls-remote", repo, "HEAD"], capture_output=True, text=True, check=True).stdout
    found = re.match(r"([0-9a-f]{40})\s+HEAD", out)
    if not found:
        raise SystemExit(f"cannot read the newest commit of {repo}")
    return found.group(1)


def external_apps(manifest):
    """The (id, name) of the apps in a manifest that are installed on the card
    (FlipperAppType.EXTERNAL): a manifest may hold several apps, and a plugin or a service in it is
    not one of these."""
    apps = []
    for body in re.findall(r"App\((.*?)\n\)", manifest, re.S):
        if "FlipperAppType.EXTERNAL" in body:
            found = re.search(r'appid\s*=\s*"([^"]+)"', body)
            if found:
                # the app's own name, not that of a library declared inside it (a deeper indent)
                name = re.search(r'^[ \t]{1,4}name\s*=\s*"([^"]*)"', body, re.M) or re.search(
                    r'\bname\s*=\s*"([^"]*)"', body
                )
                apps.append((found.group(1), name.group(1) if name else ""))
    return apps


def external_appids(manifest):
    return [appid for appid, _ in external_apps(manifest)]


def normalised(name):
    """A name as one that differs only in case, spaces or punctuation is the same one."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


def builtin_apps():
    """The ids and names (normalised) of the apps in Hadal's own tree, of any kind: a module Hadal
    builds itself is not bundled again from a collection, whatever it is called there."""
    ids, names = set(), set()
    for dirpath, _, files in os.walk(os.path.join(REPO, "applications")):
        if "application.fam" in files:
            with open(os.path.join(dirpath, "application.fam"), errors="replace") as f:
                text = f.read()
            for body in re.findall(r"App\((.*?)\n\)", text, re.S):
                found = re.search(r'appid\s*=\s*"([^"]+)"', body)
                name = re.search(r'^[ \t]{1,4}name\s*=\s*"([^"]*)"', body, re.M)
                if found:
                    ids.add(found.group(1))
                if name:
                    names.add(normalised(name.group(1)))
    return ids, names


def is_builtin(ids, names, builtin):
    """Whether an app (its ids, its names) is one Hadal already has: by id or by name."""
    built_ids, built_names = builtin
    return bool(set(ids) & built_ids) or any(normalised(n) in built_names for n in names if n)


def assess_update(apps, changed_files, folders_at_tip):
    """What moving a source to a newer commit does to the apps taken from it.

    apps: the lock's entries for the source. changed_files: the files that differ between the two
    commits. folders_at_tip: {app folder: its app ids} at the newer commit. Returns
      changed  the bundled folders with a changed file (one that is gone is not "changed": it is
               reported on its own)
      gone     the bundled folders the newer commit no longer has as an app (they would be lost)
      ids      {folder: (ids in the lock, ids now)} where an app's ids are not the lock's
      new      the app folders the newer commit has that are not bundled
    """
    bundled = {a["path"] for a in apps}
    gone = sorted(p for p in bundled if p not in folders_at_tip)
    return {
        "changed": sorted(
            p
            for p in bundled
            if p in folders_at_tip and any(f == p or f.startswith(p + "/") for f in changed_files)
        ),
        "gone": gone,
        "ids": {
            a["path"]: (sorted(a["appids"]), sorted(folders_at_tip[a["path"]]))
            for a in apps
            if a["path"] in folders_at_tip and sorted(a["appids"]) != sorted(folders_at_tip[a["path"]])
        },
        "new": sorted(p for p in folders_at_tip if p not in bundled),
    }


def inspect_source(repo, pin, tip, apps, cache):
    """Fetch two commits of a source into cache and say what moving from pin to tip does to its
    apps (see assess_update). Only what is needed: the folder trees of both commits, and the
    manifests of the newer one (a blobless, sparse fetch, a few MB however large the repository is)."""
    if os.path.isdir(cache):
        rmtree_force(cache)
    os.makedirs(cache)
    git("init", "-q", cwd=cache)
    git("remote", "add", "origin", repo, cwd=cache)
    git("sparse-checkout", "init", "--no-cone", cwd=cache)
    git("sparse-checkout", "set", "--no-cone", "**/application.fam", cwd=cache)
    for commit in (pin, tip):
        git("fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", commit, cwd=cache)
    git("checkout", "-q", tip, cwd=cache)
    # no renames: finding them reads file contents, which this fetch does not have
    diff = subprocess.run(
        ["git", "diff", "--no-renames", "--name-only", "-z", pin, tip],
        cwd=cache,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    changed_files = [name for name in diff.split("\0") if name]
    folders, names = {}, {}
    for dirpath, dirs, files in os.walk(cache):
        dirs[:] = [d for d in dirs if d != ".git"]
        if "application.fam" in files:
            with open(os.path.join(dirpath, "application.fam"), errors="replace") as f:
                found = external_apps(f.read())
            if found:  # a folder that holds no card app any more is not an app folder
                path = os.path.relpath(dirpath, cache).replace(os.sep, "/")
                folders[path] = [appid for appid, _ in found]
                names[path] = [name for _, name in found]
    result = assess_update(apps, changed_files, folders)
    result["files"] = len(changed_files)
    result["folders"] = folders
    result["names"] = names
    return result


def update(check_only=False, show_new=False):
    lock = load_lock()
    builtin = builtin_apps() if show_new else (set(), set())
    attention = moved = 0
    for name, source in lock_sources(lock).items():
        apps = [a for a in lock["apps"] if a.get("source", MAIN_SOURCE) == name]
        pin, tip = source["commit"], tip_of(source["repo"])
        label = source["repo"].split("github.com/")[-1]
        if tip == pin:
            print(f"{name}: {label} is at its newest commit, {pin[:8]}")
            continue
        print(f"{name}: {label} {pin[:8]} -> {tip[:8]}")
        found = inspect_source(source["repo"], pin, tip, apps, f"{CACHE}_update_{name}")
        print(f"  {found['files']} files changed upstream")
        changed = [os.path.basename(p) for p in found["changed"]]
        print(f"  {len(changed)} of {len(apps)} bundled apps changed" + (": " + ", ".join(changed) if changed else ""))
        for path, (old, new) in found["ids"].items():
            print(f"  app ids of {os.path.basename(path)}: {', '.join(old)} -> {', '.join(new)}")
        if found["new"]:
            print(f"  {len(found['new'])} app folders upstream are not bundled" + ("" if show_new else " (--new lists them)"))
            if show_new:
                # Hadal builds some of these itself (by id or by name): those are never to be bundled
                for path in found["new"]:
                    ids = found["folders"][path]
                    own = is_builtin(ids, found.get("names", {}).get(path, []), builtin)
                    print(f"    {path}  ({', '.join(ids)})" + ("  [built into Hadal: not for bundling]" if own else ""))
        if found["gone"]:
            attention += 1
            print(
                "  NOT MOVED: the newer commit has no app in "
                + ", ".join(found["gone"])
                + "; the pin stays so that no app is lost. Find where it went (renamed, moved?) and change"
                " its path in applications/bundled_apps.json, then update again."
            )
            continue
        if check_only:
            continue
        source["commit"] = tip
        for entry in apps:  # an app's ids follow the manifest, which is what the package check looks for
            if entry["path"] in found["ids"]:
                entry["appids"] = found["ids"][entry["path"]][1]
        moved += 1
    if moved:
        save_lock(lock)
        print(
            f"\nmoved {moved} pin(s) in applications/bundled_apps.json. Now: bundle_apps.py fetch, build,"
            " scripts/update_package_check.py, bundle_apps.py notices, and try the apps that changed on a device."
        )
    return 1 if attention else 0


def manifest_info(folder):
    """Name, author, web page and licence file of an app folder, from the app's own manifest."""
    info = {"names": [], "authors": set(), "urls": set()}
    with open(os.path.join(folder, "application.fam"), errors="replace") as f:
        text = f.read()
    for body in re.findall(r"App\((.*?)\n\)", text, re.S):
        if "FlipperAppType.EXTERNAL" not in body:
            continue
        for key, bucket in (("name", "names"), ("fap_author", "authors"), ("fap_weburl", "urls")):
            m = re.search(rf'{key}\s*=\s*"([^"]*)"', body)
            if m and m.group(1):
                if isinstance(info[bucket], list):
                    info[bucket].append(m.group(1))
                else:
                    info[bucket].add(m.group(1))
    licences = [n for n in os.listdir(folder) if re.match(r"(?i)(licen[cs]e|copying)", n)]
    return info, sorted(licences)


def notices():
    lock = load_lock()
    caches = {name: fetch_source(lock, name) for name in lock_sources(lock)}
    rows = []
    for app in lock["apps"]:
        info, licences = manifest_info(os.path.join(caches[app.get("source", MAIN_SOURCE)], app["path"]))
        rows.append(
            (
                ", ".join(info["names"]) or os.path.basename(app["path"]),
                ", ".join(sorted(info["authors"])) or "-",
                ", ".join(sorted(info["urls"])) or "-",
                ", ".join(licences) or "none in the source",
            )
        )
    rows.sort(key=lambda r: r[0].lower())
    out = os.path.join(REPO, "THIRD_PARTY_APPS.md")
    with open(out, "w", newline="\n") as f:  # the same bytes on any system, as the file in git
        f.write("# Bundled third-party apps\n\n")
        f.write(
            "These apps are not part of Hadal. They are other people's work, bundled in Hadal's update package "
            "and used under the licence each of them carries. They are built unchanged from\n"
            f"[{lock['source']['repo'].split('github.com/')[-1]}]({lock['source']['repo']}) at commit "
            f"`{lock['source']['commit'][:8]}`, a public collection kept up to date by the Unleashed team, "
            "where each app's own folder holds its licence and its source"
            + "".join(
                f", and from [{s['repo'].split('github.com/')[-1]}]({s['repo']}) at commit `{s['commit'][:8]}` "
                f"for the apps that collection does not have"
                for s in lock.get("sources", {}).values()
            )
            + ".\n\n"
            "Thank you to everyone whose apps these are.\n\n"
        )
        f.write("| App | Author | Web page | Licence file |\n|---|---|---|---|\n")
        for row in rows:
            f.write("| " + " | ".join(c.replace("|", "/") for c in row) + " |\n")
    print(f"wrote {out} ({len(rows)} apps)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("fetch", "list", "clean", "notices", "update"))
    parser.add_argument("--check", action="store_true", help="update: only say what would change")
    parser.add_argument("--new", action="store_true", help="update: list the app folders upstream that are not bundled")
    args = parser.parse_args()
    if args.command == "fetch":
        fetch()
    elif args.command == "update":
        return update(check_only=args.check, show_new=args.new)
    elif args.command == "clean":
        print(f"removed {clean() or 'all'} staged apps")
    elif args.command == "notices":
        notices()
    else:
        lock = load_lock()
        for app in lock["apps"]:
            print(f"{app.get('source', MAIN_SOURCE):10} {app['path']:55} {' '.join(app['appids'])}")
        for name, source in lock_sources(lock).items():
            count = sum(1 for a in lock["apps"] if a.get("source", MAIN_SOURCE) == name)
            print(f"{count} app folders from {source['repo']} @ {source['commit'][:8]}")


if __name__ == "__main__":
    sys.exit(main())
