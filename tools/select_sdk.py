#!/usr/bin/env python3
"""Select a successful main-branch foundation run with both live artifacts."""
import json
import os
import subprocess
import sys

repo=os.environ["GITHUB_REPOSITORY"]
requested=os.environ.get("REQUESTED_RUN","").strip()
artifact_names={"PocketOS-compiled-MIPS-SDK","PocketOS-0.1.0-alpha-foundation-NOT-FIRMWARE"}

def api(path):
    return json.loads(subprocess.check_output(["gh","api",path],text=True))

workflow=api(f"repos/{repo}/actions/workflows/pocketos-foundation.yml")["id"]
def usable(run):
    return (run["workflow_id"]==workflow and run["head_branch"]=="main"
            and run["conclusion"]=="success" and run["status"]=="completed"
            and (run.get("head_repository") or {}).get("full_name")==repo)

if requested:
    if not requested.isdecimal():
        sys.exit("Foundation run ID must be numeric.")
    runs=[api(f"repos/{repo}/actions/runs/{requested}")]
else:
    runs=api(f"repos/{repo}/actions/workflows/{workflow}/runs?branch=main&status=success&per_page=100")["workflow_runs"]

selected=None
for run in runs:
    if not usable(run):
        continue
    artifacts=api(f"repos/{repo}/actions/runs/{run['id']}/artifacts?per_page=100")["artifacts"]
    names={a["name"] for a in artifacts if not a["expired"]}
    if artifact_names <= names:
        selected=run
        break
if not selected:
    if requested:
        sys.exit("Requested run has no successful, unexpired SDK/rootfs pair.")
    print("::notice::No reusable compiled SDK yet. Host checks only; do not start a foundation build automatically.")
else:
    with open(os.environ["GITHUB_OUTPUT"],"a") as f:
        print("run_id="+str(selected["id"]),file=f)
        print("commit="+selected["head_sha"],file=f)
    print("Reusing foundation run",selected["id"],selected["head_sha"])
