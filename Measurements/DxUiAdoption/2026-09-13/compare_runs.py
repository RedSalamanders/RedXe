import json
import pathlib
import statistics

root = pathlib.Path(__file__).resolve().parent
metrics = ('completedOffscreenFps', 'frameP95Ms', 'prepareP95Ms', 'composeCpuP95Ms',
           'privateBytes', 'workingSetBytes', 'privateGrowthBytes', 'surfaceBytes')
output = {'method': 'Each comparison uses per-scenario medians of five 600-frame rounds. ABBA order; A/A and B/B retained. Descriptive results, no automatic acceptance.', 'profiles': {}}
for profile in ('Debug', 'Release'):
    paths = sorted(root.glob(f'av-20260912-{profile}-*.json'))
    assert len(paths) == 4, paths
    runs = [json.loads(p.read_text(encoding='utf-8-sig')) for p in paths]
    for run in runs:
        for field in ('fixture', 'renderer', 'views', 'framesPerRound', 'roundCount', 'compiler'):
            assert run[field] == runs[0][field], (profile, field)
        for field in ('platform', 'configuration', 'machine', 'cpu', 'operatingSystem', 'warpVersion', 'powerPolicy', 'fixtureInputs', 'productionInputs', 'wrapperSha256'):
            assert run['metadata'][field] == runs[0]['metadata'][field], (profile, field)
        assert len(run['scenarios']) == 6
        assert run['hiddenSurfaceBytes'] == run['hiddenPreparations'] == run['hiddenComposites'] == 0
    comparisons = []
    for before, after, label in ((0, 1, 'A1/B1'), (3, 2, 'A2/B2'), (0, 3, 'A1/A2'), (1, 2, 'B1/B2')):
        scenarios = []
        for a, b in zip(runs[before]['scenarios'], runs[after]['scenarios']):
            assert (a['name'], a['dpi']) == (b['name'], b['dpi'])
            values = {}
            for metric in metrics:
                if metric not in a['rounds'][0]:
                    continue
                x = statistics.median(r[metric] for r in a['rounds'])
                y = statistics.median(r[metric] for r in b['rounds'])
                values[metric] = {'before': x, 'after': y, 'changePercent': (y/x-1)*100 if x else None}
            scenarios.append({'name': a['name'], 'dpi': a['dpi'], 'metrics': values})
        comparisons.append({'label': label, 'before': paths[before].name, 'after': paths[after].name, 'scenarios': scenarios})
    output['profiles'][profile] = {'identityMatched': True, 'hiddenResourcesZero': True, 'comparisons': comparisons}
dest = root / 'av-20260912-comparison.json'
dest.write_text(json.dumps(output, indent=2)+'\n', encoding='utf-8')
for profile, report in output['profiles'].items():
    for comparison in report['comparisons']:
        print(profile, comparison['label'])
        for s in comparison['scenarios']:
            changes = ', '.join(f'{key}={s["metrics"][key]["changePercent"]:+.1f}%' for key in ('completedOffscreenFps', 'frameP95Ms', 'privateBytes', 'workingSetBytes'))
            print(f'  {s["dpi"]} {s["name"]}: {changes}')
