#!/usr/bin/env python3
"""Snow asset set (UE 5.6 PCG). Theme file = PALETTE + shape functions + SPECS + meshkit.run.

Run:  python3 generate_snow_set.py  -> meshes/*.glb, preview.webp, textures_preview.webp
Shared kit: ../common/meshkit.py (geometry, textures, GLB writer, self-check, preview).
Theme-only helpers live here: snow / birch / ice / needle / snowcap textures, snow-capped rocks
(material split by face normal.y), height fitting, and a post-run roughness patch for the ice material.
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
import meshkit as mk  # noqa: E402

UP = np.array([0.0, 1.0, 0.0])
SNOW_SHADOW = np.array([0.60, 0.70, 0.88])  # blue tint in snow dips
ICE_ROUGHNESS = 0.18


# --------------------------------------------------------------------------
# Theme textures (registered into mk.TEX_GENERATORS at import; kit file untouched)
# --------------------------------------------------------------------------
def _snow_rgb(color, shade):
    """Blue-white snow: dips lean to SNOW_SHADOW, crests to `color`."""
    s = np.clip(shade, 0, 1)[..., None]
    return SNOW_SHADOW * (1 - s) + np.asarray(color) * s


def tex_snow(color, rng, n=mk.TEX_SIZE):
    """Wind-packed snow: soft undulation + fine grain + rare sparkles."""
    und = mk.fbm(rng, n, cells=(3, 3), octaves=3)
    grain = mk.fbm(rng, n, cells=(48, 48), octaves=3)
    U, V = mk._uv_grid(n)
    ripple = 0.5 + 0.5 * np.sin(2 * np.pi * (5 * V + 0.4 * und))
    shade = 0.55 + 0.3 * und + 0.15 * grain + 0.1 * ripple
    rgb = _snow_rgb(color, shade)
    sparkle = rng.random((n, n)) > 0.997
    rgb[sparkle] = 1.0
    return mk._rgba(rgb), np.clip(0.5 * und + 0.3 * grain + 0.2 * ripple, 0, 1)


def tex_birch(color, rng, n=mk.TEX_SIZE, dashes=70):
    """Birch bark: pale mottled base, dark horizontal lenticel dashes, a few darker peel patches."""
    U, V = mk._uv_grid(n)
    mottle = mk.fbm(rng, n, cells=(4, 6), octaves=4)
    grain = mk.fbm(rng, n, cells=(6, 40), octaves=3)
    shade = 0.8 + 0.3 * mottle + 0.1 * grain
    rgb = np.asarray(color) * shade[..., None]
    h = 0.5 + 0.3 * mottle + 0.1 * grain
    dark = np.zeros((n, n), bool)
    for _ in range(dashes):
        cu, cv = rng.random(), rng.random()
        L, w = rng.uniform(0.03, 0.14), rng.uniform(0.004, 0.012)
        du = np.minimum(np.abs(U - cu), 1 - np.abs(U - cu))
        dv = np.minimum(np.abs(V - cv), 1 - np.abs(V - cv))
        dark |= (du < L) & (dv < w * (1 - 0.6 * du / L))
    peel = mk.fbm(rng, n, cells=(3, 5), octaves=2) > 0.78
    rgb[peel] = rgb[peel] * 0.45 + np.array([0.28, 0.22, 0.18]) * 0.55
    rgb[dark] = np.array([0.12, 0.10, 0.09])
    h[dark] -= 0.3
    h[peel] -= 0.15
    return mk._rgba(rgb), np.clip(h, 0, 1)


def tex_ice(color, rng, n=mk.TEX_SIZE):
    """Glacier ice: blue-white mottling, thin dark cracks, faint layer bands."""
    U, V = mk._uv_grid(n)
    mottle = mk.fbm(rng, n, cells=(4, 4), octaves=5)
    bands = 0.5 + 0.5 * np.sin(2 * np.pi * (9 * V + 0.5 * mk.fbm(rng, n, cells=(2, 2), octaves=2)))
    crack = np.abs(mk.fbm(rng, n, cells=(3, 3), octaves=4) - 0.5) < 0.009
    crack |= np.abs(mk.fbm(rng, n, cells=(6, 6), octaves=3) - 0.5) < 0.005
    shade = 0.7 + 0.35 * mottle + 0.08 * bands
    rgb = np.asarray(color) * shade[..., None]
    rgb[mottle > 0.72] = rgb[mottle > 0.72] * 0.5 + 0.5  # white inclusions
    rgb[crack] = np.array([0.30, 0.45, 0.62])
    h = 0.5 + 0.35 * mottle + 0.15 * bands
    h[crack] -= 0.35
    return mk._rgba(rgb), np.clip(h, 0, 1)


def tex_needle(color, rng, n=mk.TEX_SIZE, twiglets=12, twig_color=(0.30, 0.22, 0.16), frost=0.0):
    """Alpha card: conifer branch, base v=0 at the trunk, tip v=1. Central twig at u=0.5, side twiglets
    angled toward the tip, needle fuzz from noise-modulated width. frost>0 whitens needle tips."""
    U, V = mk._uv_grid(n)
    s = U - 0.5
    noise = mk.fbm(rng, n, cells=(40, 40), octaves=3)
    alpha = (np.abs(s) < 0.006 + 0.012 * (1 - V)) & (V < 0.97)
    twig = alpha.copy()
    for i in range(twiglets):
        v0 = 0.05 + 0.9 * i / twiglets + rng.uniform(-0.015, 0.015)
        span = 0.46 * math.sin(math.pi * min(max((v0 + 0.08) / 1.05, 0), 1)) ** 0.6
        for sgn in (-1, 1):
            L = span * rng.uniform(0.8, 1.0)
            x0, y0, x1, y1 = 0.5, v0, 0.5 + sgn * L, v0 + 0.5 * L
            px, py = x1 - x0, y1 - y0
            t = np.clip(((U - x0) * px + (V - y0) * py) / max(px * px + py * py, 1e-9), 0, 1)
            dist = np.hypot(U - (x0 + t * px), V - (y0 + t * py))
            alpha |= dist < 0.05 * (1 - 0.5 * t) * (0.4 + 1.0 * noise)
            twig |= dist < 0.004 * (1 - 0.6 * t)
    n2 = mk.fbm(rng, n, cells=(12, 12), octaves=3)
    rgb = np.asarray(color) * (0.6 + 0.7 * n2 + 0.3 * noise - 0.15)[..., None]
    if frost:
        tip = np.clip((noise - 0.55) / 0.3, 0, 1) * frost
        rgb = rgb * (1 - tip[..., None]) + np.array([0.85, 0.9, 0.97]) * tip[..., None]
    rgb[twig] = np.asarray(twig_color)
    return mk._rgba(rgb, alpha.astype(float)), np.clip(0.4 + 0.4 * noise, 0, 1)


def tex_snowcap(color, rng, n=mk.TEX_SIZE):
    """Alpha card: elongated snow pillow following a branch card (base v=0), fbm-ragged rim."""
    U, V = mk._uv_grid(n)
    s = np.abs(U - 0.5)
    span = 0.34 * np.clip(np.sin(np.pi * np.clip((V + 0.05) / 1.02, 0, 1)), 0, 1) ** 0.7
    edge = mk.fbm(rng, n, cells=(6, 10), octaves=4)
    inside = s < span * (0.45 + 0.75 * edge)
    dome = np.clip(1 - (s / np.maximum(span, 1e-6)) ** 2, 0, 1)
    grain = mk.fbm(rng, n, cells=(40, 40), octaves=2)
    shade = 0.5 + 0.4 * dome + 0.15 * grain
    rgb = _snow_rgb(color, shade)
    return mk._rgba(rgb, inside.astype(float)), np.clip(0.2 + 0.6 * dome + 0.2 * grain, 0, 1)


mk.TEX_GENERATORS.update({"snow": tex_snow, "birch": tex_birch, "ice": tex_ice,
                          "needle": tex_needle, "snowcap": tex_snowcap})

# --------------------------------------------------------------------------
# Palette: key -> color + texture generator + options. One glTF material per key.
# --------------------------------------------------------------------------
PALETTE = {
    "snow":        {"color": (0.90, 0.93, 0.99), "tex": "snow"},
    "snowcap":     {"color": (0.92, 0.95, 1.00), "tex": "snowcap"},
    "bark":        {"color": (0.36, 0.29, 0.24), "tex": "bark"},
    "birch":       {"color": (0.82, 0.81, 0.78), "tex": "birch"},
    "slate":       {"color": (0.31, 0.33, 0.37), "tex": "rock", "color2": (0.22, 0.24, 0.27), "strata": 5},
    "ice":         {"color": (0.70, 0.84, 0.96), "tex": "ice"},
    "needle":      {"color": (0.11, 0.24, 0.14), "tex": "needle", "twiglets": 12},
    "juniper":     {"color": (0.26, 0.42, 0.33), "tex": "needle", "twiglets": 7, "frost": 0.6, "seed": 77},
    "frost_blade": {"color": (0.74, 0.68, 0.52), "tex": "blade"},
    "frost_twig":  {"color": (0.60, 0.58, 0.56), "tex": "twig"},
}


def _az(deg):
    return np.array([math.cos(math.radians(deg)), 0, math.sin(math.radians(deg))])


def fit_height(m, H):
    """Uniformly scale a finished Mesh so its top sits at H (random branches overshoot otherwise)."""
    top = max(c[0][:, :, 1].max() for c in m.chunks)
    k = H / top
    m.chunks = [(t * k, uv, mat) for t, uv, mat in m.chunks]
    return m


def _face_split(V, F, up_mat, side_mat, y_thresh=0.45, top_frac=0.5):
    """Per-face material: faces facing up (normal.y > y_thresh) in the upper part of the mesh
    (centroid y > top_frac * height) get up_mat (snow cap); lower ledges stay side_mat."""
    T = V[F]
    fn = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    fn /= np.maximum(np.linalg.norm(fn, axis=1)[:, None], 1e-12)
    cy = T[:, :, 1].mean(axis=1)
    hi = cy > V[:, 1].min() + top_frac * (V[:, 1].max() - V[:, 1].min())
    return np.where((fn[:, 1] > y_thresh) & hi, up_mat, side_mat).astype(object)


def blob(rng, size, lat=22, lon=32, rough=0.14, sink=0.2, flat=0.35):
    """fbm-displaced ellipsoid without terraces (slate rubble, not sandstone): flattened top, sunk into
    the ground. Returns (V, F, UV, dirs) with dirs = pre-displacement sphere normals."""
    V, F, UV = mk.uv_sphere(lat, lon)
    dirs = V.copy()
    d = np.ones(len(V))
    for k in range(6):
        W = rng.normal(size=3) * (1.5 + k)
        d += rough * (0.6 ** k) * np.sin(V @ W + rng.uniform(0, 2 * np.pi)) * 1.6
    d += rng.normal(0, 0.01, len(V))
    V = V * d[:, None] * np.asarray(size)
    V[:, 1] -= flat * np.clip(V[:, 1], 0, None)          # squash the crown
    V[:, 1] -= V[:, 1].min() + sink * 2 * size[1]
    return V, F, UV, dirs


def snow_rock(rng, size, layers=4, lat=22, lon=32, depth=0.04, body="slate", rough=0.14):
    """blob + snow pillow: vertices facing up are lifted by `depth`, upward faces on the upper half get
    the snow material (material split by face normal.y). `layers` kept for call-site symmetry (unused)."""
    V, F, UV, dirs = blob(rng, size, lat=lat, lon=lon, rough=rough)
    up = np.clip((dirs[:, 1] - 0.25) / 0.6, 0, 1)
    V[:, 1] += depth * up * up * (3 - 2 * up)
    return V, F, UV, _face_split(V, F, "snow", body, 0.4, 0.45)


# --------------------------------------------------------------------------
# tree slot
# --------------------------------------------------------------------------
def _spruce_branch(m, p0, d, L, droop, n=6, width=0.30, snow=0.03):
    s = np.linspace(0, 1, n)
    P = np.asarray(p0, float) + d * (L * s)[:, None] + UP * (L * (0.22 * s - droop * s * s))[:, None]
    horiz = mk.unit(np.cross(UP, d))
    T = np.gradient(P, axis=0)
    vert = np.cross(T, horiz)
    vert /= np.linalg.norm(vert, axis=1)[:, None]
    w = np.full(n, L * width)
    m.add(*mk.ribbon(P, w, horiz), "needle")          # flat needle layer (reads from above)
    m.add(*mk.ribbon(P, w * 0.5, vert), "needle")     # upright cross card (silhouette from the side)
    m.add(*mk.ribbon(P + UP * snow, w * 0.6, horiz), "snowcap")  # snow pillow on the top surface


def spruce(seed, H, tiers=12, per_tier=9, Lmax=1.25, droop=0.32, trunk_r=0.09, n=6):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    steps = 14
    t = np.linspace(0, 1, steps)
    wob = np.cumsum(rng.normal(0, 0.012, (steps, 3)), axis=0) * [1, 0, 1]
    path = np.column_stack([wob[:, 0], H * 0.96 * t, wob[:, 2]])
    rad = trunk_r * (1 - 0.9 * t) + 0.008
    m.add(*mk.tube(path, rad, sides=10, v_scale=1.5), "bark")

    def at(y):
        return np.array([np.interp(y, path[:, 1], path[:, 0]), y, np.interp(y, path[:, 1], path[:, 2])])

    for i in range(tiers):
        f = 0.2 + 0.72 * i / (tiers - 1)
        L = Lmax * (1 - f) ** 0.85 + 0.1
        k = per_tier if f < 0.65 else max(4, per_tier - 2)
        az0 = rng.uniform(0, 2 * math.pi)
        for j in range(k):
            az = az0 + 2 * math.pi * j / k + rng.normal(0, 0.14)
            d = np.array([math.cos(az), 0, math.sin(az)])
            y = H * f + rng.normal(0, 0.02 * H)
            _spruce_branch(m, at(y), d, L * rng.uniform(0.7, 1.15), droop * (1.1 - 0.5 * f), n=n,
                           snow=0.025 + 0.01 * L)
    top = at(H * 0.96)
    for az in (0, 60, 120):  # leader: 3 upright crossed needle cards + a cap
        P = np.array([top - [0, 0.12 * H, 0], top - [0, 0.04 * H, 0], top + [0, 0.045 * H, 0]])
        m.add(*mk.ribbon(P, [0.12 * H, 0.09 * H, 0.03 * H], _az(az)), "needle")
    m.add(*mk.ribbon(np.array([top + [-0.04 * H, 0.03 * H, 0], top + [0.04 * H, 0.035 * H, 0]]),
                     [0.06 * H, 0.06 * H], [0, 0, 1]), "snowcap")
    return fit_height(m, H)


def birch(seed, H):
    """Frosted bare birch: white trunk, upward forking branches, frosty twig cards at the tips."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    tips = []

    def branch(p0, d, length, r0, r1, depth, sides, up=0.3):
        pts = [np.asarray(p0, float)]
        d = mk.unit(d)
        steps = 5
        for _ in range(steps):
            d = mk.unit(d + rng.normal(0, 0.22, 3) + np.array([0, up, 0]))
            pts.append(pts[-1] + d * length / steps)
        m.add(*mk.tube(pts, np.linspace(r0, r1, steps + 1), sides=sides, v_scale=2.0), "birch")
        if depth < 3:
            for k in (2, 4):
                out = mk.unit(np.array([pts[k][0], 0, pts[k][2]]) + rng.normal(0, 0.5, 3) + [0, 1.1, 0])
                branch(pts[k], out, length * 0.55, r1 * 1.1, r1 * 0.4, depth + 1, max(sides - 3, 5))
        else:
            tips.append((pts[-1], d))
        return pts

    trunk = branch([0, 0, 0], [0.03, 1, 0.02], H * 0.5, 0.055 * H, 0.03 * H, 1, 14, up=0.9)
    for az in (30, 160, 280):
        d = [math.cos(math.radians(az)) * 0.5, 1.0, math.sin(math.radians(az)) * 0.5]
        branch(trunk[-1], d, H * 0.4, 0.028 * H, 0.009 * H, 1, 9)
    for p, d in tips:  # frosted twig cards, two crossed per tip
        L = 0.17 * H
        d = mk.unit(d + [0, 0.4, 0])
        s = np.linspace(0, 1, 4)
        P = p + d * (L * s)[:, None]
        horiz = mk.unit(np.cross(UP, d) if abs(d[1]) < 0.98 else [1, 0, 0])
        for side in (horiz, np.cross(d, horiz)):
            m.add(*mk.ribbon(P, np.full(4, L * 0.9), side), "frost_twig")
    return fit_height(m, H)


