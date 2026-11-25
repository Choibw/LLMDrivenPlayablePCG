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
    "frost_twig":  {"color": (0.76, 0.74, 0.72), "tex": "twig", "depth": 5},
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


def _vnoise(rng, P, scale=2.5, octaves=4):
    """Cheap per-vertex 3D noise in about -0.5..0.5 (sum of random-direction sines, like blob())."""
    out = np.zeros(len(P))
    for k in range(octaves):
        W = rng.normal(size=3) * scale * (1.7 ** k)
        out += (0.55 ** k) * np.sin(P @ W + rng.uniform(0, 2 * np.pi))
    return out / (2 * sum(0.55 ** k for k in range(octaves)))


def snow_rock(rng, size, layers=4, lat=22, lon=32, depth=0.04, body="slate", rough=0.14, lip=0.6):
    """blob + snow pillow. Per-vertex snow score = sphere-normal.y + noise, so the cap edge wanders instead of
    stair-stepping along a lat ring; a face is snow when the mean score of its corners passes the threshold.
    Snow vertices lift by `depth`; vertices on the boundary bulge outward (thin overhang lip).
    `layers` kept for call-site symmetry (unused)."""
    V, F, UV, dirs = blob(rng, size, lat=lat, lon=lon, rough=rough)
    score = dirs[:, 1] + 0.45 * _vnoise(rng, dirs)
    up = np.clip((score - 0.15) / 0.6, 0, 1)
    V[:, 1] += depth * up * up * (3 - 2 * up)
    rim = np.exp(-((score - 0.35) / 0.12) ** 2)
    V += dirs * (lip * depth * rim)[:, None]
    snow = score[F].mean(axis=1) > 0.35
    return V, F, UV, np.where(snow, "snow", body).astype(object)


def snow_patch(rng, radius, height_fn, rings=16, sides=40, uv_tiles=1.0, warp=0.2):
    """Like mk.heightfield_disc but the outline is fbm-warped (radius varies with angle) and the height
    tapers as (1-t^2)^2 to exactly 0 at the rim (zero slope -> blends into the landscape). Planar UV."""
    amp = rng.normal(0, 1, 4)
    amp *= warp / np.abs(amp).sum()
    ph = rng.uniform(0, 2 * np.pi, 4)

    def R(theta):
        return radius * (1 + sum(a * math.sin((k + 2) * theta + p) for k, (a, p) in enumerate(zip(amp, ph))))

    V = [[0.0, height_fn(0.0, 0.0), 0.0]]
    for i in range(1, rings + 1):
        t = i / rings
        for j in range(sides):
            th = 2 * np.pi * j / sides
            x, z = t * R(th) * math.cos(th), t * R(th) * math.sin(th)
            V.append([x, height_fn(x, z) * (1 - t * t) ** 2, z])
    V = np.array(V)
    UV = (V[:, [0, 2]] / (2 * radius) + 0.5) * uv_tiles
    F = [[0, 1 + (j + 1) % sides, 1 + j] for j in range(sides)]
    for i in range(rings - 1):
        for j in range(sides):
            a, b = 1 + i * sides + j, 1 + i * sides + (j + 1) % sides
            F += [[a, b, b + sides], [a, b + sides, a + sides]]
    return V, np.array(F), UV


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
    m.add(*mk.ribbon(P + UP * snow, w * 0.72, horiz), "snowcap")  # snow pillow on the top surface


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
    """Birch with a full frosted crown: white trunk, forking branches to depth 4, four crossed frost-twig
    cards on every branch end from depth 2 up, and a snow clump card on each so the silhouette fills in."""
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
        if depth < 4:
            for k in (2, 4):
                out = mk.unit(np.array([pts[k][0], 0, pts[k][2]]) + rng.normal(0, 0.5, 3) + [0, 1.1, 0])
                branch(pts[k], out, length * 0.6, r1 * 1.1, r1 * 0.45, depth + 1, max(sides - 3, 4))
        if depth >= 2:
            tips.append((pts[-1], d, depth))
        return pts

    trunk = branch([0, 0, 0], [0.03, 1, 0.02], H * 0.45, 0.055 * H, 0.03 * H, 1, 14, up=0.9)
    for az in (30, 160, 280):
        d = [math.cos(math.radians(az)) * 0.5, 1.0, math.sin(math.radians(az)) * 0.5]
        branch(trunk[-1], d, H * 0.38, 0.028 * H, 0.009 * H, 1, 9)
    for p, d, depth in tips:
        L = (0.2 if depth >= 3 else 0.14) * H
        d = mk.unit(d + [0, 0.5, 0])
        s = np.linspace(0, 1, 4)
        P = p + d * (L * s)[:, None]
        horiz = mk.unit(np.cross(UP, d) if abs(d[1]) < 0.98 else [1, 0, 0])
        c = np.cross(d, horiz)
        w = np.full(4, L * 1.0)
        for side in (horiz, c, mk.unit(horiz + c), mk.unit(horiz - c)):
            m.add(*mk.ribbon(P, w, side), "frost_twig")
        m.add(*mk.ribbon(P[1:] + UP * 0.03, w[1:] * 0.55, horiz), "snowcap")
    return fit_height(m, H)


