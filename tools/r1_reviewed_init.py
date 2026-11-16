#!/usr/bin/env python3
"""Describe cold-start actions offline; intentionally provides no apply operation."""
import argparse
import copy
import json
from pathlib import Path
from r1_init_plan import read_commands, digest, normalize, MODULE, PARAM


def describe(drivers, integration, recipes, cmdline):
    if integration.get('flashable') is not False or recipes.get('apply_enabled') is not False:
        raise ValueError('Unexpected development state')
    # This reviewed reserved-memory recipe is specific to the actual captured boot.
    words = cmdline.split()
    if words.count('mem=64M@0x0') != 1 or any(w.startswith('rmem=') for w in words):
        raise ValueError('Reserved-memory contract differs; review rmem recipe again')
    for group in ('modules', 'startup_evidence'):
        for name, record in integration[group].items():
            file = drivers/name
            if Path(name).name != name or file.is_symlink() or not file.is_file() or digest(file) != record['sha256']:
                raise ValueError('Reference evidence differs: '+name)
    steps, preconditions, seen = [], [], set()
    for _, args in read_commands(drivers/'driver_default_init_script.sh'):
        if len(args) != 2 or args[0] != 'sh' or args[1] not in integration['startup_evidence'] or args[1] in seen:
            raise ValueError('Unsupported or duplicate startup entry')
        name = args[1]
        seen.add(name)
        recipe = recipes['recipes'].get(name)
        if recipe:
            if digest(drivers/name) != recipe['script_sha256']:
                raise ValueError('Reviewed recipe no longer matches: '+name)
            actions = copy.deepcopy(recipe['actions'])
            preconditions.extend(recipe.get('preconditions', []))
        else:
            actions = []
            for _, argv in read_commands(drivers/name):
                if len(argv) < 2 or argv[0] != 'insmod' or not MODULE.fullmatch(argv[1]) or not all(PARAM.fullmatch(p) for p in argv[2:]):
                    raise ValueError('Unreviewed statement: '+name)
                actions.append({'kind':'module', 'module':argv[1], 'parameters':argv[2:]})
        steps.extend(dict(action, source_script=name) for action in actions)
    loaded, loads, dependencies = set(), [], []
    module_names = {normalize(n) for n in integration['modules']}
    for step in steps:
        if step['kind'] != 'module':
            continue
        name = step['module']
        if name not in integration['modules'] or normalize(name) in loaded:
            raise ValueError('Unknown or duplicate module')
        record = integration['modules'][name]
        for dependency in (record.get('depends') or '').split(','):
            if dependency and normalize(dependency) not in loaded:
                dependencies.append({'module':name, 'dependency':dependency,
                                     'reason':'order' if normalize(dependency) in module_names else 'kernel builtin verification'})
        step['sha256'] = record['sha256']
        loaded.add(normalize(name))
        loads.append(name)
    if dependencies:
        raise ValueError('Unresolved dependency evidence: '+str(dependencies))
    return {'phase':'5D', 'flashable':False, 'apply_enabled':False,
            'cold_boot_verified':False, 'recovery_restore_verified':False,
            'source_review_completed':True, 'module_load_count':len(loads),
            'action_count':len(steps), 'steps':steps, 'preconditions':preconditions,
            'not_in_reference_boot_sequence':sorted(set(integration['modules'])-set(loads)),
            'remaining_gates':['physical cold initialization with display, input, SD and audio',
                               'new USB ADB endpoint operation and reconnect',
                               'recoverable boot method and physical stock restoration'],
            'notes':['No script or module executed; this is a JSON review report.',
                     'Current loaded module parameters are separate evidence, not a cold-boot test.']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('drivers','integration','recipes','cmdline','output'):
        p.add_argument(name, type=Path)
    a = p.parse_args()
    result = describe(a.drivers, json.loads(a.integration.read_text()),
                      json.loads(a.recipes.read_text()), a.cmdline.read_text())
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Offline cold-start review:',result['module_load_count'],'modules,',result['action_count'],'actions; apply remains disabled.')


if __name__ == '__main__':
    main()
