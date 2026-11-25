"""Theme-independent kit for the procedural asset sets (UE 5.6 PCG).

A theme script (assets/<theme>/generate_<theme>_set.py) only defines PALETTE,
shape functions, SPECS and calls ``run``:

    import sys; from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
    import meshkit as mk
    PALETTE = {"bark": {"color": (0.45, 0.33, 0.22), "tex": "bark"}, ...}   # see TEX_GENERATORS
    SPECS = [mk.Spec("SM_Desert_Rock_A", "rock", lambda seed: rock(seed, ...), seed=4, tris=8000), ...]
    mk.run("Desert", PALETTE, SPECS, Path(__file__).resolve().parent)

Conventions: meters, glTF Y-up, pivot at base center (y=0).
Per palette key one material ``M_<Theme>_<key>`` with an embedded 512x512 baseColor
PNG + tangent-space normal map PNG (generated here, no loose image files).
Shading: per-vertex normals averaged below CREASE_DEG, hard edges above.
Foliage: alpha-cutout cards (``ribbon`` + a "frond"/"blade"/"twig" texture) ->
alphaMode MASK, doubleSided. A material is MASK iff its texture has alpha < 255.
Set rules (11 meshes): SLOT_RULES / SLOT_COUNT.
"""
import io
import json
import math
import struct
from collections import Counter, namedtuple
from pathlib import Path

import numpy as np
from PIL import Image

ROUGHNESS = 0.9
TEX_SIZE = 512
CREASE_DEG = 50.0

# slot -> (height_min, height_max, width_min, width_max); None = unchecked.
# big: inherently large species/formations, 6-12 m tall OR 6-12 m wide.
# ground: flat cover patch, 2-4 m wide, <=0.15 m tall, sits on y=0.
SLOT_RULES = {
    "tree": (2.5, 4.0, None, None),
    "rock": (0.15, 1.5, None, None),
    "grass": (0.2, 1.0, None, None),
    "big": (None, None, None, None),  # special-cased in run()
    "ground": (0.0, 0.15, 2.0, 4.0),
}
SLOT_COUNT = {"tree": 3, "rock": 3, "grass": 2, "big": 2, "ground": 1}
SLOT_TRIS = {"tree": 8000, "rock": 8000, "grass": 8000, "big": 20000, "ground": 6000}
BIG_MIN, BIG_MAX = 6.0, 12.0

# name: file stem (SM_<Theme>_<Name>); build: callable(seed) -> Mesh; tris: budget.
Spec = namedtuple("Spec", "name slot build seed tris")


# --------------------------------------------------------------------------
# Mesh container: independent triangles with per-corner UVs (no index sharing)
# --------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.chunks = []  # (tris (M,3,3), uvs (M,3,2), material names (M,))

    def add(self, V, F, uv, mat):
        """uv: per-vertex (N,2) or per-corner (M,3,2). mat: palette key or per-face array."""
        V = np.asarray(V, float)
        F = np.asarray(F, int)
        uv = np.asarray(uv, float)
        uvc = uv if uv.ndim == 3 else uv[F]
        if isinstance(mat, str):
            mat = np.full(len(F), mat, dtype=object)
        self.chunks.append((V[F], uvc, np.asarray(mat, dtype=object)))

    def merge(self, other):
        self.chunks += other.chunks

    def finalize(self):
        tris = np.concatenate([c[0] for c in self.chunks])
        uvs = np.concatenate([c[1] for c in self.chunks])
        mats = np.concatenate([c[2] for c in self.chunks])
        tris[:, :, 1] = np.maximum(tris[:, :, 1], 0.0)  # ground clamp (y>=0)
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        keep = np.linalg.norm(n, axis=1) > 1e-9        # drop degenerate faces
        return tris[keep], uvs[keep], mats[keep]


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def place(V, scale=1.0, rot_y=0.0, pos=(0, 0, 0)):
    """Scale (scalar or xyz), rotate about Y (radians), then translate."""
    c, s = math.cos(rot_y), math.sin(rot_y)
    R = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return (np.asarray(V, float) * np.asarray(scale, float)) @ R.T + np.asarray(pos, float)


