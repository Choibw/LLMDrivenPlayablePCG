#!/usr/bin/env python3
"""Grassland asset set (UE 5.6 PCG). Theme file = PALETTE + shape functions + SPECS + meshkit.run.

Run:  python3 generate_grassland_set.py  -> meshes/*.glb, preview.webp, textures_preview.webp
Shared kit: ../common/meshkit.py (geometry, textures, GLB writer, self-check, preview).
Theme-only helpers (textures: birch/granite/turf/leafcluster/flower; geometry: grow/leaf_cluster/granite_blob)
live here and register into mk.TEX_GENERATORS at import; meshkit.py is not modified.
"""
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
import meshkit as mk  # noqa: E402

UP = np.array([0.0, 1.0, 0.0])


# --------------------------------------------------------------------------
# Extra texture generators: fn(color, rng, n, **opts) -> (rgba uint8, height 0..1)
# --------------------------------------------------------------------------
def tex_birch(color, rng, n=mk.TEX_SIZE, lenticels=90):
    """White papery bark: faint vertical grain, dark horizontal lenticel dashes, grey patches."""
    U, V = mk._uv_grid(n)
    grain = mk.fbm(rng, n, cells=(18, 3), octaves=4)
    patch = mk.fbm(rng, n, cells=(3, 5), octaves=3)
    rgb = np.asarray(color) * (0.86 + 0.18 * grain)[..., None]
    h = 0.5 + 0.2 * grain
    mask = np.zeros((n, n), bool)
    for _ in range(lenticels):
        cx, cy = rng.random(), rng.random()
        w, t = rng.uniform(0.03, 0.14), rng.uniform(0.004, 0.011)
        dx = np.abs(((U - cx + 0.5) % 1) - 0.5)
        dy = np.abs(((V - cy + 0.5) % 1) - 0.5)
        mask |= (dx / w) ** 2 + (dy / t) ** 2 < 1
    rgb[mask] = np.array([0.16, 0.14, 0.12]) * (0.8 + 0.5 * grain[mask])[:, None]
    h[mask] -= 0.3
    g = patch > 0.7
    rgb[g] = rgb[g] * 0.6 + np.array([0.36, 0.34, 0.31]) * 0.4
    h[g] -= 0.1
    return mk._rgba(rgb), np.clip(h, 0, 1)


def tex_granite(color, rng, n=mk.TEX_SIZE, lichen=(0.60, 0.62, 0.38), lichen2=(0.50, 0.53, 0.45)):
    """Grey granite: calm large-scale tone drift + fine mineral speckle (sparse mica/quartz grains),
    faint feldspar warmth; two lichen tones as soft-edged sparse patches."""
    large = mk.fbm(rng, n, cells=(2, 2), octaves=4)
    medium = mk.fbm(rng, n, cells=(7, 7), octaves=3)
    speck = mk.fbm(rng, n, cells=(110, 110), octaves=1)
    lich = mk.fbm(rng, n, cells=(3, 3), octaves=3)
    shade = (0.88 + 0.24 * large) * (0.94 + 0.12 * medium) * (0.93 + 0.14 * speck)
    rgb = np.asarray(color) * shade[..., None]
    rgb[speck < 0.1] *= 0.72                                   # mica grains
    q = speck > 0.92
    rgb[q] = rgb[q] * 0.7 + np.array([0.9, 0.9, 0.88]) * 0.3   # quartz grains
    rgb *= (1 + 0.08 * (medium - 0.5)[..., None] * np.array([1.0, -0.3, -0.8]))  # feldspar warmth
    for thr, col in ((0.70, lichen2), (0.78, lichen)):
        mix = np.clip((lich - thr) * 8, 0, 1)[..., None] * 0.45
        rgb = rgb * (1 - mix) + np.asarray(col) * (0.85 + 0.3 * speck)[..., None] * mix
    h = 0.5 + 0.2 * (large - 0.5) + 0.15 * (medium - 0.5) + 0.12 * (speck - 0.5) + 0.06 * np.clip((lich - 0.70) * 8, 0, 1)
    return mk._rgba(rgb), np.clip(h, 0, 1)


