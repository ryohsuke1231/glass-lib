#!/usr/bin/env python3
"""Generates the API reference's page of parameters and materials
(docs/reference/, gi-docgen) from spec/params.json, the one definition.

    gen-params-doc.py spec/params.json OUTPUT.md
"""
import json
import sys


def fmt(v):
    return f'{v:g}'


def main():
    spec_path, out_path = sys.argv[1], sys.argv[2]
    with open(spec_path, encoding='utf-8') as f:
        spec = json.load(f)

    params = spec['params']
    materials = spec['materials']
    presets = spec['edge_presets']
    values = []
    darks = []
    lenses = spec['lenses']
    for m in materials:
        v = {k: x for k, x in presets[m['edge']].items() if not k.startswith('_')} if m.get('edge') else {}
        if 'lens' in v:
            v.update(lenses[v.pop('lens')])
        v.update(m['values'])
        values.append(v)
        darks.append(m.get('values_dark', {}))

    out = []
    w = out.append
    w('Title: Parameters and Materials')
    w('Slug: parameters')
    w('')
    w('# Parameters and Materials')
    w('')
    w('Every key of [method@Glass.Context.set_param] and [method@Glass.Panel.set_param], its range, and')
    w('the value each [enum@Glass.Material] uses. A panel\'s glass takes the first of: the value set on')
    w('the panel, the value set on the context, the panel\'s material\'s, the default.')
    w('')
    w('Each key has a constant named after it, for C and the bindings: `GLASS_PARAM_BLUR_RADIUS`')
    w('(`Glass.PARAM_BLUR_RADIUS`) is `"blur-radius"`. A misspelt constant fails to compile (or')
    w('raises), where a misspelt string is only a warning at run time.')
    w('')
    w('Lengths ("px") are in logical pixels: they are multiplied by the surface scale when drawn.')
    w('The defaults are those of the Liquid Glass GNOME Shell extension, for large glass; the')
    w('materials carry the values for glass inside an application window.')
    w('')
    header = '| Key | Range | Default | ' + ' | '.join(m['name'].upper() for m in materials) + ' |'
    w(header)
    w('|' + '---|' * (3 + len(materials)))
    for p in params:
        rng = f'{fmt(p["min"])} – {fmt(p["max"])}'
        if 'values' in p:
            rng = ', '.join(fmt(v) for v in p['values'])
        unit = ' px' if p['px'] else ''
        cells = [f'`{p["key"]}`', rng + unit, fmt(p['default'])]
        for v, d in zip(values, darks):
            cell = fmt(v[p['key']]) if p['key'] in v else ''
            if p['key'] in d:
                cell = f'{cell or fmt(p["default"])} / {fmt(d[p["key"]])}'
            cells.append(cell)
        w('| ' + ' | '.join(cells) + ' |')
    w('')
    w('An empty cell: the material uses the default. Two values: the light appearance\'s, then the')
    w('dark\'s (libadwaita\'s `AdwStyleManager:dark`).')
    w('')
    w('## Body and outline')
    w('')
    w('Beyond these keys each material has a body and an outline of its own, fixed (they are not')
    w('settings). REGULAR, THICK and MENU have the body of SwiftUI\'s `glassEffect(.regular)` on iOS 27:')
    w('a frost (the content blurred by 14 px) laid over a copy of it blurred by `blur-radius`, the tint,')
    w('then the saturation; CLEAR and PROMINENT the plain one (the content blurred by `blur-radius` at a')
    w('saturation of 1.5, then the tint). All have iOS 27\'s outline: a half-point line of the content just')
    w('outside the glass, darkened across the light axis, and two highlights at its ends.')
    w('')
    surfaces = spec.get('surfaces', {})
    outlines = spec.get('outlines', {})
    terms = ('saturation', 'frost', 'frost-opacity', 'frost-clamp', 'frost-weight', 'blur-weight',
             'rim-shade', 'rim-shade-ends', 'rim-light', 'edge-absorption')
    w('| Material | ' + ' | '.join(f'`{t}`' for t in terms) + ' |')
    w('|---|' + '---|' * len(terms))
    for m in materials:
        cells = []
        for t in terms:
            vals = []
            for a in ('light', 'dark'):
                x = {}
                if m.get('surface'):
                    x.update(surfaces[m['surface']][a])
                if m.get('outline'):
                    x.update(outlines[m['outline']][a])
                vals.append(fmt(x[t]) if t in x else '')
            cells.append(vals[0] if vals[0] == vals[1] else f'{vals[0]} / {vals[1]}')
        w(f'| {m["name"].upper()} | ' + ' | '.join(cells) + ' |')
    w('')
    w('## Tint')
    w('')
    w('## Lenses')
    w('')
    w('The lens is shaped by `max-z`, `profile-shape-n` and `displacement-scale` together. The two measured')
    w('on macOS can be set in one call, [method@Glass.Panel.set_lens] or [method@Glass.Context.set_lens],')
    w('as the panel\'s or the context\'s values of those three keys:')
    w('')
    w('| Lens | ' + ' | '.join(f'`{k}`' for k in ('max-z', 'profile-shape-n', 'displacement-scale')) + ' | Materials |')
    w('|---|---|---|---|---|')
    for name in lenses['order']:
        l = lenses[name]
        users = ', '.join(m['name'].upper() for m in materials
                          if m.get('edge') and presets[m['edge']].get('lens') == name)
        w(f'| {name.upper()} | ' + ' | '.join(fmt(l[k]) for k in ('max-z', 'profile-shape-n', 'displacement-scale')) + f' | {users} |')
    w('')
    w('The tint is a colour mixed into the glass: `tint-strength` above says how much, and the')
    w('colour is the first of the panel\'s [property@Glass.Panel:tint] (which sets both), the')
    w('context\'s [property@Glass.Context:tint-color], and the material\'s:')
    w('')
    w('| Material | Colour | Foreground colours |')
    w('|---|---|---|')
    for m in materials:
        colour = 'the theme\'s background' if m['tint_from_theme'] else \
            'the theme\'s accent (`--accent-bg-color`)' if m.get('tint_from_accent') else \
            ('white' if m['tint'] == [1.0, 1.0, 1.0] else 'rgb(' + ', '.join(fmt(c) for c in m['tint']) + ')')
        fg = 'adapt to what is under the glass' if m['adaptive'] else \
            'the theme\'s accent foreground (`--accent-fg-color`)' if m.get('tint_from_accent') else 'the theme\'s'
        w(f'| {m["name"].upper()} | {colour} | {fg} |')
    w('')

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