def smooth_normals(tris, crease_deg=CREASE_DEG):
    """Per-corner normals: average of incident face normals within crease angle."""
    fn = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    fn /= np.linalg.norm(fn, axis=1)[:, None]
    corners = tris.reshape(-1, 3)
    key = np.round(corners * 1e5).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.ravel()
    order = np.argsort(inv, kind="stable")
    sizes = np.bincount(inv)
    G, S = len(sizes), sizes.max()
    starts = np.concatenate([[0], np.cumsum(sizes)[:-1]])
    slot = np.arange(len(inv)) - starts[inv[order]]  # position within group
    members = -np.ones((G, S), dtype=np.int64)
    members[inv[order], slot] = order
    valid = members >= 0
    N = np.where(valid[..., None], fn[np.maximum(members, 0) // 3], 0.0)  # (G,S,3)
    D = np.einsum("gsk,gtk->gst", N, N)
    W = (D > math.cos(math.radians(crease_deg))) & valid[:, None, :] & valid[:, :, None]
    out = np.einsum("gst,gtk->gsk", W.astype(float), N)
    res = np.empty_like(corners)
    res[members[valid]] = out[valid]
    res /= np.maximum(np.linalg.norm(res, axis=1)[:, None], 1e-12)
    return res.reshape(-1, 3, 3)


# --------------------------------------------------------------------------
# Primitive builders: return (V, F, UV) with outward winding
# --------------------------------------------------------------------------
def _fix_wrap(F, uv, sides, nring, u_tiles=1.0):
    """Per-corner UVs for a ring-wrapped mesh: faces crossing the seam get u=u_tiles on their j=0 corners."""
    uvc = uv[F].copy()
    j = np.where(F < nring, F % sides, -1)
    wrap = (j.max(axis=1) == sides - 1) & ((j == 0).any(axis=1))
    sel = wrap[:, None] & (j == 0)
    uvc[sel, 0] = u_tiles
    return uvc


def tube(path, radii, sides=8, aspect=1.0, rib=0, rib_amp=0.0, mod=None, cap=True, v_scale=None, u_tiles=1.0):
    """Sweep a (ribbed) circle along a polyline. mod(i, theta) scales ring i.
    UV: u around (0..u_tiles), v = arc length / circumference (square texels); v_scale (per meter) overrides."""
    P = np.asarray(path, float)
    n = len(P)
    T = np.zeros_like(P)
    T[1:-1] = P[2:] - P[:-2]
    T[0] = P[1] - P[0]
    T[-1] = P[-1] - P[-2]
    T /= np.linalg.norm(T, axis=1)[:, None]
    ref = np.array([1.0, 0, 0]) if abs(T[0][0]) < 0.9 else np.array([0, 0, 1.0])
    N = unit(np.cross(T[0], ref))
    th = np.linspace(0, 2 * np.pi, sides, endpoint=False)
    rings = []
    for i in range(n):
        if i > 0:  # parallel transport the frame
            N = unit(N - T[i] * np.dot(N, T[i]))
        B = np.cross(T[i], N)
        r = radii[i] * (1 + rib_amp * np.cos(rib * th)) if rib else np.full(sides, radii[i])
        if mod is not None:
            r = r * mod(i, th)
        rings.append(P[i] + (np.cos(th) * r)[:, None] * N + (np.sin(th) * r * aspect)[:, None] * B)
    V = np.vstack(rings)
    arc = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))])
    vs = v_scale if v_scale is not None else 1.0 / max(2 * np.pi * float(np.mean(radii)), 1e-6)
    UV = np.column_stack([np.tile(th / (2 * np.pi) * u_tiles, n), np.repeat(arc * vs, sides)])
    F = []
    for i in range(n - 1):
        for j in range(sides):
            a, b = i * sides + j, i * sides + (j + 1) % sides
            F += [[a, b, b + sides], [a, b + sides, a + sides]]
    if cap:
        c0 = len(V)
        V = np.vstack([V, P[0]])
        UV = np.vstack([UV, [0.5, 0.0]])
        F += [[c0, (j + 1) % sides, j] for j in range(sides)]
        c1 = len(V)
        V = np.vstack([V, P[-1]])
        UV = np.vstack([UV, [0.5, arc[-1] * vs]])
        base = (n - 1) * sides
        F += [[c1, base + j, base + (j + 1) % sides] for j in range(sides)]
    F = np.array(F)
    return V, F, _fix_wrap(F, UV, sides, n * sides, u_tiles)


def uv_sphere(lat=10, lon=14, v_tiles=1.0):
    """Unit sphere centered at origin; spherical UV (u around, v pole to pole)."""
    V, UV = [], []
    for i in range(1, lat):
        phi = np.pi * i / lat
        for j in range(lon):
            t = 2 * np.pi * j / lon
            V.append([np.sin(phi) * np.cos(t), np.cos(phi), np.sin(phi) * np.sin(t)])
            UV.append([j / lon, v_tiles * i / lat])
    top = len(V)
    V.append([0, 1.0, 0]); UV.append([0.5, 0.0])
    bot = len(V)
    V.append([0, -1.0, 0]); UV.append([0.5, v_tiles])
    F = []
    for i in range(lat - 2):
        for j in range(lon):
            a, b = i * lon + j, i * lon + (j + 1) % lon
            F += [[a, b, b + lon], [a, b + lon, a + lon]]
    last = (lat - 2) * lon
    for j in range(lon):
        F.append([top, (j + 1) % lon, j])
        F.append([bot, last + j, last + (j + 1) % lon])
    F = np.array(F)
    return np.array(V), F, _fix_wrap(F, np.array(UV), lon, (lat - 1) * lon)


def column(start, up_len, R, dome=True, steps=10):
    """Path+radii of a vertical column (slight belly) with a rounded top. Feed to tube()."""
    pts, rad = [], []
    for t in np.linspace(0, 1, steps):
        pts.append(start + np.array([0, up_len * t, 0]))
        rad.append(R * (1 + 0.12 * t * (1 - t)))
    if dome:
        for a in (0.3, 0.6, 0.9, 1.15, 1.35, 1.52):
            pts.append(start + np.array([0, up_len + R * math.sin(a), 0]))
            rad.append(R * math.cos(a) + 0.01)
    return pts, rad


def rock_blob(rng, size, layers=5, sink=0.18, lat=16, lon=24, rough=0.1):
    """fbm-displaced, terraced, ground-sunk ellipsoid. size = (sx, sy, sz) half-extents.
    Returns (V, F, UV, band): band[face] = terrace index for striped materials."""
    V, F, UV = uv_sphere(lat, lon, v_tiles=1.0)
    d = np.ones(len(V))
    for k in range(6):  # fbm-like: 6 sine octaves, random directions
        W = rng.normal(size=3) * (1.5 + k)
        d += rough * (0.6 ** k) * np.sin(V @ W + rng.uniform(0, 2 * np.pi)) * 1.6
    d += rng.normal(0, 0.012, len(V))
    V = V * d[:, None] * np.asarray(size)
    step = 2 * size[1] / layers
    V[:, 1] += 0.7 * (np.round(V[:, 1] / step) * step - V[:, 1])  # terraces
    V[:, 1] -= V[:, 1].min() + sink * 2 * size[1]                # sink into ground
    cy = V[F][:, :, 1].mean(axis=1)
    band = np.floor(cy / step + 0.5).astype(int)
    return V, F, UV, band


def heightfield_disc(radius, height_fn, rings=14, sides=32, uv_tiles=1.0):
    """Disc of radius `radius` on y=0, top surface y = height_fn(x, z) (0 at the rim).
    Planar UV (uv_tiles repeats across the diameter). Upward winding. For ground patches."""
    V = [[0.0, height_fn(0.0, 0.0), 0.0]]
    for i in range(1, rings + 1):
        r = radius * i / rings
        for j in range(sides):
            t = 2 * np.pi * j / sides
            x, z = r * math.cos(t), r * math.sin(t)
            V.append([x, height_fn(x, z), z])
    V = np.array(V)
    UV = (V[:, [0, 2]] / (2 * radius) + 0.5) * uv_tiles
    F = []
    for j in range(sides):  # fan
        F.append([0, 1 + (j + 1) % sides, 1 + j])
    for i in range(rings - 1):
        for j in range(sides):
            a, b = 1 + i * sides + j, 1 + i * sides + (j + 1) % sides
            F += [[a, b, b + sides], [a, b + sides, a + sides]]
    return V, np.array(F), UV