# --------------------------------------------------------------------------
# rock slot (small, snow-capped)
# --------------------------------------------------------------------------
def rock(seed, size, layers=4):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    m.add(*snow_rock(rng, size, layers=layers, depth=0.05))
    return m


def rock_cluster(seed, count=4, radius=0.3):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for k in range(count):
        s = rng.uniform(0.10, 0.17)
        V, F, UV, mat = snow_rock(rng, (s * rng.uniform(1.0, 1.5), s * 0.85, s * rng.uniform(0.8, 1.2)),
                                  layers=3, lat=14, lon=20, depth=0.035)
        a = 2 * math.pi * k / count + rng.uniform(-0.4, 0.4)
        r = radius * rng.uniform(0.3, 1.0)
        m.add(mk.place(V, rot_y=rng.uniform(0, 6.28), pos=(r * math.cos(a), 0, r * math.sin(a))), F, UV, mat)
    return m


# --------------------------------------------------------------------------
# grass slot
# --------------------------------------------------------------------------
def _mound(m, radius, h0, rng):
    ph = rng.uniform(0, 6.28)

    def h(x, z):
        r = math.hypot(x, z) / radius
        return h0 * max(0.0, 1 - r * r) * (0.85 + 0.15 * math.sin(3 * math.atan2(z, x) + ph))

    m.add(*mk.heightfield_disc(radius, h, rings=6, sides=18, uv_tiles=0.5), "snow")