def tex_turf(color, rng, n=mk.TEX_SIZE, color2=None):
    """Opaque moss/turf: layered fbm with warm clumps (color2) and fine grain."""
    base = mk.fbm(rng, n, cells=(12, 12), octaves=5)
    fine = mk.fbm(rng, n, cells=(40, 40), octaves=2)
    clump = mk.fbm(rng, n, cells=(4, 4), octaves=3)
    col = np.asarray(color)
    col2 = np.asarray(color2) if color2 is not None else col * np.array([1.1, 1.0, 0.6])
    mix = np.clip((clump - 0.45) * 3, 0, 1)[..., None]
    rgb = (col * (1 - mix) + col2 * mix) * (0.7 + 0.4 * base + 0.15 * fine)[..., None]
    return mk._rgba(rgb), np.clip(0.5 * base + 0.3 * fine + 0.2 * clump, 0, 1)


def tex_leafcluster(color, rng, n=mk.TEX_SIZE, leaves=26, lobes=0, size=(0.08, 0.13), color2=None,
                    blossom=None, blossoms=0, blossom_r=0.045):
    """Alpha card: scattered overlapping leaves (lobes>0 = lobed oak-like edge) with midrib and tint
    variation; optional 5-petal blossoms. Transparent texels keep the leaf color (no dark fringe)."""
    U, V = mk._uv_grid(n)
    col = np.asarray(color, float)
    col2 = np.asarray(color2, float) if color2 is not None else col * np.array([0.75, 0.85, 0.6])
    noise = mk.fbm(rng, n, cells=(10, 10), octaves=3)
    rgb = np.broadcast_to(col * 0.7, (n, n, 3)).copy()
    alpha = np.zeros((n, n), bool)
    h = np.full((n, n), 0.5)
    for _ in range(leaves):
        cx, cy = rng.uniform(0.14, 0.86), rng.uniform(0.12, 0.9)
        a = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(*size)
        W = L * rng.uniform(0.5, 0.7)
        x = (U - cx) * math.cos(a) + (V - cy) * math.sin(a)
        y = -(U - cx) * math.sin(a) + (V - cy) * math.cos(a)
        r = np.hypot(x / L, y / W)
        edge = 1 + 0.18 * np.cos(lobes * np.arctan2(y / W, x / L)) if lobes else np.ones_like(r)
        inside = r < edge
        t = rng.random()
        c = col * (1 - t) + col2 * t
        rgb[inside] = c * (0.8 + 0.4 * noise[inside])[:, None]
        rgb[inside & (np.abs(y) < 0.05 * W)] *= 0.85
        h[inside] = 0.45 + 0.35 * np.clip(1 - r[inside] / edge[inside], 0, 1)
        alpha |= inside
    for _ in range(blossoms):
        cx, cy = rng.uniform(0.12, 0.88), rng.uniform(0.15, 0.9)
        th = np.arctan2(V - cy, U - cx)
        r = np.hypot(U - cx, V - cy)
        pet = r < blossom_r * (0.6 + 0.4 * np.abs(np.cos(2.5 * th)))
        rgb[pet] = np.asarray(blossom) * (0.85 + 0.3 * noise[pet])[:, None]
        rgb[r < blossom_r * 0.2] = (0.95, 0.8, 0.25)
        h[pet] = 0.75
        alpha |= pet
    alpha &= (U > 0.02) & (U < 0.98) & (V > 0.02) & (V < 0.98)
    return mk._rgba(rgb, alpha.astype(float)), np.clip(h, 0, 1)


def tex_flower(color, rng, n=mk.TEX_SIZE, aspect=0.4, petals=8, petal=(0.96, 0.96, 0.9),
               disc=(0.95, 0.78, 0.2), head_r=0.17, stem_w=0.012):
    """Alpha card for a card of width aspect*length: green stem + 2 leaflets from v=0, flower head at v=0.8."""
    U, V = mk._uv_grid(n)
    x = (U - 0.5) * aspect
    noise = mk.fbm(rng, n, cells=(8, 8), octaves=3)
    stem = (np.abs(x) < stem_w * (1 - 0.4 * V)) & (V < 0.8)
    leaf = (np.hypot((x - 0.045) / 0.05, (V - 0.32) / 0.02) < 1) | (np.hypot((x + 0.045) / 0.05, (V - 0.5) / 0.02) < 1)
    dx, dy = x, V - 0.8
    th, r = np.arctan2(dy, dx), np.hypot(dx, dy)
    pet = r < head_r * (0.55 + 0.45 * np.abs(np.cos(petals / 2 * th)))
    dsk = r < head_r * 0.3
    rgb = np.asarray(color) * (0.8 + 0.4 * noise)[..., None]
    rgb[pet] = np.asarray(petal) * ((0.85 + 0.3 * noise) * (0.75 + 0.25 * r / head_r))[pet][:, None]
    rgb[dsk] = np.asarray(disc) * (0.8 + 0.4 * noise[dsk])[:, None]
    h = np.full((n, n), 0.5)
    h[pet] = 0.6
    h[dsk] = 0.8 + 0.2 * noise[dsk]
    return mk._rgba(rgb, (stem | leaf | pet).astype(float)), np.clip(h, 0, 1)