def ribbon(path, widths, side):
    """Flat card strip along a polyline: width across `side` (3,) or (n,3). UV u across, v along 0..1.
    Use with an alpha "frond"/"blade"/"twig" texture; material becomes MASK + doubleSided."""
    P = np.asarray(path, float)
    n = len(P)
    S = np.broadcast_to(np.asarray(side, float), (n, 3))
    S = S / np.linalg.norm(S, axis=1)[:, None]
    w = np.asarray(widths, float)[:, None] / 2
    V = np.empty((2 * n, 3))
    V[0::2] = P - S * w
    V[1::2] = P + S * w
    t = np.linspace(0, 1, n)
    UV = np.column_stack([np.tile([0.0, 1.0], n), np.repeat(t, 2)])
    F = []
    for i in range(n - 1):
        a = 2 * i
        F += [[a, a + 1, a + 3], [a, a + 3, a + 2]]
    return V, np.array(F), UV


# --------------------------------------------------------------------------
# Procedural textures. Generators: fn(color, rng, n=TEX_SIZE, **opts) -> (rgba uint8 (n,n,4), height float (n,n) 0..1)
# Image row 0 == v=0 (base of a tube / base of a card).
# --------------------------------------------------------------------------
def _lattice(rng, n, cx, cy):
    g = rng.random((cy, cx))

    def coords(c):
        t = np.arange(n) * c / n
        i0 = np.floor(t).astype(int)
        f = t - i0
        f = f * f * (3 - 2 * f)
        return i0 % c, (i0 + 1) % c, f

    x0, x1, fx = coords(cx)
    y0, y1, fy = coords(cy)
    fx, fy = fx[None, :], fy[:, None]
    return (g[np.ix_(y0, x0)] * (1 - fx) * (1 - fy) + g[np.ix_(y0, x1)] * fx * (1 - fy)
            + g[np.ix_(y1, x0)] * (1 - fx) * fy + g[np.ix_(y1, x1)] * fx * fy)


def fbm(rng, n=TEX_SIZE, cells=(4, 4), octaves=5, gain=0.5):
    """Tileable value-noise fbm in 0..1. cells=(x, y) lattice of the first octave (anisotropic ok)."""
    out, amp, cx, cy = np.zeros((n, n)), 1.0, cells[0], cells[1]
    for _ in range(octaves):
        out += amp * _lattice(rng, n, min(cx, n), min(cy, n))
        amp *= gain
        cx, cy = cx * 2, cy * 2
    return (out - out.min()) / max(out.ptp(), 1e-9)


def _uv_grid(n):
    u = (np.arange(n) + 0.5) / n
    return np.meshgrid(u, u)  # U (cols), V (rows)


def _rgba(rgb, alpha=None):
    n = rgb.shape[0]
    a = np.full((n, n), 255, np.uint8) if alpha is None else (alpha * 255).astype(np.uint8)
    return np.dstack([(np.clip(rgb, 0, 1) * 255).astype(np.uint8), a])


def tex_bark(color, rng, n=TEX_SIZE, rings=0.0):
    """Vertical fibrous grain with dark crevices; rings>0 adds horizontal ring bands (palm)."""
    grain = fbm(rng, n, cells=(24, 3), octaves=5)
    crev = fbm(rng, n, cells=(12, 2), octaves=3)
    U, V = _uv_grid(n)
    shade = 0.6 + 0.6 * grain - 0.35 * (crev < 0.35)
    h = 0.35 + 0.5 * grain - 0.3 * (crev < 0.35)
    if rings:
        band = 0.5 + 0.5 * np.cos(2 * np.pi * rings * V)
        shade *= 0.8 + 0.3 * band
        h = 0.6 * h + 0.4 * band
    return _rgba(np.asarray(color) * shade[..., None]), np.clip(h, 0, 1)


def tex_rock(color, rng, n=TEX_SIZE, color2=None, strata=6):
    """Horizontal strata (color/color2 bands, ledges in the height), speckle, grime."""
    color2 = np.asarray(color2 if color2 is not None else np.asarray(color) * 0.75)
    U, V = _uv_grid(n)
    warp = 0.12 * fbm(rng, n, cells=(3, 3), octaves=3)
    c = V * strata + warp
    band = np.floor(c).astype(int)
    frac = c - band
    per_band = rng.uniform(0.8, 1.15, strata + 4)[np.clip(band, 0, strata + 3)]
    mix = (band % 2)[..., None]
    base = np.asarray(color) * (1 - mix) + color2 * mix
    speck = fbm(rng, n, cells=(96, 96), octaves=2)
    grime = fbm(rng, n, cells=(3, 3), octaves=4)
    shade = per_band * (0.8 + 0.35 * speck) * (0.75 + 0.35 * grime)
    h = 0.55 * frac + 0.25 * speck + 0.2 * grime
    return _rgba(base * shade[..., None]), np.clip(h, 0, 1)


def tex_cactus(color, rng, n=TEX_SIZE, ribs=10):
    """Vertical ribs (cos across u) with lighter areole dashes on the crests."""
    U, V = _uv_grid(n)
    rib = 0.5 + 0.5 * np.cos(2 * np.pi * ribs * U)
    noise = fbm(rng, n, cells=(8, 8), octaves=4)
    areole = (rib > 0.93) & (np.mod(V * 24 + 0.5 * (np.floor(U * ribs) % 2), 1) < 0.12)
    shade = 0.7 + 0.45 * rib + 0.2 * (noise - 0.5)
    rgb = np.asarray(color) * shade[..., None]
    rgb[areole] = rgb[areole] * 0.5 + np.array([0.85, 0.85, 0.7]) * 0.5
    return _rgba(rgb), np.clip(0.2 + 0.7 * rib + 0.1 * noise, 0, 1)


def tex_sand(color, rng, n=TEX_SIZE):
    """Fine grain + faint ripples; for sand/dirt/ground patches."""
    U, V = _uv_grid(n)
    grain = fbm(rng, n, cells=(64, 64), octaves=3)
    ripple = 0.5 + 0.5 * np.sin(2 * np.pi * (6 * V + 0.3 * fbm(rng, n, cells=(2, 2), octaves=2)))
    shade = 0.82 + 0.25 * grain + 0.1 * ripple
    return _rgba(np.asarray(color) * shade[..., None]), np.clip(0.5 * grain + 0.5 * ripple, 0, 1)


