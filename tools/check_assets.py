"""Asset gate: every shipped model must be accounted for, licensed, and sane.

The rule this exists to enforce (docs/asset-pipeline.md): visuals never touch the
simulation, but they still have to be legal and correctly scaled, or the
presentation and the collision box silently disagree.

Runs over `assets/**/*.glb`. No third-party imports: a GLB is a 12-byte header plus
length-prefixed chunks, and the JSON chunk carries everything checked here.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'assets'
LEDGER = ROOT / 'docs' / 'assets-licenses.md'

# Name prefix decides what the asset claims to be, and the category bounds its size.
# A centimetre export is otherwise indistinguishable from a metre one.
CATEGORIES = {
    'sk_char_': (0.5, 2.5),   # characters: 0.5-2.5 m
    'sm_prop_': (0.05, 8.0),  # props: crates, doors, cover
    'wp_': (0.1, 2.0),        # weapons
    'sm_map_': (1.0, 200.0),  # a whole map's visual mesh
}

BAD_LICENSE_TOKENS = ('NC', 'ND', 'NONCOMMERCIAL', 'NODERIVATIVES')
MAX_TRIANGLES = 200_000


def read_glb_json(path: Path) -> dict | None:
    data = path.read_bytes()
    if len(data) < 20 or data[0:4] != b'glTF':
        return None
    _magic, version, _total = struct.unpack('<4sII', data[0:12])
    if version != 2:
        return None
    offset = 12
    while offset + 8 <= len(data):
        length, kind = struct.unpack('<II', data[offset:offset + 8])
        body = data[offset + 8:offset + 8 + length]
        if kind == 0x4E4F534A:  # 'JSON'
            return json.loads(body.decode('utf-8'))
        offset += 8 + length
    return None


def node_transforms(gltf: dict) -> list[str]:
    problems = []
    for index, node in enumerate(gltf.get('nodes', [])):
        scale = node.get('scale')
        if scale and any(abs(float(v) - 1.0) > 1e-6 for v in scale):
            name = node.get('name', f'node{index}')
            problems.append(f'{name}: scale {scale} (apply transforms before export)')
        if node.get('rotation') and any(abs(float(v)) > 1e-6 for v in node['rotation'][:3]):
            name = node.get('name', f'node{index}')
            problems.append(f'{name}: rotation on the node, bake it into the mesh')
        if node.get('translation') and any(abs(float(v)) > 1e-4 for v in node['translation']):
            name = node.get('name', f'node{index}')
            problems.append(f'{name}: translated off origin, pivot check would be wrong')
    return problems


def bounds_and_triangles(gltf: dict) -> tuple[list[float] | None, list[float] | None, int]:
    lo = None
    hi = None
    triangles = 0
    accessors = gltf.get('accessors', [])
    for mesh in gltf.get('meshes', []):
        for primitive in mesh.get('primitives', []):
            position = primitive.get('attributes', {}).get('POSITION')
            if position is None or position >= len(accessors):
                continue
            acc = accessors[position]
            amin = acc.get('min')
            amax = acc.get('max')
            if amin and amax:
                lo = list(amin) if lo is None else [min(a, b) for a, b in zip(lo, amin)]
                hi = list(amax) if hi is None else [max(a, b) for a, b in zip(hi, amax)]
            indices = primitive.get('indices')
            if indices is not None and indices < len(accessors):
                triangles += int(accessors[indices].get('count', 0)) // 3
            else:
                triangles += int(acc.get('count', 0)) // 3
    return lo, hi, triangles


def license_rows(ledger: Path) -> list[str]:
    if not ledger.exists():
        return []
    return [line for line in ledger.read_text(encoding='utf-8').splitlines() if line.startswith('|')]


def check_asset(path: Path, rows: list[str]) -> list[str]:
    problems = []
    name = path.name
    category = next((key for key in CATEGORIES if name.startswith(key)), None)
    if category is None:
        known = ', '.join(CATEGORIES)
        problems.append(f'{name}: name does not start with a known category ({known})')
    row = next((line for line in rows if name in line), None)
    if row is None:
        problems.append(f'{name}: no row in docs/assets-licenses.md')
    else:
        upper = row.upper()
        for token in BAD_LICENSE_TOKENS:
            if token in upper:
                problems.append(f'{name}: licence says {token}, which iron law H forbids')
                break
    gltf = read_glb_json(path)
    if gltf is None:
        problems.append(f'{name}: not a readable glTF 2.0 binary')
        return problems
    problems.extend(f'{name}: {line}' for line in node_transforms(gltf))
    lo, hi, triangles = bounds_and_triangles(gltf)
    if lo is None or hi is None:
        problems.append(f'{name}: no POSITION bounds, cannot check units or pivot')
    else:
        extent = max(h - l for l, h in zip(lo, hi))
        if category is not None:
            low, high = CATEGORIES[category]
            if not (low <= extent <= high):
                problems.append(f'{name}: size {extent:.3f} m outside {category} '
                                f'range {low}-{high} m (metres exported? scale applied?)')
        # glTF is +Y up; the ground plane is Y == 0, not Z.
        if abs(lo[1]) > 0.02:
            problems.append(f'{name}: lowest point is Y={lo[1]:.3f}, pivot must sit on Y=0')
    if triangles > MAX_TRIANGLES:
        problems.append(f'{name}: {triangles} triangles, over the {MAX_TRIANGLES} hard cap')
    return problems


def run(assets: Path, ledger: Path) -> int:
    rows = license_rows(ledger)
    files = sorted(assets.rglob('*.glb')) if assets.exists() else []
    problems = []
    for path in files:
        problems.extend(check_asset(path, rows))
    for line in problems:
        print(f'FAIL {line}')
    print(f'assets: {len(files)} glb checked, {len(problems)} problem(s)')
    return 1 if problems else 0


def write_glb(path: Path, lo, hi, vertex_count, index_count, scale=None, translate=None):
    nodes = [{'name': 'root', 'mesh': 0}]
    if scale is not None:
        nodes[0]['scale'] = scale
    if translate is not None:
        nodes[0]['translation'] = translate
    gltf = {
        'asset': {'version': '2.0'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': nodes,
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': vertex_count, 'type': 'VEC3',
             'min': lo, 'max': hi},
            {'bufferView': 1, 'componentType': 5125, 'count': index_count, 'type': 'SCALAR'},
        ],
        'bufferViews': [{'buffer': 0, 'byteLength': 12}, {'buffer': 0, 'byteLength': 12}],
        'buffers': [{'byteLength': 24}],
    }
    payload = json.dumps(gltf).encode('utf-8')
    payload += b' ' * ((4 - len(payload) % 4) % 4)
    path.parent.mkdir(parents=True, exist_ok=True)
    header = b'glTF' + struct.pack('<II', 2, 12 + 8 + len(payload))
    path.write_bytes(header + struct.pack('<II', len(payload), 0x4E4F534A) + payload)


def selftest(tmp: Path) -> int:
    tmp.mkdir(parents=True, exist_ok=True)
    for stale in tmp.glob('*.glb'):
        stale.unlink()
    ledger = tmp / 'assets-licenses.md'
    write_glb(tmp / 'sk_char_good.glb', [-0.4, 0.0, -0.4], [0.4, 1.8, 0.4], 24, 36)
    write_glb(tmp / 'sm_prop_nolicense.glb', [-0.5, 0.0, -0.5], [0.5, 1.0, 0.5], 24, 36)
    write_glb(tmp / 'wp_nc.glb', [-0.5, 0.0, -0.5], [0.5, 0.8, 0.5], 24, 36)
    write_glb(tmp / 'sm_prop_cm.glb', [-50.0, 0.0, -50.0], [50.0, 140.0, 50.0], 24, 36)
    write_glb(tmp / 'sk_char_pivot.glb', [-0.4, 0.9, -0.4], [0.4, 2.7, 0.4], 24, 36)
    write_glb(tmp / 'sm_prop_scaled.glb', [-0.5, 0.0, -0.5], [0.5, 1.0, 0.5], 24, 36,
              scale=[2.0, 2.0, 2.0])
    write_glb(tmp / 'sm_prop_big.glb', [-0.5, 0.0, -0.5], [0.5, 1.0, 0.5], 24, 3 * 250_000)
    ledger.write_text(
        '| asset | source | licence | author | ships | note |\n'
        '|---|---|---|---|---|---|\n'
        '| sk_char_good.glb | selftest | CC0 | us | yes | - |\n'
        '| wp_nc.glb | selftest | CC BY-NC | someone | no | - |\n'
        '| sm_prop_cm.glb | selftest | CC0 | us | yes | - |\n'
        '| sk_char_pivot.glb | selftest | CC0 | us | yes | - |\n'
        '| sm_prop_scaled.glb | selftest | CC0 | us | yes | - |\n'
        '| sm_prop_big.glb | selftest | CC0 | us | yes | - |\n',
        encoding='utf-8')
    expectations = {
        'sk_char_good.glb': False,
        'sm_prop_nolicense.glb': True,
        'wp_nc.glb': True,
        'sm_prop_cm.glb': True,
        'sk_char_pivot.glb': True,
        'sm_prop_scaled.glb': True,
        'sm_prop_big.glb': True,
    }
    rows = license_rows(ledger)
    failures = 0
    for name, should_fail in expectations.items():
        problems = check_asset(tmp / name, rows)
        caught = bool(problems)
        verdict = 'ok' if caught == should_fail else 'FAIL'
        if caught != should_fail:
            failures += 1
        print(f'  {verdict:<4} {name:<26} {problems[0] if problems else "clean"}')
    print(f'asset selftest: {len(expectations)} cases, {failures} wrong')
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest(ROOT / 'build' / 'assets_selftest')
    return run(ASSETS, LEDGER)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
