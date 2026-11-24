#!/usr/bin/env python3
"""Desert asset set (UE 5.6 PCG). Theme file = PALETTE + shape functions + SPECS + meshkit.run.

Run:  python3 generate_desert_set.py  -> meshes/*.glb, preview.webp, textures_preview.webp
Shared kit: ../common/meshkit.py (geometry, textures, GLB writer, self-check, preview).
"""
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
import meshkit as mk  # noqa: E402

# --------------------------------------------------------------------------
# Palette: key -> color + texture generator (mk.TEX_GENERATORS) + generator options.
# One glTF material per key. Alpha textures (frond/blade/twig) become MASK + doubleSided.
# --------------------------------------------------------------------------
PALETTE = {
    "sand":       {"color": (0.86, 0.74, 0.52), "tex": "sand"},
    "sandstone":  {"color": (0.72, 0.42, 0.28), "tex": "rock", "color2": (0.86, 0.74, 0.52), "strata": 6},
    "cactus":     {"color": (0.47, 0.56, 0.40), "tex": "cactus", "ribs": 10},
    "dry_wood":   {"color": (0.45, 0.33, 0.22), "tex": "bark"},
    "palm_trunk": {"color": (0.52, 0.42, 0.28), "tex": "bark", "rings": 6},
    "palm_frond": {"color": (0.36, 0.52, 0.30), "tex": "frond"},
    "straw":      {"color": (0.78, 0.68, 0.40), "tex": "blade"},
    "twig":       {"color": (0.66, 0.56, 0.34), "tex": "twig"},
}

UP = np.array([0.0, 1.0, 0.0])


def _az(deg):
    return np.array([math.cos(math.radians(deg)), 0, math.sin(math.radians(deg))])


# --------------------------------------------------------------------------
# tree slot
# --------------------------------------------------------------------------
def saguaro(seed, height, arm_specs):
    """arm_specs: list of (height_frac, azimuth_deg, out, up)."""
    m = mk.Mesh()
    R = 0.075 * height
    trunk_h = height - R
    pts, rad = mk.column(np.zeros(3), trunk_h, R)
    m.add(*mk.tube(pts, rad, sides=20, rib=10, rib_amp=0.07, v_scale=1.0), "cactus")
    for frac, az, out, up in arm_specs:
        d = _az(az)
        s = np.array([0, trunk_h * frac, 0])
        b = 0.35
        apts = [s, s + d * 0.5 * out - np.array([0, 0.04, 0]), s + d * out]
        Q = s + d * out + np.array([0, b, 0])
        for a in (0.5, 1.0, 1.4):
            apts.append(Q + b * (math.sin(a) * d - math.cos(a) * UP))
        cpts, crad = mk.column(Q + b * d, up, 0.65 * R, steps=6)
        apts += cpts[1:]
        arad = [0.65 * R] * len(apts[: -len(cpts) + 1]) + crad[1:]
        m.add(*mk.tube(apts, arad, sides=16, rib=10, rib_amp=0.07, v_scale=1.5), "cactus")
    return m


def dead_tree(seed, height):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()

    def branch(p0, d, length, r0, r1, depth, sides, up=0.12):
        pts = [np.asarray(p0, float)]
        d = mk.unit(d)
        steps = 5
        for _ in range(steps):
            d = mk.unit(d + rng.normal(0, 0.28, 3) + np.array([0, up, 0]))
            pts.append(pts[-1] + d * length / steps)
        m.add(*mk.tube(pts, np.linspace(r0, r1, steps + 1), sides=sides), "dry_wood")
        if depth < 2:
            for k in (2, 4):
                out = mk.unit(np.array([pts[k][0], 0, pts[k][2]]) + rng.normal(0, 0.4, 3) + [0, 0.9, 0])
                branch(pts[k], out, length * 0.55, r1 * 1.1, r1 * 0.35, depth + 1, max(sides - 3, 6))
        return pts

    trunk_h = height * 0.62
    trunk = branch([0, 0, 0], [0.05, 1, 0.02], trunk_h, 0.065 * height, 0.03 * height, 1, 14, up=0.7)
    for az in (20, 150, 270):
        d = [math.cos(math.radians(az)) * 0.7, 1.0, math.sin(math.radians(az)) * 0.7]
        branch(trunk[-1], d, height * 0.42, 0.03 * height, 0.008 * height, 1, 10)
    return m