def tex_frond(color, rng, n=TEX_SIZE, leaflets=18, rachis_color=(0.45, 0.35, 0.18)):
    """Alpha card: pinnate frond, base at v=0 (row 0), tip at v=1, rachis along u=0.5."""
    U, V = _uv_grid(n)
    s = np.abs(U - 0.5)
    span = 0.5 * np.clip(np.sin(np.pi * np.clip((V - 0.04) / 0.96, 0, 1)), 0, 1) ** 0.5
    c = V - 0.55 * s  # leaflets angle toward the tip
    f = np.mod(c * leaflets, 1)
    width = 0.72 * (1 - 0.5 * np.clip(s / np.maximum(span, 1e-6), 0, 1))
    leaf = (f > 0.1) & (f < 0.1 + width) & (s < span) & (V > 0.04)
    rachis = s < (0.006 + 0.016 * (1 - V))
    noise = fbm(rng, n, cells=(16, 16), octaves=3)
    rgb = np.asarray(color) * (0.7 + 0.5 * noise)[..., None]
    rgb[np.abs(f - 0.1 - width / 2) < 0.05] *= 1.12  # leaflet midrib
    rgb[rachis] = rachis_color
    return _rgba(rgb, (leaf | rachis).astype(float)), np.clip(0.3 + 0.4 * noise, 0, 1)


def tex_blade(color, rng, n=TEX_SIZE, blades=14):
    """Alpha card: grass blades fanning from the bottom center (v=0 row 0 = base)."""
    U, V = _uv_grid(n)
    alpha = np.zeros((n, n), bool)
    for _ in range(blades):
        a = rng.uniform(-0.55, 0.55)
        b = a * rng.uniform(0.2, 0.9)
        L = rng.uniform(0.6, 1.0)
        w0 = rng.uniform(0.03, 0.05)
        uc = 0.5 + a * V + b * V * V
        alpha |= (np.abs(U - uc) < w0 * (1 - V / L)) & (V < L)
    noise = fbm(rng, n, cells=(8, 8), octaves=3)
    rgb = np.asarray(color) * (0.55 + 0.45 * V + 0.3 * (noise - 0.5))[..., None]
    return _rgba(rgb, alpha.astype(float)), np.full((n, n), 0.5)


def tex_twig(color, rng, n=TEX_SIZE, depth=4):
    """Alpha card: branching dry twigs from the bottom center (v=0 row 0 = base)."""
    U, V = _uv_grid(n)
    alpha = np.zeros((n, n), bool)
    segs = [((0.5, 0.0), (0.5 + rng.uniform(-0.1, 0.1), 0.3), 0.016, 0)]
    while segs:
        (x0, y0), (x1, y1), w, d = segs.pop()
        px, py = x1 - x0, y1 - y0
        t = np.clip(((U - x0) * px + (V - y0) * py) / max(px * px + py * py, 1e-9), 0, 1)
        dist = np.hypot(U - (x0 + t * px), V - (y0 + t * py))
        alpha |= dist < w
        if d < depth:
            for _ in range(rng.integers(2, 4)):
                ang = math.atan2(py, px) + rng.uniform(-0.9, 0.9)
                ln = math.hypot(px, py) * rng.uniform(0.55, 0.85)
                segs.append(((x1, y1), (x1 + ln * math.cos(ang), y1 + ln * math.sin(ang)), w * 0.65, d + 1))
    rgb = np.asarray(color) * (0.6 + 0.5 * fbm(rng, n, cells=(16, 16), octaves=2))[..., None]
    return _rgba(rgb, alpha.astype(float)), np.full((n, n), 0.5)


TEX_GENERATORS = {"bark": tex_bark, "rock": tex_rock, "cactus": tex_cactus, "sand": tex_sand,
                  "frond": tex_frond, "blade": tex_blade, "twig": tex_twig}


def normal_map(height, strength=3.0):
    """Tangent-space normal map (glTF/OpenGL +Y up) from a 0..1 height field via central differences."""
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength * height.shape[1] / 64
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength * height.shape[0] / 64
    n = np.dstack([-dx, dy, np.ones_like(dx)])
    n /= np.linalg.norm(n, axis=2)[..., None]
    return ((n * 0.5 + 0.5) * 255).astype(np.uint8)


def _png(arr):
    buf = io.BytesIO()
    Image.fromarray(arr).save(buf, "PNG", compress_level=6)
    return buf.getvalue()


def build_textures(palette):
    """palette key -> dict(rgba, base_png, normal_png, mask). Deterministic per key order."""
    out = {}
    for i, (key, ent) in enumerate(palette.items()):
        opts = {k: v for k, v in ent.items() if k not in ("color", "tex", "seed")}
        rng = np.random.default_rng(ent.get("seed", 1000 + i))
        rgba, h = TEX_GENERATORS[ent["tex"]](ent["color"], rng, **opts)
        out[key] = {"rgba": rgba, "base_png": _png(rgba), "normal_png": _png(normal_map(h)),
                    "mask": bool((rgba[..., 3] < 255).any())}
    return out