# --------------------------------------------------------------------------
# rock slot (small, snow-capped)
# --------------------------------------------------------------------------
def rock(seed, size, layers=4, depth=0.08):
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    m.add(*snow_rock(rng, size, layers=layers, depth=depth))
    return m


def _snow_lumps(m, rng, count, r_range, s_range, y=0.0, flat=0.3):
    """Small irregular snow lumps (fbm blobs, sunk) scattered in a ring r_range around the origin."""
    for _ in range(count):
        a, r, s = rng.uniform(0, 6.28), rng.uniform(*r_range), rng.uniform(*s_range)
        V, F, UV, _ = blob(rng, (s * rng.uniform(1.0, 1.4), s * 0.5, s * rng.uniform(0.8, 1.1)),
                           lat=8, lon=12, rough=0.18, sink=0.3, flat=flat)
        m.add(mk.place(V, rot_y=rng.uniform(0, 6.28), pos=(r * math.cos(a), y, r * math.sin(a))), F, UV, "snow")


def snow_mound(seed, radius=1.05, h0=0.27, tips=4):
    """Wide low soft snow mound (fbm outline, zero-slope rim) with slate tips poking out. Fills empty ground."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    p1, p2 = rng.uniform(0, 6.28, 2)

    def h(x, z):
        return h0 * (0.8 + 0.2 * math.sin(2.2 * x + p1) * math.cos(1.9 * z + p2))

    m.add(*snow_patch(rng, radius, h, rings=16, sides=44, uv_tiles=1.5, warp=0.18), "snow")
    for k in range(tips):
        a = 2 * math.pi * k / tips + rng.uniform(-0.5, 0.5)
        r = radius * rng.uniform(0.15, 0.5)
        x, z = r * math.cos(a), r * math.sin(a)
        y = h(x, z) * (1 - (r / radius) ** 2) ** 2  # tips sit well inside, so the mean radius is close enough
        s = rng.uniform(0.14, 0.2)
        V, F, UV, mat = snow_rock(rng, (s * rng.uniform(1.0, 1.4), s * 0.8, s * rng.uniform(0.8, 1.2)),
                                  lat=12, lon=18, depth=0.03)
        m.add(mk.place(V, rot_y=rng.uniform(0, 6.28), pos=(x, y - 0.6 * s, z)), F, UV, mat)
    return m


# --------------------------------------------------------------------------
# grass slot (no ground discs: PCG scatters these densely and discs read as polka dots)
# --------------------------------------------------------------------------
def frozen_grass(seed, height, radius=0.5, tufts=10):
    """Full clump of dry frosted grass (3 crossed cards per tuft, taller in the middle) with a few small
    irregular snow lumps hugging the stems."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    for k in range(tufts):
        a = 2 * math.pi * k / tufts + rng.uniform(-0.3, 0.3)
        r = radius * 0.72 * math.sqrt(rng.uniform(0.05, 1.0))
        cx, cz = r * math.cos(a), r * math.sin(a)
        h = height * rng.uniform(0.65, 1.0) * (1 - 0.3 * r / radius)
        lean = rng.uniform(-0.1, 0.1, 2)
        for j in range(3):
            az = math.pi * j / 3 + rng.uniform(-0.2, 0.2)
            P = np.array([[cx, 0.03, cz], [cx + lean[0] * 0.4, 0.03 + h * 0.5, cz + lean[1] * 0.4],
                          [cx + lean[0], 0.03 + h, cz + lean[1]]])
            m.add(*mk.ribbon(P, [h * 1.1] * 3, [math.cos(az), 0, math.sin(az)]), "frost_blade")
    _snow_lumps(m, rng, 3, (0.08, 0.28), (0.08, 0.12))
    return m


