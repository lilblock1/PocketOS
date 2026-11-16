#!/usr/bin/env python3
"""Create a guarded R1 module initialization draft from immutable staged evidence."""
import argparse
import hashlib
import json
import re
import shlex
from pathlib import Path

MODULE = re.compile(r"[A-Za-z0-9_.+-]+\.ko")
SCRIPT = re.compile(r"[A-Za-z0-9_.-]+\.sh")
PARAM = re.compile(r"[A-Za-z_][A-Za-z0-9_]*=[A-Za-z0-9_.,:+/@%=-]*")

def digest(path):
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()

def read_commands(path):
    # Never interpret expansions, command substitutions, redirections or shell flow.
    raw = path.read_text(encoding="utf-8")
    raw = re.sub(r"\\\r?\n", " ", raw)
    result = []
    for lineno, line in enumerate(raw.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        try:
            args = shlex.split(line, comments=True, posix=True)
        except ValueError:
            args = []
        if args:
            result.append((lineno, args))
        else:
            result.append((lineno, ["<unparsed>"]))
    return result

def normalize(name):
    return name.removesuffix(".ko").replace("-", "_")

def build_plan(staging, integration):
    evidence = staging / "reference-startup-evidence"
    source = evidence / "driver_default_init_script.sh"
    modules = integration["modules"]
    blockers, calls, deferred = [], [], []
    def verified_script(name):
        record = integration["startup_evidence"].get(name)
        path = evidence / name
        if not record or path.is_symlink() or not path.is_file() or digest(path) != record["sha256"]:
            blockers.append({"script": name, "reason": "script missing or digest mismatch"})
            return None
        return path

    entry = verified_script(source.name)
    seen = set()
    for lineno, argv in read_commands(entry) if entry else []:
        if len(argv) != 2 or argv[0] not in ("sh", "/bin/sh") or not SCRIPT.fullmatch(argv[1]):
            blockers.append({"script": source.name, "line": lineno, "reason": "non-literal startup statement"})
            continue
        name = argv[1]
        if name in seen:
            blockers.append({"script": name, "reason": "duplicate startup script"})
            continue
        seen.add(name)
        path = verified_script(name)
        if not path:
            continue
        for line, args in read_commands(path):
            if (len(args) < 2 or args[0] not in ("insmod", "/sbin/insmod", "/bin/insmod")
                    or not MODULE.fullmatch(args[1])
                    or not all(PARAM.fullmatch(p) for p in args[2:])):
                blockers.append({"script": name, "line": line, "reason": "requires manual review; not a literal insmod"})
                continue
            module = args[1]
            record = modules.get(module)
            file = staging / "rootfs/module_driver" / module
            if not record or file.is_symlink() or not file.is_file() or digest(file) != record["sha256"]:
                blockers.append({"script": name, "module": module, "reason": "module missing or digest mismatch"})
                continue
            if any(c["module"] == module for c in calls):
                blockers.append({"script": name, "module": module, "reason": "duplicate module load"})
                continue
            calls.append({"script": name, "module": module, "parameters": args[2:],
                          "sha256": record["sha256"], "vermagic": record.get("vermagic")})
    loaded = set()
    names = {normalize(name): name for name in modules}
    for call in calls:
        module = call["module"]
        metadata = modules[module]
        if not metadata.get("vermagic") or metadata.get("depends") is None:
            blockers.append({"module": module, "reason": "missing module ABI or dependency metadata"})
        for dep in (metadata.get("depends") or "").split(","):
            dep = dep.strip()
            if not dep:
                continue
            if normalize(dep) in names and normalize(dep) not in loaded:
                blockers.append({"module": module, "dependency": dep, "reason": "dependency not loaded earlier"})
            elif normalize(dep) not in names:
                deferred.append({"module": module, "dependency": dep, "reason": "not a separate staged module; verify kernel builtin"})
        loaded.add(normalize(module))
    unplanned = sorted(set(modules) - {c["module"] for c in calls})
    if not calls:
        blockers.append({"reason": "no validated literal module loads"})
    if integration.get("blocked"):
        blockers.append({"reason": "Phase 2B integration report has unresolved gates"})
    required = {name for group in integration["subsystems"].values() for name in group["staged"]}
    for module in sorted(required - {c["module"] for c in calls}):
        # Broadcom radio drivers may intentionally be absent at boot. Do not
        # insert them in a guessed order; their services are a separate gate.
        if module in ("brcmfmac.ko", "brcmutil.ko"):
            deferred.append({"module": module, "reason": "radio service initialization not yet integrated"})
        else:
            blockers.append({"module": module, "reason": "required module absent from literal startup plan"})
    return {"phase": "2C", "flashable": False, "hardware_verified": False,
            "apply_enabled": False, "reference_kernel": integration["kernel"],
            "planned_loads": calls, "unplanned_modules": unplanned,
            "blockers": blockers, "deferred": deferred,
            "services": {"audio": "driver initialization draft only",
                         "display_input": "driver initialization draft only",
                         "battery": "monitoring only; no charger or governor writes",
                         "storage": "no mount/format/erase; read-only detection only",
                         "radio": "network daemons disabled until verified on demand",
                         "suspend": "capability probe only; no suspend requests"},
            "physical_test_required": True}

def render(plan):
    # The load function is deliberately unreachable from the command line.
    out = ["#!/bin/sh", "set -eu", "# DEVELOPMENT DRAFT: module application intentionally disabled.",
           "load_board_modules() {", "    :", "    # Stop on first insmod error; no force or unload."]
    for call in plan["planned_loads"]:
        args = ["/sbin/insmod", "/module_driver/" + call["module"], *call["parameters"]]
        out.append("    " + " ".join(shlex.quote(a) for a in args) + " || return 1")
    out.extend(["}", 'case "${1:---plan}" in', "  --plan)"])
    for call in plan["planned_loads"]:
        out.append("    printf '%s\\n' " + shlex.quote(call["module"]))
    out.extend(["    :", "    ;;", "  --apply)", "    printf '%s\\n' 'Disabled: requires verified kernel boot and physical-device approval.' >&2",
                "    exit 2", "    ;;", "  *) exit 2 ;;", "esac", ""])
    return "\n".join(out)

def main():
    p = argparse.ArgumentParser()
    for key in ("staging", "integration", "report"):
        p.add_argument(key, type=Path)
    a = p.parse_args()
    integration = json.loads(a.integration.read_text())
    if integration["flashable"] or integration["hardware_verified"]:
        raise SystemExit("Unexpected integration state")
    plan = build_plan(a.staging, integration)
    draft = a.staging / "rootfs/usr/sbin/pocketos-r1-init-draft"
    if draft.exists() or draft.is_symlink():
        raise SystemExit("Unexpected existing init draft")
    draft.write_text(render(plan))
    draft.chmod(0o755)
    a.report.write_text(json.dumps(plan, indent=2) + "\n")
    print("Phase 2C planned module loads:", len(plan["planned_loads"]),
          "review blockers:", len(plan["blockers"]), "deferred:", len(plan["deferred"]))
    print("Module application remains disabled; no vendor scripts executed.")

if __name__ == "__main__":
    main()