mk.TEX_GENERATORS.update({"birch": tex_birch, "granite": tex_granite, "turf": tex_turf,
                          "leafcluster": tex_leafcluster, "flower": tex_flower})

# --------------------------------------------------------------------------
# Palette: fresh greens, sunlit yellow-greens, grey granite, brown bark.
# --------------------------------------------------------------------------
PALETTE = {
    "bark_oak":     {"color": (0.40, 0.30, 0.20), "tex": "bark"},
    "bark_birch":   {"color": (0.90, 0.88, 0.82), "tex": "birch"},
    "bark_shrub":   {"color": (0.36, 0.30, 0.24), "tex": "bark"},
    "granite":      {"color": (0.56, 0.56, 0.53), "tex": "granite"},
    "moss":         {"color": (0.21, 0.33, 0.14), "tex": "turf", "color2": (0.30, 0.40, 0.16)},
    "turf":         {"color": (0.33, 0.52, 0.17), "tex": "turf", "color2": (0.60, 0.66, 0.24)},
    "leaf_oak":     {"color": (0.28, 0.47, 0.16), "tex": "leafcluster", "lobes": 6, "color2": (0.52, 0.62, 0.22)},
    "leaf_birch":   {"color": (0.50, 0.66, 0.22), "tex": "leafcluster", "leaves": 40, "size": (0.05, 0.08), "color2": (0.74, 0.76, 0.28)},
    "leaf_blossom": {"color": (0.30, 0.46, 0.18), "tex": "leafcluster", "leaves": 30, "size": (0.05, 0.09),
                     "blossom": (0.97, 0.90, 0.93), "blossoms": 14},
    "clover":       {"color": (0.28, 0.50, 0.20), "tex": "leafcluster", "leaves": 24, "lobes": 3, "size": (0.06, 0.09)},
    "blade":        {"color": (0.36, 0.58, 0.20), "tex": "blade", "blades": 16},
    "daisy":        {"color": (0.34, 0.52, 0.20), "tex": "flower", "petals": 8},
    "cornflower":   {"color": (0.34, 0.52, 0.20), "tex": "flower", "petals": 5, "petal": (0.45, 0.42, 0.82),
                     "disc": (0.30, 0.25, 0.50), "head_r": 0.13},
}


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------
def fit_height(m, h):
    """Uniformly scale the whole mesh so its top is at h (slot rules are exact, growth is random)."""
    top = max(c[0][:, :, 1].max() for c in m.chunks)
    m.chunks = [(t * (h / top), uv, mat) for t, uv, mat in m.chunks]
    return m


def grow(m, rng, p0, d, plan, depth, mat, tips):
    """Recursive branch: tube along a wobbling path, children from its upper half.
    plan[depth] = dict(len, r0, r1, sides, n, children, up, wobble, spread[, flare, flare_rings, child_from]).
    flare_rings: how many base rings the flare spans (default 1.5); child_from: fraction of the
    branch below which no child spawns (default 0.5; ~0.75 keeps a trunk bare up to its top limbs).
    tips collects (end point, direction, depth) of every branch."""
    P = plan[depth]
    n = P["n"]
    pts = [np.asarray(p0, float)]
    d = mk.unit(d)
    for _ in range(n - 1):
        d = mk.unit(d + rng.normal(0, P["wobble"], 3) + UP * P["up"])
        pts.append(pts[-1] + d * P["len"] / (n - 1))
    pts = np.array(pts)
    rad = np.linspace(P["r0"], P["r1"], n)
    mod = None
    if P.get("flare"):
        ph = rng.uniform(0, 2 * np.pi)
        fl, fr = P["flare"], P.get("flare_rings", 1.5)
        mod = lambda i, th: 1 + fl * max(0.0, 1 - i / fr) * (0.4 + 0.6 * np.abs(np.cos(2 * th + ph)))  # noqa: E731
    m.add(*mk.tube(pts, rad, sides=P["sides"], mod=mod), mat)
    tips.append((pts[-1], d, depth))
    if depth + 1 < len(plan):
        k_lo = max(1, int(round(n * P.get("child_from", 0.5))))
        for _ in range(P["children"]):
            k = int(rng.integers(min(k_lo, n - 1), n))
            t = mk.unit(pts[k] - pts[k - 1])
            side = mk.unit(np.cross(t, rng.normal(size=3)))
            cd = mk.unit(t * (1 - P["spread"]) + side * P["spread"])
            grow(m, rng, pts[k], cd, plan, depth + 1, mat, tips)