def frozen_grass(seed, height):
    """Dry grass tufts poking through a low snow mound."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    _mound(m, 0.36, 0.06, rng)
    for cx, cz, sc in ((0, 0, 1.0), (0.14, 0.06, 0.72), (-0.1, -0.12, 0.8), (0.05, -0.16, 0.6)):
        h = height * sc
        lean = rng.uniform(-0.08, 0.08, 2)
        for k in range(3):
            az = math.pi * k / 3 + rng.uniform(-0.2, 0.2)
            P = np.array([[cx, 0.03, cz], [cx + lean[0] * 0.4, 0.03 + h * 0.5, cz + lean[1] * 0.4],
                          [cx + lean[0], 0.03 + h, cz + lean[1]]])
            m.add(*mk.ribbon(P, [h * 1.0] * 3, [math.cos(az), 0, math.sin(az)]), "frost_blade")
    return m


def juniper(seed, size):
    """Low snowy shrub: short woody stems, frosted juniper sprays, snow pillows on top."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    _mound(m, size * 0.7, 0.05, rng)
    m.add(*mk.tube([[0, 0, 0], [0, size * 0.2, 0]], [0.035, 0.025], sides=7), "bark")
    for k in range(9):
        az = 2 * math.pi * k / 9 + rng.uniform(-0.3, 0.3)
        elev = math.radians(rng.uniform(20, 60))
        d = np.array([math.cos(az) * math.cos(elev), math.sin(elev), math.sin(az) * math.cos(elev)])
        L = size * rng.uniform(0.6, 0.9)
        stem = [[0, size * 0.15, 0], [0, size * 0.15, 0] + d * L * 0.5]
        m.add(*mk.tube(stem, [0.018, 0.01], sides=5), "bark")
        s = np.linspace(0, 1, 5)
        P = stem[1] + d * (L * 0.6 * s)[:, None] - UP * (0.2 * L * s * s)[:, None]
        horiz = mk.unit(np.cross(UP, d))
        T = np.gradient(P, axis=0)
        vert = np.cross(T, horiz)
        vert /= np.linalg.norm(vert, axis=1)[:, None]
        w = np.full(5, L * 0.5)
        m.add(*mk.ribbon(P, w, horiz), "juniper")
        m.add(*mk.ribbon(P, w * 0.6, vert), "juniper")
        m.add(*mk.ribbon(P + UP * 0.02, w * 0.55, horiz), "snowcap")
    return m


