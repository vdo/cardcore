#!/usr/bin/env python3
"""Builds src/map_world.h for the map page: world coastlines and borders at two levels of detail,
provinces, rivers, the Catalonia outline and towns.

  python3 tools/mkmap.py <dir with the geojson files> > src/map_world.h

Inputs, from github.com/nvkelso/natural-earth-vector/geojson (Natural Earth, public domain):
ne_110m_coastline, ne_110m_admin_0_boundary_lines_land, ne_10m_coastline,
ne_10m_admin_0_boundary_lines_land, ne_10m_admin_1_states_provinces_lines,
ne_10m_rivers_lake_centerlines, ne_10m_rivers_europe, ne_10m_populated_places_simple;
and spain-communities.geojson (IGN via CartoDB) from github.com/codeforgermany/click_that_hood.

Lines are cut into chunks of <= 48 points. A chunk has a base corner (1/100 degree) and a bounding
box, so the firmware only draws what's on screen; its points are uint16 {lat, lon} offsets from the
base in 1/10000 degree (~11 m), shifted right by the chunk's `shift` when the chunk is wider than
6.5 degrees. The points are one string literal: compilers chew through that far faster than a
million-element initializer."""
import json, math, os, struct, sys

def lines(path):
    for f in json.load(open(path))['features']:
        g = f['geometry']
        if not g: continue
        if g['type'] == 'LineString': yield g['coordinates']
        elif g['type'] == 'MultiLineString': yield from g['coordinates']

def merge(ls):  # joins lines that meet end to end where only two meet: fewer, longer chunks
    ls = [l for l in ls if len(l) >= 2]
    key = lambda p: (round(p[0], 6), round(p[1], 6))
    ends = {}
    for i, l in enumerate(ls):
        ends.setdefault(key(l[0]), []).append(i)
        ends.setdefault(key(l[-1]), []).append(i)
    used = [False] * len(ls)
    def grow(line):  # extends `line` at its end
        while True:
            at = ends[key(line[-1])]
            nxt = [j for j in at if not used[j]]
            if len(at) != 2 or not nxt: return line
            j = nxt[0]
            used[j] = True
            l = ls[j] if key(ls[j][0]) == key(line[-1]) else ls[j][::-1]
            line = line + l[1:]
    for i, l in enumerate(ls):
        if used[i]: continue
        used[i] = True
        yield grow(grow(l)[::-1])

def dp(pts, tol):  # Douglas-Peucker in degrees, iterative (coastlines run to thousands of points)
    if tol == 0 or len(pts) < 3: return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        if b - a < 2: continue
        (ax, ay), (bx, by) = pts[a][:2], pts[b][:2]
        dx, dy = bx - ax, by - ay
        L = dx * dx + dy * dy
        m, mi = -1, 0
        for i in range(a + 1, b):
            px, py = pts[i][:2]
            t = 0 if L == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
            d = math.hypot(px - ax - t * dx, py - ay - t * dy)
            if d > m: m, mi = d, i
        if m > tol:
            keep[mi] = True
            stack += [(a, mi), (mi, b)]
    return [p for p, k in zip(pts, keep) if k]