# --------------------------------------------------------------------------
# GLB writer
# --------------------------------------------------------------------------
def _pad(b, fill=b"\x00"):
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, name, mesh, textures, theme):
    """textures: build_textures(palette). Embeds baseColor + normal PNGs per used material."""
    tris, uvs, mats = mesh.finalize()
    nrm_all = smooth_normals(tris)
    bin_data = bytearray()
    views, accessors, materials, prims, images, texs = [], [], [], [], [], []
    image_index = {}

    def add_view(data, target=None):
        off = len(bin_data)
        bin_data.extend(_pad(data))
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v["target"] = target
        views.append(v)
        return len(views) - 1

    for mi, mat in enumerate(sorted(set(mats))):
        sel = mats == mat
        pos = tris[sel].reshape(-1, 3).astype(np.float32)
        nrm = nrm_all[sel].reshape(-1, 3).astype(np.float32)
        uv = uvs[sel].reshape(-1, 2).astype(np.float32)
        nv = len(pos)
        if nv <= 65535:
            idx, ctype = np.arange(nv, dtype=np.uint16), 5123
        else:
            idx, ctype = np.arange(nv, dtype=np.uint32), 5125
        a = len(accessors)
        accessors.append({"bufferView": add_view(pos.tobytes(), 34962), "componentType": 5126,
                          "count": nv, "type": "VEC3",
                          "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()})
        accessors.append({"bufferView": add_view(nrm.tobytes(), 34962), "componentType": 5126,
                          "count": nv, "type": "VEC3"})
        accessors.append({"bufferView": add_view(uv.tobytes(), 34962), "componentType": 5126,
                          "count": nv, "type": "VEC2"})
        accessors.append({"bufferView": add_view(idx.tobytes(), 34963), "componentType": ctype,
                          "count": nv, "type": "SCALAR"})
        t = textures[mat]
        ti = len(texs)
        for png in (t["base_png"], t["normal_png"]):
            if png not in image_index:  # identical PNG bytes embedded once per glb
                images.append({"bufferView": add_view(png), "mimeType": "image/png"})
                image_index[png] = len(images) - 1
            texs.append({"sampler": 0, "source": image_index[png]})
        materials.append({
            "name": f"M_{theme}_{mat}", "doubleSided": t["mask"],
            "alphaMode": "MASK" if t["mask"] else "OPAQUE", **({"alphaCutoff": 0.5} if t["mask"] else {}),
            "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                                     "baseColorTexture": {"index": ti, "texCoord": 0},
                                     "metallicFactor": 0.0, "roughnessFactor": ROUGHNESS},
            "normalTexture": {"index": ti + 1, "texCoord": 0}})
        prims.append({"attributes": {"POSITION": a, "NORMAL": a + 1, "TEXCOORD_0": a + 2},
                      "indices": a + 3, "material": mi, "mode": 4})

    gltf = {
        "asset": {"version": "2.0", "generator": "meshkit.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
        "textures": texs,
        "images": images,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_data)}],
    }
    js = _pad(json.dumps(gltf, separators=(",", ":")).encode(), b" ")
    bin_chunk = bytes(bin_data)
    total = 12 + 8 + len(js) + 8 + len(bin_chunk)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk)
    return len(tris)


# --------------------------------------------------------------------------
# Self-check: re-parse the GLB independently of the writer
# --------------------------------------------------------------------------
def verify_glb(path, max_tris):
    """Returns (tris, height, width). width = largest extent across x/z."""
    data = Path(path).read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2, f"{path}: bad header"
    assert length == len(data), f"{path}: length field {length} != file size {len(data)}"
    jlen, jtype = struct.unpack_from("<II", data, 12)
    assert jtype == 0x4E4F534A, f"{path}: first chunk not JSON"
    gltf = json.loads(data[20:20 + jlen])
    blen, btype = struct.unpack_from("<II", data, 20 + jlen)
    assert btype == 0x004E4942, f"{path}: second chunk not BIN"
    boff = 28 + jlen
    assert boff + blen == len(data), f"{path}: BIN chunk length mismatch"
    assert gltf["buffers"][0]["byteLength"] == blen, f"{path}: buffer byteLength mismatch"
    bin_chunk = data[boff:boff + blen]
    for bv in gltf["bufferViews"]:
        assert bv["byteOffset"] + bv["byteLength"] <= blen, f"{path}: bufferView outside BIN"

    def read(acc_i, dtype, ncomp):
        acc = gltf["accessors"][acc_i]
        bv = gltf["bufferViews"][acc["bufferView"]]
        arr = np.frombuffer(bin_chunk, dtype=dtype, count=acc["count"] * ncomp,
                            offset=bv["byteOffset"] + acc.get("byteOffset", 0))
        return arr.reshape(acc["count"], ncomp) if ncomp > 1 else arr

    # images / textures / samplers
    assert gltf["samplers"], f"{path}: no sampler"
    for img in gltf["images"]:
        assert img["mimeType"] == "image/png" and "bufferView" in img and "uri" not in img, f"{path}: image not embedded png"
        bv = gltf["bufferViews"][img["bufferView"]]
        assert "target" not in bv, f"{path}: image bufferView has a GPU target"
        png = bin_chunk[bv["byteOffset"]:bv["byteOffset"] + bv["byteLength"]]
        assert png[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: image bytes are not PNG"
        im = Image.open(io.BytesIO(png))
        im.verify()
        assert im.size == (TEX_SIZE, TEX_SIZE), f"{path}: texture size {im.size}"
    for tex in gltf["textures"]:
        assert 0 <= tex["source"] < len(gltf["images"]) and 0 <= tex["sampler"] < len(gltf["samplers"]), f"{path}: bad texture ref"
    for m in gltf["materials"]:
        assert m.get("alphaMode", "OPAQUE") in ("OPAQUE", "MASK", "BLEND"), f"{path}: bad alphaMode"
        for ref in (m["pbrMetallicRoughness"]["baseColorTexture"], m["normalTexture"]):
            assert 0 <= ref["index"] < len(gltf["textures"]) and ref.get("texCoord", 0) == 0, f"{path}: bad textureInfo"

    tris, vol, opaque = 0, 0.0, 0
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for prim in gltf["meshes"][0]["primitives"]:
        pacc = gltf["accessors"][prim["attributes"]["POSITION"]]
        assert "min" in pacc and "max" in pacc, f"{path}: POSITION accessor lacks min/max"
        iacc = gltf["accessors"][prim["indices"]]
        idx = read(prim["indices"], np.uint16 if iacc["componentType"] == 5123 else np.uint32, 1)
        assert idx.max() < pacc["count"], f"{path}: index out of range"
        assert len(idx) % 3 == 0
        pos = read(prim["attributes"]["POSITION"], np.float32, 3).astype(float)
        nrm = read(prim["attributes"]["NORMAL"], np.float32, 3).astype(float)
        uacc = gltf["accessors"][prim["attributes"]["TEXCOORD_0"]]
        assert uacc["type"] == "VEC2" and uacc["componentType"] == 5126 and uacc["count"] == pacc["count"], f"{path}: TEXCOORD_0 mismatch"
        uv = read(prim["attributes"]["TEXCOORD_0"], np.float32, 2)
        assert np.isfinite(uv).all() and (np.abs(uv) < 1e4).all(), f"{path}: bad UVs"
        assert np.allclose(np.linalg.norm(nrm, axis=1), 1, atol=1e-3), f"{path}: non-unit normals"
        assert np.allclose(pos.min(axis=0), pacc["min"], atol=1e-6) and np.allclose(pos.max(axis=0), pacc["max"], atol=1e-6)
        t = pos[idx].reshape(-1, 3, 3)
        fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        # stored (smoothed) normals must still face the winding side
        assert (np.einsum("ij,ij->i", fn[:, None, :].repeat(3, 1).reshape(-1, 3), nrm[idx]) > 0).all(), f"{path}: normal/winding mismatch"
        if gltf["materials"][prim["material"]].get("alphaMode") != "MASK":
            vol += np.einsum("ij,ij->i", t.mean(axis=1), fn).sum() / 6  # signed volume
            opaque += len(t)
        tris += len(t)
        lo, hi = np.minimum(lo, pacc["min"]), np.maximum(hi, pacc["max"])
    assert abs(lo[1]) < 1e-4, f"{path}: min y = {lo[1]}, expected 0"
    assert opaque == 0 or vol > 0, f"{path}: negative signed volume -> inverted normals"
    assert tris <= max_tris, f"{path}: {tris} tris > budget {max_tris}"
    return tris, float(hi[1]), float(max(hi[0] - lo[0], hi[2] - lo[2]))


# --------------------------------------------------------------------------
# Preview contact sheet: numpy software rasterizer (z-buffer, perspective-correct UV/normal,
# bilinear baseColor + normal map, alpha test for MASK, 2x supersampling) -> webp.
# --------------------------------------------------------------------------
PREVIEW_CELL = 600
PREVIEW_SS = 2
_KEY_LIGHT = unit([0.45, 0.75, 0.5])
_KEY_COLOR = np.array([1.0, 0.96, 0.90])
_SKY, _GROUND_TINT = np.array([0.62, 0.70, 0.82]), np.array([0.42, 0.38, 0.34])
_GROUND_ALBEDO = np.array([0.58, 0.55, 0.50])


def _sample(img, u, v):
    """Bilinear, wrapping sample of img (n,n,c) float32 at UV arrays (row 0 == v=0)."""
    n = img.shape[0]
    x, y = u * n - 0.5, v * n - 0.5
    x0, y0 = np.floor(x), np.floor(y)
    fx, fy = (x - x0)[:, None].astype(np.float32), (y - y0)[:, None].astype(np.float32)
    x0, y0 = x0.astype(np.int64) % n, y0.astype(np.int64) % n
    x1, y1 = (x0 + 1) % n, (y0 + 1) % n
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy)
            + img[y1, x0] * (1 - fx) * fy + img[y1, x1] * fx * fy)