# --------------------------------------------------------------------------
# big slot
# --------------------------------------------------------------------------
def ice_spire(seed, H, base=(1.35, 4.1, 1.1)):
    """Glacier spire: tall fbm blob tapered toward the top, ice body, snow on upward faces, slate at the foot."""
    rng = np.random.default_rng(seed)
    V, F, UV, _ = mk.rock_blob(rng, base, layers=7, lat=40, lon=56, rough=0.08)
    t = np.clip(V[:, 1] / V[:, 1].max(), 0, 1)
    V[:, [0, 2]] *= (1 - 0.42 * t ** 1.3)[:, None]
    V[:, 0] += 0.08 * V[:, 1]  # slight lean
    dirs, _, _ = mk.uv_sphere(40, 56)
    up = np.clip((dirs[:, 1] - 0.25) / 0.6, 0, 1)
    V[:, 1] += 0.1 * up * up * (3 - 2 * up)
    V[:, 1] *= H / V[:, 1].max()
    m = mk.Mesh()
    m.add(V, F, UV, _face_split(V, F, "snow", "ice", 0.5, 0.3))
    for k in range(5):  # slate boulders at the foot
        Vr, Fr, UVr, mat = snow_rock(rng, (0.55, 0.32, 0.45), layers=4, lat=14, lon=20, depth=0.08)
        a = 2 * math.pi * k / 5 + rng.uniform(-0.4, 0.4)
        d = base[0] * rng.uniform(1.0, 1.25)
        m.add(mk.place(Vr, scale=rng.uniform(0.6, 1.1), rot_y=rng.uniform(0, 6.28),
                       pos=(d * math.cos(a), 0, d * math.sin(a))), Fr, UVr, mat)
    return m