def juniper(seed, size):
    """Dense low snow-laden shrub: three rings of sprays (low outer, mid, steep inner core), every spray
    with a snow pillow card, and snow lumps weighing down the crown."""
    rng = np.random.default_rng(seed)
    m = mk.Mesh()
    m.add(*mk.tube([[0, 0, 0], [0, size * 0.25, 0]], [0.04, 0.03], sides=7), "bark")
    for count, e0, e1, l0, l1 in ((14, 10, 35, 0.75, 1.0), (10, 30, 60, 0.6, 0.85), (6, 55, 80, 0.65, 0.9)):
        for k in range(count):
            az = 2 * math.pi * k / count + rng.uniform(-0.3, 0.3)
            elev = math.radians(rng.uniform(e0, e1))
            d = np.array([math.cos(az) * math.cos(elev), math.sin(elev), math.sin(az) * math.cos(elev)])
            L = size * rng.uniform(l0, l1)
            stem = [[0, size * 0.15, 0], [0, size * 0.15, 0] + d * L * 0.45]
            m.add(*mk.tube(stem, [0.02, 0.01], sides=5), "bark")
            s = np.linspace(0, 1, 5)
            P = stem[1] + d * (L * 0.65 * s)[:, None] - UP * (0.25 * L * s * s)[:, None]
            horiz = mk.unit(np.cross(UP, d))
            T = np.gradient(P, axis=0)
            vert = np.cross(T, horiz)
            vert /= np.linalg.norm(vert, axis=1)[:, None]
            w = np.full(5, L * 0.7)
            m.add(*mk.ribbon(P, w, horiz), "juniper")
            m.add(*mk.ribbon(P, w * 0.7, vert), "juniper")
            m.add(*mk.ribbon(P + UP * 0.025, w * 0.7, horiz), "snowcap")
    _snow_lumps(m, rng, 4, (0.0, 0.22 * size), (0.14 * size, 0.2 * size), y=size * 0.72, flat=0.4)
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
def snow_drift(seed, radius=2.7):
    """Wind-sculpted drift: fbm-warped outline, soft undulation + gentle sastrugi ripples across the wind (x),
    height tapering to exactly 0 at the rim (snow_patch). Max height 0.06+0.05+0.035 = 0.145 m."""
    rng = np.random.default_rng(seed)
    p0, p1, p2 = rng.uniform(0, 6.28, 3)

    def h(x, z):
        ph = z / 0.7 + 0.2 * math.sin(2 * math.pi * x / 1.8 + p0)
        ripple = (1 - abs((ph % 1.0) * 2 - 1)) ** 2       # softened triangle wave
        mound = 0.5 + 0.5 * math.sin(0.9 * x + p1) * math.cos(0.6 * z + p2)
        return 0.06 + 0.05 * mound + 0.035 * ripple * (0.5 + 0.5 * mound)

    m = mk.Mesh()
    m.add(*snow_patch(rng, radius, h, rings=30, sides=64, uv_tiles=3.0, warp=0.15), "snow")
    return m


# --------------------------------------------------------------------------
# Specs: name, slot, builder(seed), seed, tri budget
# --------------------------------------------------------------------------
SPECS = [
    mk.Spec("SM_Snow_Spruce_A", "tree", lambda s: spruce(s, 3.85, tiers=12, per_tier=9, Lmax=1.3), 1, 8000),
    mk.Spec("SM_Snow_Spruce_B", "tree", lambda s: spruce(s, 3.0, tiers=10, per_tier=8, Lmax=1.15, droop=0.4), 2, 8000),
    mk.Spec("SM_Snow_Birch_A", "tree", lambda s: birch(s, 3.9), 3, 8000),
    mk.Spec("SM_Snow_Rock_A", "rock", lambda s: rock(s, (0.40, 0.24, 0.32), layers=3, depth=0.07), 4, 8000),
    mk.Spec("SM_Snow_Rock_B", "rock", lambda s: rock(s, (0.66, 0.43, 0.52), layers=4, depth=0.1), 5, 8000),
    mk.Spec("SM_Snow_RockCluster_A", "rock", lambda s: snow_mound(s), 6, 8000),
    mk.Spec("SM_Snow_FrozenGrass_A", "grass", lambda s: frozen_grass(s, 0.5), 7, 8000),
    mk.Spec("SM_Snow_Juniper_A", "grass", lambda s: juniper(s, 0.6), 8, 8000),
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


def check_tileable(h, tol=2.0):
    """Height field must wrap: seam difference no larger than tol x the interior neighbour difference."""
    inner = max(np.abs(h[1] - h[0]).mean(), np.abs(h[:, 1] - h[:, 0]).mean())
    seam = max(np.abs(h[0] - h[-1]).mean(), np.abs(h[:, 0] - h[:, -1]).mean())
    assert seam <= tol * inner, f"snow texture seam {seam:.4f} > {tol} x interior {inner:.4f}"


# In-engine review asked for a 5-6 m drift; the kit's ground rule is 2-4 m. Runtime override, kit untouched.
mk.SLOT_RULES["ground"] = (0.0, 0.15, 2.0, 6.5)

if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    _, h_snow = tex_snow(PALETTE["snow"]["color"], np.random.default_rng(1000))  # same seed as build_textures (key 0)
    check_tileable(h_snow)
    print("OK: M_Snow_snow baseColor/normal tile seamlessly")
    mk.run("Snow", PALETTE, SPECS, root)
    spire = root / "meshes" / "SM_Snow_IceSpire_A.glb"
    assert patch_roughness(spire, "M_Snow_ice", ICE_ROUGHNESS) == 1
    mk.verify_glb(spire, 20000)  # re-check after the JSON rewrite
    print(f"OK: M_Snow_ice roughness -> {ICE_ROUGHNESS}")