def _camera(tris, W, H, elev_deg=25.0, azim_deg=35.0, fov_deg=28.0):
    """Perspective camera looking at the bbox center, distance fitted so the projected bbox fills ~90%."""
    lo, hi = tris.reshape(-1, 3).min(axis=0), tris.reshape(-1, 3).max(axis=0)
    center = (lo + hi) / 2
    el, az = math.radians(elev_deg), math.radians(azim_deg)
    view_dir = np.array([math.cos(el) * math.sin(az), math.sin(el), math.cos(el) * math.cos(az)])
    focal = (H / 2) / math.tan(math.radians(fov_deg) / 2)
    corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
    d = max(np.linalg.norm(hi - lo) / 2, 1e-3) / math.sin(math.radians(fov_deg) / 2)
    for _ in range(3):  # fit projected bbox corners into 90% of the frame
        eye = center + view_dir * d
        f = unit(center - eye)
        r = unit(np.cross(f, [0, 1, 0]))
        u = np.cross(r, f)
        p = corners - eye
        x, y, z = p @ r, p @ u, p @ f
        over = max(np.abs(x / z).max() * focal / (0.9 * W / 2), np.abs(y / z).max() * focal / (0.9 * H / 2))
        d *= over
    eye = center + view_dir * d
    f = unit(center - eye)
    r = unit(np.cross(f, [0, 1, 0]))
    u = np.cross(r, f)
    return eye, np.stack([r, u, f]), focal, d


def _project(P, eye, M, focal, W, H):
    """World (...,3) -> screen x, y (pixels) and view depth z."""
    v = (P - eye) @ M.T
    z = v[..., 2]
    return v[..., 0] / z * focal + W / 2, H / 2 - v[..., 1] / z * focal, z


