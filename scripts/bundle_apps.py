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


def check_lock(lock):
    """Reasons the lock file cannot be trusted, empty if it is fine."""
    problems = []
    source = lock.get("source", {})
    if not re.fullmatch(r"https://[^\s]+", source.get("repo", "")):
        problems.append("source.repo must be an https URL")
    if not re.fullmatch(r"[0-9a-f]{40}", source.get("commit", "")):
        problems.append("source.commit must be a full 40 character commit hash (a branch would drift)")
    seen_paths, seen_ids, seen_names = set(), set(), set()
    for app in lock.get("apps", []):
        path = app.get("path", "")
        parts = path.split("/")
        if not path or path.startswith("/") or ".." in parts or len(parts) < 2:
            problems.append(f"unsafe or empty app path: {path!r}")
            continue
        if path in seen_paths:
            problems.append(f"app path listed twice: {path}")
        seen_paths.add(path)
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
    return problems


def git(*args, cwd=None):
    subprocess.run(["git", *args], cwd=cwd, check=True, stdout=subprocess.DEVNULL)


def fetch_source(lock):
    """The pinned commit of the source repository, checked out in the cache folder."""
    repo, commit = lock["source"]["repo"], lock["source"]["commit"]
    if os.path.isdir(os.path.join(CACHE, ".git")):
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=CACHE, capture_output=True, text=True).stdout.strip()
        if head == commit:
            return CACHE
        shutil.rmtree(CACHE)
    os.makedirs(CACHE)
    git("init", "-q", cwd=CACHE)
    git("remote", "add", "origin", repo, cwd=CACHE)
    print(f"fetching {repo} at {commit[:8]} ...")
    git("fetch", "-q", "--depth", "1", "origin", commit, cwd=CACHE)
    git("checkout", "-q", "FETCH_HEAD", cwd=CACHE)
    return CACHE


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
    source = fetch_source(lock)
    os.makedirs(STAGE, exist_ok=True)
    clean()
    count = 0
    for app in lock["apps"]:
        src = os.path.join(source, app["path"])
        if not os.path.isfile(os.path.join(src, "application.fam")):
            raise SystemExit(f"{app['path']} is not an app folder in the pinned source")
        dest = os.path.join(STAGE, os.path.basename(app["path"]))
        if os.path.exists(dest):
            raise SystemExit(f"{dest} exists and is not ours; move it away first")
        shutil.copytree(src, dest, ignore=shutil.ignore_patterns(".git", ".github"))
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
    source = fetch_source(lock)
    rows = []
    for app in lock["apps"]:
        info, licences = manifest_info(os.path.join(source, app["path"]))
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
            "where each app's own folder holds its licence and its source.\n\n"
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
            print(f"{app['path']:55} {' '.join(app['appids'])}")
        print(f"\n{len(lock['apps'])} app folders from {lock['source']['repo']} @ {lock['source']['commit'][:8]}")


if __name__ == "__main__":
    sys.exit(main())