# --------------------------------------------------------------------------
# ground slot
# --------------------------------------------------------------------------
def snow_drift(seed, radius=1.6):
    """Wind-sculpted drift: soft mound + sharp-crested sastrugi ridges across the wind (x)."""
    rng = np.random.default_rng(seed)
    p0, p1, p2 = rng.uniform(0, 6.28, 3)

    def h(x, z):
        r = math.hypot(x, z) / radius
        fall = max(0.0, 1 - r ** 3)
        ph = z / 0.55 + 0.18 * math.sin(2 * math.pi * x / 1.3 + p0)
        tri = 1 - abs((ph % 1.0) * 2 - 1)           # triangle wave -> sharp crests
        mound = 0.5 + 0.5 * math.sin(1.4 * x + p1) * math.cos(0.9 * z + p2)
        return fall * (0.03 + 0.045 * mound + 0.065 * tri ** 1.6 * (0.5 + 0.5 * mound))

    m = mk.Mesh()
    m.add(*mk.heightfield_disc(radius, h, rings=26, sides=54, uv_tiles=2.0), "snow")
    return m


# --------------------------------------------------------------------------
# Specs: name, slot, builder(seed), seed, tri budget
# --------------------------------------------------------------------------
SPECS = [
    mk.Spec("SM_Snow_Spruce_A", "tree", lambda s: spruce(s, 3.85, tiers=12, per_tier=9, Lmax=1.3), 1, 8000),
    mk.Spec("SM_Snow_Spruce_B", "tree", lambda s: spruce(s, 3.0, tiers=10, per_tier=8, Lmax=1.15, droop=0.4), 2, 8000),
    mk.Spec("SM_Snow_Birch_A", "tree", lambda s: birch(s, 3.9), 3, 8000),
    mk.Spec("SM_Snow_Rock_A", "rock", lambda s: rock(s, (0.30, 0.17, 0.24), layers=3), 4, 8000),
    mk.Spec("SM_Snow_Rock_B", "rock", lambda s: rock(s, (0.42, 0.27, 0.33), layers=4), 5, 8000),
    mk.Spec("SM_Snow_RockCluster_A", "rock", lambda s: rock_cluster(s), 6, 8000),
    mk.Spec("SM_Snow_FrozenGrass_A", "grass", lambda s: frozen_grass(s, 0.42), 7, 8000),
    mk.Spec("SM_Snow_Juniper_A", "grass", lambda s: juniper(s, 0.65), 8, 8000),
    mk.Spec("SM_Snow_GiantFir_A", "big", lambda s: spruce(s, 11.5, tiers=20, per_tier=11, Lmax=2.7, droop=0.3, trunk_r=0.28, n=7), 9, 20000),
    mk.Spec("SM_Snow_IceSpire_A", "big", lambda s: ice_spire(s, 6.6), 10, 20000),
    mk.Spec("SM_Snow_SnowDrift_A", "ground", lambda s: snow_drift(s), 11, 6000),
]


def patch_roughness(path, mat_name, rough):
    """Rewrite one material's roughnessFactor inside a finished GLB (the kit writes a single global value)."""
    data = Path(path).read_bytes()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    hit = [m for m in gltf["materials"] if m["name"] == mat_name]
    for m in hit:
        m["pbrMetallicRoughness"]["roughnessFactor"] = rough
    js = mk._pad(json.dumps(gltf, separators=(",", ":")).encode(), b" ")
    rest = data[20 + jlen:]  # BIN chunk header + data, unchanged
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + len(rest)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js + rest)
    return len(hit)


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    mk.run("Snow", PALETTE, SPECS, root)
    spire = root / "meshes" / "SM_Snow_IceSpire_A.glb"
    assert patch_roughness(spire, "M_Snow_ice", ICE_ROUGHNESS) == 1
    mk.verify_glb(spire, 20000)  # re-check after the JSON rewrite
    print(f"OK: M_Snow_ice roughness -> {ICE_ROUGHNESS}")