def _rasterize(sx, sy, invz, uvs, mats, tex_f, W, H):
    """Z-buffer resolve. Returns per-pixel winning triangle index (-1 = background).
    Fragments of MASK materials are alpha-tested (bilinear alpha >= 0.5) before the depth test."""
    keybuf = np.full(W * H, np.iinfo(np.int64).max, dtype=np.int64)
    x0 = np.clip(np.floor(sx.min(axis=1)), 0, W - 1).astype(np.int64)
    y0 = np.clip(np.floor(sy.min(axis=1)), 0, H - 1).astype(np.int64)
    x1 = np.clip(np.ceil(sx.max(axis=1)), 0, W - 1).astype(np.int64)
    y1 = np.clip(np.ceil(sy.max(axis=1)), 0, H - 1).astype(np.int64)
    area = (sx[:, 1] - sx[:, 0]) * (sy[:, 2] - sy[:, 0]) - (sx[:, 2] - sx[:, 0]) * (sy[:, 1] - sy[:, 0])
    ok = (np.abs(area) > 1e-9) & (x1 >= x0) & (y1 >= y0) & (sx.min(axis=1) < W) & (sx.max(axis=1) >= 0) \
        & (sy.min(axis=1) < H) & (sy.max(axis=1) >= 0)
    zmin, zmax = (1 / invz).min(), (1 / invz).max()
    zscale = (2 ** 31 - 1) / max(zmax - zmin, 1e-9)
    kw = np.ceil(np.log2(x1 - x0 + 1)).astype(int)
    kh = np.ceil(np.log2(y1 - y0 + 1)).astype(int)
    mask_mat = np.array([tex_f[m]["mask"] for m in mats])
    for key in np.unique(kw[ok] * 100 + kh[ok]):  # bucket triangles by bbox size class
        Sw, Sh = 2 ** (key // 100), 2 ** (key % 100)
        ids_all = np.nonzero(ok & (kw * 100 + kh == key))[0]
        step = max(1, (1 << 22) // (Sw * Sh))
        for c in range(0, len(ids_all), step):
            ids = ids_all[c:c + step]
            px = (x0[ids][:, None, None] + np.arange(Sw)[None, None, :]).astype(np.float64)
            py = (y0[ids][:, None, None] + np.arange(Sh)[None, :, None]).astype(np.float64)
            inb = (px < W) & (py < H)
            px, py = px + 0.5, py + 0.5
            X, Y = sx[ids][:, :, None, None], sy[ids][:, :, None, None]
            A = area[ids][:, None, None]
            l0 = ((X[:, 1] - px) * (Y[:, 2] - py) - (X[:, 2] - px) * (Y[:, 1] - py)) / A
            l1 = ((X[:, 2] - px) * (Y[:, 0] - py) - (X[:, 0] - px) * (Y[:, 2] - py)) / A
            l2 = 1 - l0 - l1
            inside = inb & (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            b, iy, ix = np.nonzero(inside)
            if not len(b):
                continue
            t = ids[b]
            L = np.stack([l0[b, iy, ix], l1[b, iy, ix], l2[b, iy, ix]], axis=1)
            iz = (L * invz[t]).sum(axis=1)
            am = mask_mat[t]
            if am.any():  # alpha test
                w = L[am] * invz[t[am]] / iz[am, None]
                uv = (w[:, :, None] * uvs[t[am]]).sum(axis=1)
                alpha = np.empty(am.sum(), np.float32)
                for m in set(mats[t[am]]):
                    sel = mats[t[am]] == m
                    alpha[sel] = _sample(tex_f[m]["a"], uv[sel, 0] % 1.0, uv[sel, 1] % 1.0)[:, 0]
                keep = np.ones(len(t), bool)
                keep[np.nonzero(am)[0][alpha < 0.5]] = False
                t, iy, ix, b, iz = t[keep], iy[keep], ix[keep], b[keep], iz[keep]
            pix = (y0[t] + iy) * W + (x0[t] + ix)
            zq = ((1 / iz - zmin) * zscale).astype(np.int64)
            np.minimum.at(keybuf, pix, (zq << 32) | t)
    win = np.where(keybuf == np.iinfo(np.int64).max, -1, keybuf & 0xFFFFFFFF)
    return win.reshape(H, W)


def _tangents(tris, uvs):
    """Per-triangle tangent (dP/du) and bitangent (dP/dv)."""
    e1, e2 = tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]
    d1, d2 = uvs[:, 1] - uvs[:, 0], uvs[:, 2] - uvs[:, 0]
    r = 1.0 / np.where(np.abs(d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]) < 1e-12, 1e-12,
                       d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1])
    T = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r[:, None]
    B = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r[:, None]
    return T, B


def render_mesh(mesh, textures, size=PREVIEW_CELL, ss=PREVIEW_SS):
    """Render one Mesh to an (size, size, 3) float image in 0..1 (ground plane + soft shadow)."""
    W = H = size * ss
    tris, uvs, mats = mesh.finalize()
    nrm = smooth_normals(tris)
    tex_f = {m: {"rgb": textures[m]["rgba"][..., :3].astype(np.float32) / 255,
                 "a": textures[m]["rgba"][..., 3:].astype(np.float32) / 255,
                 "n": (np.asarray(Image.open(io.BytesIO(textures[m]["normal_png"]))).astype(np.float32) / 127.5 - 1),
                 "mask": textures[m]["mask"]} for m in set(mats)}
    eye, M, focal, dist = _camera(tris, W, H)
    sx, sy, z = _project(tris, eye, M, focal, W, H)
    invz = 1.0 / z
    win = _rasterize(sx, sy, invz, uvs, mats, tex_f, W, H)

    # background: sky gradient + ground plane (y=0) with a soft shadow ellipse under the footprint
    jj, ii = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5)
    rays = np.stack([(jj - W / 2) / focal, (H / 2 - ii) / focal, np.ones_like(jj)], axis=-1) @ M
    img = np.empty((H, W, 3), np.float32)
    grad = np.clip((ii / H), 0, 1)[..., None]
    img[:] = (_SKY * 1.15) * (1 - grad) + np.array([0.80, 0.78, 0.74]) * grad
    hit = rays[..., 1] < -1e-6
    tt = np.where(hit, -eye[1] / np.where(hit, rays[..., 1], -1), 0)
    gx, gz = eye[0] + rays[..., 0] * tt, eye[2] + rays[..., 2] * tt
    lo, hi = tris.reshape(-1, 3).min(axis=0), tris.reshape(-1, 3).max(axis=0)
    cx, cz = (lo[0] + hi[0]) / 2 - _KEY_LIGHT[0] / _KEY_LIGHT[1] * 0.15 * hi[1], (lo[2] + hi[2]) / 2 - _KEY_LIGHT[2] / _KEY_LIGHT[1] * 0.15 * hi[1]
    rx, rz = (hi[0] - lo[0]) / 2 * 1.15 + 0.05 * hi[1], (hi[2] - lo[2]) / 2 * 1.15 + 0.05 * hi[1]
    s = ((gx - cx) / rx) ** 2 + ((gz - cz) / rz) ** 2
    shadow = 0.5 * np.exp(-1.4 * s)
    fade = np.clip(tt / (dist * 4), 0, 1) ** 0.7
    ground = _GROUND_ALBEDO * (0.95 - shadow)[..., None]
    ground = ground * (1 - fade[..., None]) + img * fade[..., None]
    img[hit] = ground[hit]

    # shade covered pixels
    ys, xs = np.nonzero(win >= 0)
    if len(ys):
        t = win[ys, xs]
        px, py = xs + 0.5, ys + 0.5
        X, Y = sx[t], sy[t]
        A = (X[:, 1] - X[:, 0]) * (Y[:, 2] - Y[:, 0]) - (X[:, 2] - X[:, 0]) * (Y[:, 1] - Y[:, 0])
        l0 = ((X[:, 1] - px) * (Y[:, 2] - py) - (X[:, 2] - px) * (Y[:, 1] - py)) / A
        l1 = ((X[:, 2] - px) * (Y[:, 0] - py) - (X[:, 0] - px) * (Y[:, 2] - py)) / A
        L = np.stack([l0, l1, 1 - l0 - l1], axis=1) * invz[t]
        L /= L.sum(axis=1, keepdims=True)
        uv = (L[:, :, None] * uvs[t]).sum(axis=1)
        n = (L[:, :, None] * nrm[t]).sum(axis=1)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        T, B = _tangents(tris, uvs)
        T, B = T[t], B[t]
        T -= n * (T * n).sum(axis=1, keepdims=True)
        T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-9)
        B = -B + n * (B * n).sum(axis=1, keepdims=True)  # glTF green = up in image = -v
        B /= np.maximum(np.linalg.norm(B, axis=1, keepdims=True), 1e-9)
        albedo = np.empty((len(t), 3), np.float32)
        tn = np.empty((len(t), 3), np.float32)
        mt = mats[t]
        for m in set(mt):
            sel = mt == m
            albedo[sel] = _sample(tex_f[m]["rgb"], uv[sel, 0] % 1.0, uv[sel, 1] % 1.0)
            tn[sel] = _sample(tex_f[m]["n"], uv[sel, 0] % 1.0, uv[sel, 1] % 1.0)
        tn[:, :2] *= 0.6  # soften normal-map relief
        n2 = T * tn[:, 0:1] + B * tn[:, 1:2] + n * tn[:, 2:3]
        n2 /= np.maximum(np.linalg.norm(n2, axis=1, keepdims=True), 1e-9)
        to_eye = eye - (L[:, :, None] * tris[t]).sum(axis=1)
        flip = (n2 * to_eye).sum(axis=1) < 0  # double-sided: face the camera
        n2[flip] *= -1
        ndl = n2 @ _KEY_LIGHT
        diffuse = np.maximum(ndl, 0)
        is_mask = np.array([tex_f[m]["mask"] for m in mt])
        diffuse = diffuse + is_mask * 0.35 * np.maximum(-ndl, 0)  # leaf translucency
        hemi = _GROUND_TINT * (0.5 - 0.5 * n2[:, 1:2]) + _SKY * (0.5 + 0.5 * n2[:, 1:2])
        light = diffuse[:, None] * _KEY_COLOR * 1.15 + hemi * 0.7
        img[ys, xs] = np.clip(albedo * light, 0, 1)
    img = img.reshape(size, ss, size, ss, 3).mean(axis=(1, 3))
    return np.clip(img, 0, 1)