# --------------------------------------------------------------------------
# rock slot
# --------------------------------------------------------------------------
def rock(seed, size, layers=5):
    rng = np.random.default_rng(seed)
    V, F, UV, _ = mk.rock_blob(rng, size, layers=layers)
    m = mk.Mesh()
    m.add(V, F, UV, "sandstone")
    return m


def pebble_cluster(seed, count=7, radius=0.36):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for k in range(count):
        s = rng.uniform(0.05, 0.11)
        V, F, UV, _ = mk.rock_blob(rng, (s * rng.uniform(1.0, 1.5), s * 0.8, s * rng.uniform(0.8, 1.2)),
                                   layers=3, lat=10, lon=14, sink=0.25)
        a = 2 * math.pi * k / count + rng.uniform(-0.3, 0.3)
        r = radius * rng.uniform(0.3, 1.0)
        m.add(mk.place(V, rot_y=rng.uniform(0, 6.28), pos=(r * math.cos(a), 0, r * math.sin(a))), F, UV, "sandstone")
    return m


# --------------------------------------------------------------------------
# big slot
# --------------------------------------------------------------------------
def mesa_large(seed, height, radius, layers=8):
    rng = np.random.default_rng(seed)
    pts, rad, phases = [], [], []
    r, y, lh = radius, 0.0, height / layers
    for _ in range(layers):
        for yy, rr in ((y, r), (y + lh - 0.08, r * 0.95)):
            pts.append([0, yy, 0]); rad.append(rr); phases.append(rng.uniform(0, 2 * np.pi, 3))
        y += lh
        r *= 0.88
    pts.append([0, height, 0]); rad.append(r * 0.9); phases.append(phases[-1])

    def mod(i, th):
        p1, p2, p3 = phases[i]
        return 1 + 0.07 * np.sin(3 * th + p1) + 0.04 * np.sin(7 * th + p2) + 0.025 * np.sin(13 * th + p3)

    m = mk.Mesh()
    m.add(*mk.tube(pts, rad, sides=48, mod=mod, v_scale=1.3 / height, u_tiles=4), "sandstone")
    for k in range(5):  # fallen boulders around the foot
        V, F, UV, _ = mk.rock_blob(rng, (0.9, 0.55, 0.7), layers=4, lat=12, lon=18)
        a = 2 * math.pi * k / 5 + rng.uniform(-0.4, 0.4)
        d = radius * rng.uniform(1.0, 1.15)
        m.add(mk.place(V, scale=rng.uniform(0.7, 1.3), rot_y=rng.uniform(0, 6.28),
                       pos=(d * math.cos(a), 0, d * math.sin(a))), F, UV, "sandstone")
    return m


def date_palm(seed, height, fronds=13, frond_len=3.2):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    trunk_h = height - 1.5
    n = 26
    t = np.linspace(0, 1, n)
    path = np.column_stack([0.55 * t ** 1.6, trunk_h * t, 0.15 * t ** 2])  # slight lean
    rad = 0.3 - 0.09 * t
    m.add(*mk.tube(path, rad, sides=16, mod=lambda i, th: 1.0 if i % 2 else 0.92), "palm_trunk")  # ringed
    top = path[-1]
    V, F, UV = mk.uv_sphere(8, 12)
    m.add(mk.place(V, scale=(0.4, 0.5, 0.4), pos=top + [0, 0.05, 0]), F, UV, "palm_trunk")  # crown base
    for k in range(fronds):
        az = 360 * k / fronds + rng.uniform(-10, 10)
        elev = math.radians(rng.uniform(-15, 70))
        d = _az(az)
        L = frond_len * rng.uniform(0.85, 1.1)
        s = np.linspace(0, 1, 10)
        P = top + [0, 0.3, 0] + d * (L * math.cos(elev) * s * (1 + 0.1 * s))[:, None] \
            + UP * (L * (math.sin(elev) * s - 0.5 * s * s))[:, None]
        horiz = np.cross(UP, d)
        T = np.gradient(P, axis=0)
        vert = np.cross(T, horiz)
        vert /= np.linalg.norm(vert, axis=1)[:, None]
        widths = np.full(10, 1.3)
        m.add(*mk.ribbon(P, widths, horiz), "palm_frond")   # crossed cards: flat + upright
        m.add(*mk.ribbon(P, widths * 0.8, vert), "palm_frond")
    return m