data, npts, chunks = bytearray(), 0, {}
def add(layer, line, tol, min_span=0.0):
    xs, ys = [p[0] for p in line], [p[1] for p in line]
    if max(max(xs) - min(xs), max(ys) - min(ys)) < min_span: return  # drop specks
    q = []
    for p in dp(line, tol):
        e = (round(p[1] * 10000), round(p[0] * 10000))  # lat, lon in 1/10000 degree
        if not q or q[-1] != e: q.append(e)
    if len(q) < 2: return
    global npts
    for s in range(0, len(q) - 1, 47):  # chunks share their end point
        c = q[s:s + 48]
        la0, lo0 = min(p[0] for p in c) // 100, min(p[1] for p in c) // 100  # floor, 1/100 degree
        la1, lo1 = -(-max(p[0] for p in c) // 100), -(-max(p[1] for p in c) // 100)
        span = max(max(p[0] for p in c) - la0 * 100, max(p[1] for p in c) - lo0 * 100)
        shift = 0
        while span >> shift > 65535: shift += 1
        chunks.setdefault(layer, []).append((la0, lo0, la1 - la0, lo1 - lo0, npts, len(c), shift))
        for a, b in c:
            data.extend(struct.pack('<HH', (a - la0 * 100) >> shift, (b - lo0 * 100) >> shift))
        npts += len(c)

d = sys.argv[1]
src = lambda n: os.path.join(d, n + '.geojson')
for l in lines(src('ne_110m_coastline')): add('COAST_LO', l, 0)
for l in lines(src('ne_110m_admin_0_boundary_lines_land')): add('BORDER_LO', l, 0)
for l in merge(lines(src('ne_10m_coastline'))): add('COAST', l, 0.001)
for l in merge(lines(src('ne_10m_admin_0_boundary_lines_land'))): add('BORDER', l, 0.001)
for l in merge(lines(src('ne_10m_admin_1_states_provinces_lines'))): add('PROVINCE', l, 0.005)
for n in ('ne_10m_rivers_lake_centerlines', 'ne_10m_rivers_europe'):
    for l in merge(lines(src(n))): add('RIVER', l, 0.005)
cat = [f for f in json.load(open(src('spain-communities')))['features']
       if f['properties']['cod_ccaa'] == '09'][0]['geometry']['coordinates']
ring = max((p[0] for p in cat), key=len)
add('CATALONIA', ring + [ring[0]], 0.0005)

# Towns. rank 0: shown from the Catalonia zoom (2.5 km/px) in; rank 1: local zooms only.
TOWNS = [  # Catalonia, finer than Natural Earth (which has 4 places here)
    ('Barcelona', 41.3874, 2.1686, 0), ('Girona', 41.9794, 2.8214, 0), ('Lleida', 41.6176, 0.6200, 0),
    ('Tarragona', 41.1189, 1.2445, 0), ('Reus', 41.1561, 1.1069, 1), ('Manresa', 41.7251, 1.8266, 1),
    ('Vic', 41.9301, 2.2549, 1), ('Figueres', 42.2654, 2.9581, 1), ('Tortosa', 40.8125, 0.5216, 1),
    ('Puigcerdà', 42.4317, 1.9283, 1), ("la Seu d'Urgell", 42.3580, 1.4560, 1), ('Vielha', 42.7010, 0.7956, 1),
    ('Sabadell', 41.5463, 2.1086, 1), ('Terrassa', 41.5632, 2.0089, 1), ('Mataró', 41.5381, 2.4445, 1),
    ('Igualada', 41.5791, 1.6174, 1), ('Vilanova', 41.2241, 1.7256, 1), ('Olot', 42.1817, 2.4900, 1),
    ('Balaguer', 41.7904, 0.8054, 1), ('Amposta', 40.7130, 0.5810, 1), ('Granollers', 41.6083, 2.2878, 1),
    ('Badalona', 41.4500, 2.2474, 1), ("l'Hospitalet", 41.3597, 2.0997, 1), ('Sant Cugat', 41.4722, 2.0864, 1),
    ('Rubí', 41.4933, 2.0325, 1), ('Castelldefels', 41.2800, 1.9767, 1), ('Sitges', 41.2372, 1.8059, 1),
    ('Vilafranca', 41.3466, 1.6996, 1), ('el Vendrell', 41.2186, 1.5350, 1), ('Valls', 41.2864, 1.2497, 1),
    ('Montblanc', 41.3757, 1.1622, 1), ('Cambrils', 41.0670, 1.0597, 1), ('Salou', 41.0764, 1.1416, 1),
    ('Falset', 41.1453, 0.8197, 1), ('Gandesa', 41.0528, 0.4372, 1), ("Móra d'Ebre", 41.0919, 0.6431, 1),
    ('Cervera', 41.6706, 1.2717, 1), ('Tàrrega', 41.6469, 1.1394, 1), ('Mollerussa', 41.6314, 0.8950, 1),
    ('Solsona', 41.9942, 1.5172, 1), ('Berga', 42.1036, 1.8456, 1), ('Ripoll', 42.2011, 2.1906, 1),
    ('Banyoles', 42.1169, 2.7664, 1), ('Palafrugell', 41.9174, 3.1631, 1), ('Palamós', 41.8467, 3.1292, 1),
    ('Blanes', 41.6741, 2.7903, 1), ('Lloret de Mar', 41.6997, 2.8456, 1), ('Sant Feliu de G.', 41.7833, 3.0333, 1),
    ('la Bisbal', 41.9603, 3.0372, 1), ('Tremp', 42.1672, 0.8947, 1), ('Sort', 42.4131, 1.1308, 1),
    ('el Pont de Suert', 42.4078, 0.7400, 1), ('Calella', 41.6139, 2.6544, 1), ('Sant Celoni', 41.6892, 2.4897, 1),
    ('les Borges Blanques', 41.5217, 0.8686, 1), ('la Ràpita', 40.6178, 0.5917, 1), ('Deltebre', 40.7194, 0.7181, 1),
    ("l'Ametlla de Mar", 40.8836, 0.8022, 1), ('Roses', 42.2619, 3.1767, 1), ('Cadaqués', 42.2886, 3.2778, 1),
    ('Camprodon', 42.3117, 2.3650, 1), ('Santa Coloma de F.', 41.8606, 2.6694, 1), ('Moià', 41.8111, 2.0964, 1),
    ('Martorell', 41.4744, 1.9306, 1), ('Mollet', 41.5397, 2.2131, 1), ('Cerdanyola', 41.4914, 2.1406, 1),
    ('el Prat', 41.3264, 2.0947, 1)]
towns = list(TOWNS)
for f in json.load(open(src('ne_10m_populated_places_simple')))['features']:
    p = f['properties']
    lat, lon = p['latitude'], p['longitude']
    if any(abs(lat - t[1]) < 0.05 and abs(lon - t[2]) < 0.05 for t in TOWNS): continue  # already listed
    towns.append((p['name'], lat, lon, 0 if p['scalerank'] <= 4 else 1))

def cstr(s):  # UTF-8 as \x escapes, each in its own literal so a following hex digit can't join it
    return '"' + ''.join(c if c.isascii() and c not in '"\\' else ''.join(f'\\x{b:02x}""' for b in c.encode()) for c in s) + '"'

out = ['// Generated by tools/mkmap.py from Natural Earth (public domain) and IGN/CartoDB (Catalonia).',
       '#pragma once', '#include <stdint.h>',
       '// Base corner and box in 1/100 degree; points are uint16 {lat, lon} pairs from MAP_DATA,',
       '// offsets from the base in 1/10000 degree, shifted right by `shift`.',
       'struct MapChunk { int16_t lat0, lon0; uint16_t dlat, dlon; uint32_t start; uint8_t n, shift; };',
       'struct MapTown { const char* name; int32_t lat, lon; uint8_t rank; };  // 1e-4 degree',
       f'alignas(4) static const char MAP_DATA[{len(data) + 1}] =']
for i in range(0, len(data), 40):
    out.append('  "' + ''.join(f'\\x{b:02x}' for b in data[i:i + 40]) + '"')
out[-1] += ';'
for name, cs in chunks.items():
    out.append(f'static const MapChunk MAP_{name}[] = {{')
    out += [f'  {{{a},{b},{c},{e},{s},{n},{k}}},' for a, b, c, e, s, n, k in cs]
    out.append('};')
    out.append(f'constexpr int NMAP_{name} = sizeof MAP_{name} / sizeof *MAP_{name};')
out.append('static const MapTown MAP_TOWNS[] = {')
out += [f'  {{{cstr(n)}, {round(la * 10000)}, {round(lo * 10000)}, {r}}},' for n, la, lo, r in towns]
out += ['};', 'constexpr int NMAP_TOWNS = sizeof MAP_TOWNS / sizeof *MAP_TOWNS;']
print('\n'.join(out))
print({k: len(v) for k, v in chunks.items()}, npts, 'points', len(towns), 'towns', file=sys.stderr)