def render_preview(results, textures, title, out_webp, cols=4):
    """results: list of (name, slot, mesh, tris, height, width). Software-rendered contact sheet."""
    from PIL import ImageDraw, ImageFont
    cell, label, top = PREVIEW_CELL, 54, 44
    rows = math.ceil(len(results) / cols)
    sheet = Image.new("RGB", (cols * cell, top + rows * (cell + label)), (28, 28, 30))
    draw = ImageDraw.Draw(sheet)
    try:
        font, font_b = ImageFont.load_default(size=15), ImageFont.load_default(size=20)
    except TypeError:  # old Pillow: bitmap default font only
        font = font_b = ImageFont.load_default()
    draw.text((12, 12), title, fill=(235, 235, 235), font=font_b)
    for k, (name, slot, mesh, tris_n, h, w) in enumerate(results):
        img = (render_mesh(mesh, textures) * 255).astype(np.uint8)
        x, y = (k % cols) * cell, top + (k // cols) * (cell + label)
        sheet.paste(Image.fromarray(img), (x, y))
        draw.text((x + 10, y + cell + 6), name, fill=(240, 240, 240), font=font)
        draw.text((x + 10, y + cell + 28), f"[{slot}]  H={h:.2f} m  W={w:.2f} m  {tris_n} tris",
                  fill=(190, 190, 190), font=font)
    sheet.save(out_webp, "WEBP", quality=90)


def run(theme, palette, specs, out_root):
    """Build every spec -> out_root/meshes/<name>.glb, verify, print table, write preview.webp
    and textures_preview.webp (one strip per palette key). Returns the results list."""
    out_root = Path(out_root)
    mesh_dir = out_root / "meshes"
    mesh_dir.mkdir(parents=True, exist_ok=True)
    counts = Counter(s.slot for s in specs)
    assert counts == Counter(SLOT_COUNT), f"slot counts {dict(counts)} != {SLOT_COUNT}"
    assert len({s.name for s in specs}) == len(specs), "duplicate spec names"
    textures = build_textures(palette)
    results = []
    print(f"{'name':<30}{'slot':<8}{'tris':>6}{'H[m]':>7}{'W[m]':>7}{'bytes':>9}")
    for s in specs:
        assert s.tris <= SLOT_TRIS[s.slot], f"{s.name}: budget {s.tris} > slot max {SLOT_TRIS[s.slot]}"
        mesh = s.build(s.seed)
        path = mesh_dir / f"{s.name}.glb"
        write_glb(path, s.name, mesh, textures, theme)
        tris, h, w = verify_glb(path, s.tris)
        if s.slot == "big":
            assert BIG_MIN <= h <= BIG_MAX or BIG_MIN <= w <= BIG_MAX, \
                f"{s.name}: big needs {BIG_MIN}-{BIG_MAX} m tall or wide, got H={h:.2f} W={w:.2f}"
        else:
            hmin, hmax, wmin, wmax = SLOT_RULES[s.slot]
            assert hmin <= h <= hmax, f"{s.name}: height {h:.2f} outside {hmin}-{hmax} ({s.slot})"
            if wmin is not None:
                assert wmin <= w <= wmax, f"{s.name}: width {w:.2f} outside {wmin}-{wmax} ({s.slot})"
        print(f"{s.name:<30}{s.slot:<8}{tris:>6}{h:>7.2f}{w:>7.2f}{path.stat().st_size:>9}")
        results.append((s.name, s.slot, mesh, tris, h, w))
    out = out_root / "preview.webp"
    render_preview(results, textures, f"{theme} asset set (glTF Y-up, meters, pivot at base)", out)
    strip = np.concatenate([np.concatenate([t["rgba"][..., :3], t["rgba"][..., 3:].repeat(3, 2)], axis=0)
                            for t in textures.values()], axis=1)
    Image.fromarray(strip).resize((strip.shape[1] // 2, strip.shape[0] // 2)).save(out_root / "textures_preview.webp", "WEBP", quality=80)
    print(f"OK: {len(results)} meshes verified, preview -> {out}")
    return results