def leaf_cluster(m, rng, p, d, size, mat, cards=3, droop=0.35):
    """`cards` square alpha cards crossed around the branch axis at the branch tip, drooping."""
    d = mk.unit(d)
    horiz = mk.unit(np.cross(UP, d)) if abs(d[1]) < 0.98 else np.array([1.0, 0, 0])
    vert = np.cross(d, horiz)
    s = np.linspace(0, 1, 4)
    for k in range(cards):
        roll = math.pi * k / cards + rng.uniform(-0.3, 0.3)
        side = math.cos(roll) * horiz + math.sin(roll) * vert
        L = size * rng.uniform(0.85, 1.15)
        P = np.asarray(p, float) - d * L * 0.15 + d * (L * s)[:, None] - UP * (droop * L * s * s)[:, None]
        m.add(*mk.ribbon(P, L * np.array([0.55, 0.95, 1.0, 0.7]), side), mat)


def leafy_tree(seed, plan, mat_bark, mat_leaf, leaf_size, leaf_from, height, droop=0.35,
               stems=((0, (0.04, 1, 0.02)),), leaf_min=0.0, cards=3, end_cards=None):
    """stems: (azimuth_deg (unused when one stem), direction) per trunk; all share one plan.
    leaf_min: no leaves on tips below this fraction of the bare tree's height (keeps the trunk visible).
    end_cards: cards per cluster on the last branch depth (denser clumps at limb ends); default cards."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    tips = []
    for _, d in stems:
        grow(m, rng, np.zeros(3), np.asarray(d, float), plan, 0, mat_bark, tips)
    top = max(c[0][:, :, 1].max() for c in m.chunks)
    for p, d, depth in tips:
        if depth >= leaf_from and p[1] >= leaf_min * top:
            n_cards = end_cards if (end_cards and depth == len(plan) - 1) else cards
            leaf_cluster(m, rng, p, d, leaf_size, mat_leaf, cards=n_cards, droop=droop)
    return fit_height(m, height)


def granite_blob(rng, size, lat=20, lon=32, rough=0.11, sink=0.2):
    """fbm-displaced ellipsoid (no terraces: granite weathers round), sunk into the ground."""
    V, F, UV = mk.uv_sphere(lat, lon)
    d = np.ones(len(V))
    for k in range(6):
        W = rng.normal(size=3) * (1.5 + k)
        d += rough * (0.6 ** k) * np.sin(V @ W + rng.uniform(0, 2 * np.pi)) * 1.6
    d += rng.normal(0, 0.008, len(V))
    V = V * d[:, None] * np.asarray(size)
    V[:, 1] -= V[:, 1].min() + sink * 2 * size[1]
    UV = UV * np.array([2.0, 1.0])  # tileable: square texels around the blob
    return V, F, UV


def add_rock(m, V, F, UV, rng, moss=0.0):
    """Granite, with moss on upward faces (noise-broken) when moss > 0 (threshold on normal.y)."""
    if moss <= 0:
        m.add(V, F, UV, "granite")
        return
    T = V[F]
    fn = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    ny = fn[:, 1] / np.maximum(np.linalg.norm(fn, axis=1), 1e-9)
    c = T.mean(axis=1)
    noise = sum(np.sin(c @ (rng.normal(size=3) * f) + rng.uniform(0, 6.28)) for f in (1.5, 3.0))
    mat = np.where(ny + 0.04 * noise > moss, "moss", "granite").astype(object)
    m.add(V, F, UV, mat)


def _card(m, p, d, L, w, roll, mat, bend=0.3, n=4):
    """Single alpha card of length L, width w along direction d, bending down by `bend`."""
    d = mk.unit(d)
    s = np.linspace(0, 1, n)
    P = np.asarray(p, float) + d * (L * s)[:, None] - UP * (bend * L * s * s)[:, None]
    horiz = mk.unit(np.cross(UP, d)) if abs(d[1]) < 0.99 else np.array([1.0, 0, 0])
    side = math.cos(roll) * horiz + math.sin(roll) * np.cross(d, horiz)
    m.add(*mk.ribbon(P, np.full(n, w), side), mat)


def grass_clump(m, rng, cx, cz, h, cards=4):
    for k in range(cards):
        az = math.pi * k / cards + rng.uniform(-0.2, 0.2)
        s = np.linspace(0, 1, 4)
        lx, lz = rng.uniform(-0.12, 0.12, 2)
        P = np.column_stack([cx + lx * h * s * s, h * s, cz + lz * h * s * s])
        m.add(*mk.ribbon(P, np.full(4, h * 0.9), [math.cos(az), 0, math.sin(az)]), "blade")


def flower(m, rng, x, z, L, mat):
    """Two crossed stem+head cards (width 0.4 L, matching tex_flower aspect)."""
    s = np.linspace(0, 1, 3)
    lx, lz = rng.uniform(-0.15, 0.15, 2)
    P = np.column_stack([x + lx * L * s * s, L * s, z + lz * L * s * s])
    rot = rng.uniform(0, math.pi)
    for az in (rot, rot + math.pi / 2):
        m.add(*mk.ribbon(P, np.full(3, 0.4 * L), [math.cos(az), 0, math.sin(az)]), mat)


# --------------------------------------------------------------------------
# tree slot
# --------------------------------------------------------------------------
OAK_PLAN = [  # trunk bare to its top quarter, limbs rise then spread; leaves only above 30% height
    dict(len=1.6, r0=0.17, r1=0.12, sides=14, n=6, children=4, up=0.8, wobble=0.08, spread=0.75, flare=0.3, child_from=0.75),
    dict(len=1.2, r0=0.11, r1=0.06, sides=10, n=6, children=3, up=0.35, wobble=0.25, spread=0.75),
    dict(len=0.8, r0=0.055, r1=0.025, sides=7, n=5, children=4, up=0.2, wobble=0.3, spread=0.7),
    dict(len=0.5, r0=0.022, r1=0.008, sides=5, n=4, children=0, up=0.15, wobble=0.3, spread=0.6),
]
BIRCH_PLAN = [
    dict(len=2.4, r0=0.10, r1=0.05, sides=12, n=8, children=7, up=1.0, wobble=0.05, spread=0.7, flare=0.15),
    dict(len=1.0, r0=0.045, r1=0.018, sides=7, n=5, children=4, up=0.5, wobble=0.22, spread=0.7),
    dict(len=0.6, r0=0.016, r1=0.006, sides=5, n=4, children=0, up=-0.3, wobble=0.3, spread=0.6),
]
SHRUB_PLAN = [
    dict(len=1.1, r0=0.05, r1=0.03, sides=8, n=5, children=3, up=0.3, wobble=0.25, spread=0.7),
    dict(len=0.7, r0=0.028, r1=0.01, sides=5, n=4, children=3, up=0.15, wobble=0.3, spread=0.65),
    dict(len=0.45, r0=0.01, r1=0.004, sides=4, n=3, children=0, up=0.1, wobble=0.3, spread=0.6),
]
SHRUB_STEMS = [(az, (0.42 * math.cos(math.radians(az)), 1, 0.42 * math.sin(math.radians(az)))) for az in (10, 130, 250)]

# --------------------------------------------------------------------------
# big slot
# --------------------------------------------------------------------------
ANCIENT_OAK_PLAN = [  # lone-field oak: thick gnarled trunk bare to ~38% height, few thick spreading limbs, broad crown
    dict(len=3.6, r0=0.7, r1=0.45, sides=20, n=8, children=5, up=0.9, wobble=0.07, spread=0.8, flare=0.6, flare_rings=2.5, child_from=0.75),
    dict(len=3.2, r0=0.4, r1=0.2, sides=12, n=7, children=3, up=0.3, wobble=0.22, spread=0.75),
    dict(len=2.2, r0=0.18, r1=0.09, sides=8, n=6, children=3, up=0.15, wobble=0.3, spread=0.7),
    dict(len=1.4, r0=0.085, r1=0.035, sides=6, n=5, children=2, up=0.12, wobble=0.35, spread=0.65),
    dict(len=0.9, r0=0.032, r1=0.012, sides=5, n=4, children=0, up=0.1, wobble=0.35, spread=0.6),
]


def granite_tor(seed, height=6.2):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    slabs = [((4.6, 1.3, 3.4), (0, 0, 0), 0.25), ((3.6, 1.2, 2.8), (0.3, 1.6, -0.2), 0.0),
             ((2.9, 1.1, 2.3), (-0.3, 3.6, 0.3), 0.0), ((2.1, 1.0, 1.8), (0.2, 5.3, 0.0), 0.0)]
    for size, pos, sink in slabs:
        V, F, UV = granite_blob(rng, size, lat=22, lon=40, rough=0.1, sink=sink)
        add_rock(m, mk.place(V, rot_y=rng.uniform(0, 6.28), pos=pos), F, UV, rng, moss=0.82)
    for k in range(5):  # boulders around the foot
        V, F, UV = granite_blob(rng, (0.9, 0.6, 0.7), lat=14, lon=20, sink=0.25)
        a = 2 * math.pi * k / 5 + rng.uniform(-0.4, 0.4)
        r = rng.uniform(3.6, 4.2)
        add_rock(m, mk.place(V, scale=rng.uniform(0.7, 1.2), rot_y=rng.uniform(0, 6.28),
                             pos=(r * math.cos(a), 0, r * math.sin(a))), F, UV, rng, moss=0.84)
    return fit_height(m, height)


# --------------------------------------------------------------------------
# rock slot (small: 0.2-0.6 m)
# --------------------------------------------------------------------------
def granite_stone(seed, size, height, moss=0.0, sink=0.2):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    V, F, UV = granite_blob(rng, size, lat=24, lon=36, sink=sink)
    add_rock(m, V, F, UV, rng, moss=moss)
    return fit_height(m, height)


def pebble_cluster(seed, count=10, radius=0.3):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for k in range(count):
        s = rng.uniform(0.05, 0.12)
        V, F, UV = granite_blob(rng, (s * rng.uniform(1.0, 1.5), s * 0.75, s * rng.uniform(0.8, 1.2)),
                                lat=12, lon=16, sink=0.25)
        a = 2 * math.pi * k / count + rng.uniform(-0.3, 0.3)
        r = radius * rng.uniform(0.2, 1.0)
        add_rock(m, mk.place(V, rot_y=rng.uniform(0, 6.28), pos=(r * math.cos(a), 0, r * math.sin(a))),
                 F, UV, rng, moss=0.86 if k % 3 == 0 else 0.0)
    return fit_height(m, 0.22)


# --------------------------------------------------------------------------
# grass slot (alpha cards)
# --------------------------------------------------------------------------
def meadow_grass(seed, height=0.85):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for cx, cz, sc in ((0, 0, 1.0), (0.16, 0.08, 0.8), (-0.12, 0.12, 0.85), (0.04, -0.17, 0.7), (-0.15, -0.08, 0.75)):
        grass_clump(m, rng, cx, cz, height * sc)
    return m


def wildflower(seed, height=0.55):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for cx, cz, sc in ((0, 0, 0.6), (0.2, 0.1, 0.5), (-0.18, 0.1, 0.55), (0.05, -0.2, 0.5)):
        grass_clump(m, rng, cx, cz, height * sc, cards=3)
    for k in range(9):
        a = 2 * math.pi * k / 9 + rng.uniform(-0.3, 0.3)
        r = rng.uniform(0.05, 0.28)
        flower(m, rng, r * math.cos(a), r * math.sin(a), height * rng.uniform(0.75, 1.0),
               "cornflower" if k % 3 == 0 else "daisy")
    return m


# --------------------------------------------------------------------------
# ground slot
# --------------------------------------------------------------------------
def meadow_patch(seed, radius=1.5):
    rng = np.random.default_rng(seed)
    ph = rng.uniform(0, 2 * math.pi, 3)

    def h(x, z):
        r = math.hypot(x, z) / radius
        fall = max(0.0, 1 - r ** 3)
        bump = 0.5 + 0.5 * math.sin(2 * math.pi * x / 0.9 + ph[0]) * math.sin(2 * math.pi * z / 1.1 + ph[1]) \
            + 0.3 * math.sin(2 * math.pi * (x + z) / 0.5 + ph[2])
        return 0.03 * bump * fall + 0.004 * fall

    m = mk.Mesh()
    m.add(*mk.heightfield_disc(radius, h, rings=20, sides=48, uv_tiles=3.0), "turf")
    for _ in range(36):  # clover cards lying low
        r, a = radius * math.sqrt(rng.uniform(0, 0.85)), rng.uniform(0, 2 * math.pi)
        x, z = r * math.cos(a), r * math.sin(a)
        a2 = rng.uniform(0, 2 * math.pi)
        d = (0.9 * math.cos(a2), 0.4, 0.9 * math.sin(a2))
        L = rng.uniform(0.12, 0.18)
        for roll in (0.0, math.pi / 2):
            _card(m, (x, h(x, z), z), d, L, L, roll, "clover", bend=0.2)
    for k in range(40):  # tiny flowers
        r, a = radius * math.sqrt(rng.uniform(0, 0.85)), rng.uniform(0, 2 * math.pi)
        x, z = r * math.cos(a), r * math.sin(a)
        P0 = h(x, z)
        L = rng.uniform(0.07, 0.1)
        mat = "cornflower" if k % 4 == 0 else "daisy"
        s = np.linspace(0, 1, 3)
        P = np.column_stack([np.full(3, x), P0 + L * s, np.full(3, z)])
        rot = rng.uniform(0, math.pi)
        for az in (rot, rot + math.pi / 2):
            m.add(*mk.ribbon(P, np.full(3, 0.4 * L), [math.cos(az), 0, math.sin(az)]), mat)
    return m


# --------------------------------------------------------------------------
# Specs: name, slot, builder(seed), seed, tri budget
# --------------------------------------------------------------------------
SPECS = [
    mk.Spec("SM_Grassland_Oak_A", "tree", lambda s: leafy_tree(s, OAK_PLAN, "bark_oak", "leaf_oak", 0.6, 3, 3.8, leaf_min=0.3, cards=5), 1, 8000),
    mk.Spec("SM_Grassland_Birch_A", "tree", lambda s: leafy_tree(s, BIRCH_PLAN, "bark_birch", "leaf_birch", 0.5, 1, 3.9, droop=0.5), 2, 8000),
    mk.Spec("SM_Grassland_Hawthorn_A", "tree", lambda s: leafy_tree(s, SHRUB_PLAN, "bark_shrub", "leaf_blossom", 0.5, 1, 2.7, stems=SHRUB_STEMS), 3, 8000),
    mk.Spec("SM_Grassland_Stone_A", "rock", lambda s: granite_stone(s, (0.42, 0.26, 0.34), 0.40), 4, 8000),
    mk.Spec("SM_Grassland_Stone_B", "rock", lambda s: granite_stone(s, (0.60, 0.36, 0.42), 0.55, moss=0.76), 5, 8000),
    mk.Spec("SM_Grassland_PebbleCluster_A", "rock", lambda s: pebble_cluster(s), 6, 8000),
    mk.Spec("SM_Grassland_MeadowGrass_A", "grass", lambda s: meadow_grass(s), 7, 8000),
    mk.Spec("SM_Grassland_Wildflower_A", "grass", lambda s: wildflower(s), 8, 8000),
    mk.Spec("SM_Grassland_AncientOak_A", "big", lambda s: leafy_tree(s, ANCIENT_OAK_PLAN, "bark_oak", "leaf_oak", 1.4, 3, 11.0, leaf_min=0.32, cards=3, end_cards=6), 9, 20000),
    mk.Spec("SM_Grassland_GraniteTor_A", "big", lambda s: granite_tor(s), 10, 20000),
    mk.Spec("SM_Grassland_MeadowPatch_A", "ground", lambda s: meadow_patch(s), 11, 6000),
]

if __name__ == "__main__":
    mk.run("Grassland", PALETTE, SPECS, Path(__file__).resolve().parent)
