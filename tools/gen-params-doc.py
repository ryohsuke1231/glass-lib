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
    for m in materials:
        v = dict(presets[m['edge']]) if m.get('edge') else {}
        v.update(m['values'])
        values.append(v)

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
        for v in values:
            cells.append(fmt(v[p['key']]) if p['key'] in v else '')
        w('| ' + ' | '.join(cells) + ' |')
    w('')
    w('An empty cell: the material uses the default.')
    w('')
    w('## Tint')
    w('')
    w('The tint is a colour mixed into the glass: `tint-strength` above says how much, and the')
    w('colour is the first of the panel\'s [property@Glass.Panel:tint] (which sets both), the')
    w('context\'s [property@Glass.Context:tint-color], and the material\'s:')
    w('')
    w('| Material | Colour | Foreground colours |')
    w('|---|---|---|')
    for m in materials:
        colour = 'the theme\'s background' if m['tint_from_theme'] else \
            ('white' if m['tint'] == [1.0, 1.0, 1.0] else 'rgb(' + ', '.join(fmt(c) for c in m['tint']) + ')')
        fg = 'adapt to what is under the glass' if m['adaptive'] else 'the theme\'s'
        w(f'| {m["name"].upper()} | {colour} | {fg} |')
    w('')

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
