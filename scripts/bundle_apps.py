#!/usr/bin/env python3
"""Fetch and stage the third-party apps Hadal bundles.

    python3 scripts/bundle_apps.py fetch     # get the pinned source and put the apps in applications_user/
    python3 scripts/bundle_apps.py list      # what is bundled
    python3 scripts/bundle_apps.py clean     # remove what `fetch` put in applications_user/
    python3 scripts/bundle_apps.py notices   # write THIRD_PARTY_APPS.md from the apps' own manifests

applications/bundled_apps.json pins the source (Unleashed's all-the-plugins collection, at one
commit) and lists each app folder in it. `fetch` puts those folders in applications_user/, which the
build compiles against Hadal's own SDK and, like any app, packs into the update package.

The apps are other people's work under their own licences: the source repository is public, each
folder keeps its own LICENSE file, and nothing here modifies them. applications_user/ is not tracked
by git, so the repository stays small and the apps stay the originals.
"""

import argparse
import json
import os
import re
import shutil
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
        shutil.rmtree(cache)
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
        shutil.rmtree(os.path.join(STAGE, name))
    return len(staged_folders())


def fetch():
    lock = load_lock()
    caches = {name: fetch_source(lock, name) for name in lock_sources(lock)}
    os.makedirs(STAGE, exist_ok=True)
    clean()
    count = 0
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
    print(f"staged {count} apps in applications_user/; build as usual")


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
    with open(out, "w") as f:
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
    parser.add_argument("command", choices=("fetch", "list", "clean", "notices"))
    args = parser.parse_args()
    if args.command == "fetch":
        fetch()
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