# --------------------------------------------------------------------------
# grass slot (alpha cards)
# --------------------------------------------------------------------------
def _card(m, p0, direction, length, width, roll, mat, bend=0.25, n=5):
    d = mk.unit(direction)
    s = np.linspace(0, 1, n)
    P = np.asarray(p0, float) + d * (length * s)[:, None] - UP * (bend * length * s * s)[:, None]
    horiz = mk.unit(np.cross(UP, d) if abs(d[1]) < 0.99 else [1, 0, 0])
    side = math.cos(roll) * horiz + math.sin(roll) * np.cross(d, horiz)
    m.add(*mk.ribbon(P, np.full(n, width), side), mat)


def dry_shrub(seed, size):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    m.add(*mk.tube([[0, 0, 0], [0, size * 0.25, 0]], [0.03, 0.02], sides=6), "dry_wood")
    for k in range(14):
        az = 2 * math.pi * k / 14 + rng.uniform(-0.3, 0.3)
        elev = math.radians(rng.uniform(25, 75))
        d = [math.cos(az) * math.cos(elev), math.sin(elev), math.sin(az) * math.cos(elev)]
        _card(m, [0, 0.05, 0], d, size * rng.uniform(0.9, 1.3), size * 0.9, rng.uniform(-0.6, 0.6), "twig", bend=0.15)
    return m


def grass_tuft(seed, height):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for cx, cz, sc in ((0, 0, 1.0), (0.12, 0.05, 0.75), (-0.08, -0.1, 0.8)):
        h = height * sc
        for k in range(3):  # three cards crossed at 60 deg
            az = math.pi * k / 3 + rng.uniform(-0.2, 0.2)
            P = np.array([[cx, 0, cz], [cx, h * 0.5, cz], [cx + 0.03 * sc, h, cz]])
            m.add(*mk.ribbon(P, [h * 1.1] * 3, [math.cos(az), 0, math.sin(az)]), "straw")
    return m


# --------------------------------------------------------------------------
# ground slot
# --------------------------------------------------------------------------
def sand_ripple(seed, radius=1.5, amp=0.045):
    rng = np.random.default_rng(seed)
    ph = rng.uniform(0, 6.28)

    def h(x, z):
        r = math.hypot(x, z) / radius
        fall = max(0.0, 1 - r ** 4)
        wave = 0.5 + 0.5 * math.sin(2 * math.pi * (x / 0.45 + 0.25 * math.sin(2 * math.pi * z / 1.4 + ph)))
        return amp * wave * fall + 0.01 * fall

    m = mk.Mesh()
    m.add(*mk.heightfield_disc(radius, h, rings=24, sides=48, uv_tiles=2.0), "sand")
    return m


# --------------------------------------------------------------------------
# Specs: name, slot, builder(seed), seed, tri budget
# --------------------------------------------------------------------------
SPECS = [
    mk.Spec("SM_Desert_Saguaro_A", "tree", lambda s: saguaro(s, 3.6, [(0.45, 20, 0.35, 1.3), (0.55, 200, 0.3, 0.9)]), 1, 8000),
    mk.Spec("SM_Desert_Saguaro_B", "tree", lambda s: saguaro(s, 2.8, [(0.5, 110, 0.3, 0.7), (0.4, 250, 0.32, 1.0), (0.62, 340, 0.25, 0.4)]), 2, 8000),
    mk.Spec("SM_Desert_DeadTree_A", "tree", lambda s: dead_tree(s, 3.0), 3, 8000),
    mk.Spec("SM_Desert_Rock_A", "rock", lambda s: rock(s, (0.38, 0.20, 0.30), layers=4), 4, 8000),
    mk.Spec("SM_Desert_Rock_B", "rock", lambda s: rock(s, (0.56, 0.31, 0.45), layers=5), 5, 8000),
    mk.Spec("SM_Desert_PebbleCluster_A", "rock", lambda s: pebble_cluster(s), 6, 8000),
    mk.Spec("SM_Desert_DryShrub_A", "grass", lambda s: dry_shrub(s, 0.6), 7, 8000),
    mk.Spec("SM_Desert_GrassTuft_A", "grass", lambda s: grass_tuft(s, 0.46), 8, 8000),
    mk.Spec("SM_Desert_Mesa_Large", "big", lambda s: mesa_large(s, 7.0, 4.0), 9, 20000),
    mk.Spec("SM_Desert_DatePalm_A", "big", lambda s: date_palm(s, 9.0), 10, 20000),
    mk.Spec("SM_Desert_SandRipple_A", "ground", lambda s: sand_ripple(s), 11, 6000),
]

if __name__ == "__main__":
    mk.run("Desert", PALETTE, SPECS, Path(__file__).resolve().parent)
