#!/usr/bin/env python3
"""Desert asset set generator for the LLM-Driven PCG project (UE 5.6).

Builds low-poly, flat-shaded stylized meshes with pure Python + numpy and
writes each as a binary glTF 2.0 (.glb). Deterministic (fixed seeds).

Conventions: meters, Y-up (glTF), pivot at base center (y=0), mesh on +Y.
Run:  python3 generate_desert_set.py        -> meshes/*.glb, preview.png

To make another theme (grassland/snow): copy this file, change PALETTE and
the shape builders listed in SPECS. Keep write_glb / verify_glb / preview.
"""
import json
import math
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
MESH_DIR = ROOT / "meshes"

# --------------------------------------------------------------------------
# Palette: the only place colors are defined. Swap for other themes.
# --------------------------------------------------------------------------
PALETTE = {
    "sand":       (0.86, 0.74, 0.52),
    "terracotta": (0.72, 0.42, 0.28),
    "sage":       (0.47, 0.56, 0.40),   # muted sage green (cactus)
    "dry_wood":   (0.45, 0.33, 0.22),
    "straw":      (0.78, 0.68, 0.40),   # dry grass / shrub twigs
}
ROUGHNESS = 0.9
MAX_TRIS = 3000


# --------------------------------------------------------------------------
# Mesh container (flat shaded: triangles are stored as independent faces)
# --------------------------------------------------------------------------
class Mesh:
    def __init__(self):
        self.chunks = []  # (tris (M,3,3), material names (M,))

    def add(self, V, F, mat):
        V = np.asarray(V, float)
        F = np.asarray(F, int)
        tris = V[F]
        if isinstance(mat, str):
            mat = np.full(len(F), mat, dtype=object)
        self.chunks.append((tris, np.asarray(mat, dtype=object)))

    def finalize(self):
        tris = np.concatenate([c[0] for c in self.chunks])
        mats = np.concatenate([c[1] for c in self.chunks])
        tris[:, :, 1] = np.maximum(tris[:, :, 1], 0.0)  # ground clamp (y>=0)
        n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        keep = np.linalg.norm(n, axis=1) > 1e-9        # drop degenerate faces
        return tris[keep], mats[keep]


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


# --------------------------------------------------------------------------
# Primitive builders
# --------------------------------------------------------------------------
def tube(path, radii, sides=8, aspect=1.0, rib=0, rib_amp=0.0, mod=None, cap=True):
    """Sweep a (ribbed) circle along a polyline. Returns (V, F), outward winding."""
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
    F = []
    for i in range(n - 1):
        for j in range(sides):
            a, b = i * sides + j, i * sides + (j + 1) % sides
            F += [[a, b, b + sides], [a, b + sides, a + sides]]
    if cap:
        c0 = len(V)
        V = np.vstack([V, P[0]])
        F += [[c0, (j + 1) % sides, j] for j in range(sides)]
        c1 = len(V)
        V = np.vstack([V, P[-1]])
        base = (n - 1) * sides
        F += [[c1, base + j, base + (j + 1) % sides] for j in range(sides)]
    return V, np.array(F)


def uv_sphere(lat=10, lon=14):
    V = []
    for i in range(1, lat):
        phi = np.pi * i / lat
        for j in range(lon):
            t = 2 * np.pi * j / lon
            V.append([np.sin(phi) * np.cos(t), np.cos(phi), np.sin(phi) * np.sin(t)])
    top = len(V)
    V.append([0, 1.0, 0])
    bot = len(V)
    V.append([0, -1.0, 0])
    F = []
    for i in range(lat - 2):
        for j in range(lon):
            a, b = i * lon + j, i * lon + (j + 1) % lon
            F += [[a, b, b + lon], [a, b + lon, a + lon]]
    last = (lat - 2) * lon
    for j in range(lon):
        F.append([top, (j + 1) % lon, j])
        F.append([bot, last + j, last + (j + 1) % lon])
    return np.array(V), np.array(F)


# --------------------------------------------------------------------------
# Desert shapes (tree slot)
# --------------------------------------------------------------------------
def _column(start, up_len, R, dome=True):
    """Path+radii of a cactus column with rounded top."""
    pts, rad = [], []
    for t in np.linspace(0, 1, 6):
        pts.append(start + np.array([0, up_len * t, 0]))
        rad.append(R * (1 + 0.12 * t * (1 - t)))
    if dome:
        for a in (0.45, 0.9, 1.3, 1.52):
            pts.append(start + np.array([0, up_len + R * math.sin(a), 0]))
            rad.append(R * math.cos(a) + 0.01)
    return pts, rad


def saguaro(seed, height, arm_specs):
    """arm_specs: list of (height_frac, azimuth_deg, out, up)."""
    m = Mesh()
    R = 0.075 * height
    trunk_h = height - R
    pts, rad = _column(np.zeros(3), trunk_h, R)
    V, F = tube(pts, rad, sides=12, rib=10, rib_amp=0.07)
    m.add(V, F, "sage")
    for frac, az, out, up in arm_specs:
        d = np.array([math.cos(math.radians(az)), 0, math.sin(math.radians(az))])
        s = np.array([0, trunk_h * frac, 0])
        b = 0.35
        apts = [s, s + d * 0.5 * out - np.array([0, 0.04, 0]), s + d * out]
        Q = s + d * out + np.array([0, b, 0])
        for a in (0.5, 1.0, 1.4):
            apts.append(Q + b * (math.sin(a) * d - math.cos(a) * np.array([0, 1.0, 0])))
        elbow = Q + b * d
        cpts, crad = _column(elbow, up, 0.65 * R)
        apts += cpts[1:]
        arad = [0.65 * R] * len(apts[: -len(cpts) + 1]) + crad[1:]
        V, F = tube(apts, arad, sides=10, rib=8, rib_amp=0.07)
        m.add(V, F, "sage")
    return m


def dead_tree(seed, height):
    rng = np.random.default_rng(seed)
    m = Mesh()

    def branch(p0, d, length, r0, r1, depth, sides, up=0.12):
        pts = [np.asarray(p0, float)]
        d = unit(d)
        steps = 5
        for _ in range(steps):
            d = unit(d + rng.normal(0, 0.28, 3) + np.array([0, up, 0]))
            pts.append(pts[-1] + d * length / steps)
        V, F = tube(pts, np.linspace(r0, r1, steps + 1), sides=sides)
        m.add(V, F, "dry_wood")
        if depth < 2:
            for k in (2, 4):
                out = unit(np.array([pts[k][0], 0, pts[k][2]]) + rng.normal(0, 0.4, 3) + [0, 0.9, 0])
                branch(pts[k], out, length * 0.55, r1 * 1.1, r1 * 0.35, depth + 1, max(sides - 2, 4))
        return pts

    trunk_h = height * 0.62
    trunk = branch([0, 0, 0], [0.05, 1, 0.02], trunk_h, 0.065 * height, 0.03 * height, 1, 8, up=0.7)
    for az in (20, 150, 270):
        p = trunk[-1]
        d = [math.cos(math.radians(az)) * 0.7, 1.0, math.sin(math.radians(az)) * 0.7]
        branch(p, d, height * 0.42, 0.03 * height, 0.008 * height, 1, 6)
    return m


# --------------------------------------------------------------------------
# Rock slot
# --------------------------------------------------------------------------
def rock(seed, size, layers=5, sink=0.18):
    rng = np.random.default_rng(seed)
    V, F = uv_sphere(10, 14)
    W = rng.normal(size=(4, 3)) * 2.2
    ph = rng.uniform(0, 2 * np.pi, 4)
    A = rng.uniform(0.05, 0.14, 4)
    d = 1 + sum(A[k] * np.sin(V @ W[k] + ph[k]) for k in range(4))
    V = V * d[:, None] * np.asarray(size)
    step = 2 * size[1] / layers
    V[:, 1] += 0.7 * (np.round(V[:, 1] / step) * step - V[:, 1])  # terraces
    V[:, 1] -= V[:, 1].min() + sink * 2 * size[1]                # sink into ground
    cy = V[F][:, :, 1].mean(axis=1)
    band = np.floor(cy / step + 0.5).astype(int)
    mats = np.where(band % 2 == 0, "terracotta", "sand").astype(object)
    m = Mesh()
    m.add(V, F, mats)
    return m


def mesa(seed, height, radius, layers=6):
    rng = np.random.default_rng(seed)
    pts, rad, phases = [], [], []
    r = radius
    y = 0.0
    lh = height / layers
    for k in range(layers):
        for yy, rr in ((y, r), (y + lh - 0.05, r * 0.96)):
            pts.append([0, yy, 0])
            rad.append(rr)
            phases.append(rng.uniform(0, 2 * np.pi, 2))
        y += lh
        r *= 0.86
    pts.append([0, height, 0])
    rad.append(r * 0.9)
    phases.append(phases[-1])

    def mod(i, th):
        p1, p2 = phases[i]
        return 1 + 0.07 * np.sin(3 * th + p1) + 0.04 * np.sin(7 * th + p2)

    V, F = tube(pts, rad, sides=18, mod=mod)
    cy = V[F][:, :, 1].mean(axis=1)
    band = np.floor(cy / lh).astype(int)
    mats = np.where(band % 2 == 0, "terracotta", "sand").astype(object)
    m = Mesh()
    m.add(V, F, mats)
    return m


# --------------------------------------------------------------------------
# Grass slot
# --------------------------------------------------------------------------
def dry_shrub(seed, size):
    rng = np.random.default_rng(seed)
    m = Mesh()
    c = np.array([0, size * 0.55, 0])
    V, F = tube([[0, 0, 0], [0, size * 0.25, 0], c], [0.03, 0.02, 0.012], sides=5)
    m.add(V, F, "dry_wood")
    for _ in range(64):
        p = c + rng.normal(0, 0.06, 3) * size
        d = unit(rng.normal(size=3))
        pts = [p]
        for _ in range(4):
            d = unit(d + rng.normal(0, 0.7, 3))
            p = p + d * size * 0.22
            p[1] = max(p[1], 0.03)
            pts.append(p)
        V, F = tube(pts, np.linspace(0.012, 0.004, 5), sides=3)
        m.add(V, F, "straw")
    return m


def grass_tuft(seed, height, blades=16):
    rng = np.random.default_rng(seed)
    m = Mesh()
    for i in range(blades):
        az = 2 * np.pi * i / blades + rng.uniform(-0.2, 0.2)
        out = np.array([math.cos(az), 0, math.sin(az)])
        p = out * rng.uniform(0.0, 0.03)
        lean = rng.uniform(0.15, 0.5)
        d = unit(np.array([0, 1.0, 0]) + out * lean)
        pts = [p]
        seg = height * rng.uniform(0.95, 1.25) / 5
        for _ in range(5):
            d = unit(d + out * 0.22)
            p = p + d * seg
            pts.append(p)
        V, F = tube(pts, np.linspace(0.014, 0.002, 6), sides=4, aspect=0.3)
        m.add(V, F, "straw")
    return m


# --------------------------------------------------------------------------
# Asset list: name, PCG slot, builder, expected height range (m)
# --------------------------------------------------------------------------
SPECS = [
    ("SM_Desert_Saguaro_A", "tree", lambda: saguaro(1, 3.6, [(0.45, 20, 0.35, 1.3), (0.55, 200, 0.3, 0.9)]), (2.5, 4.0)),
    ("SM_Desert_Saguaro_B", "tree", lambda: saguaro(2, 2.8, [(0.5, 110, 0.3, 0.7), (0.4, 250, 0.32, 1.0), (0.62, 340, 0.25, 0.4)]), (2.5, 4.0)),
    ("SM_Desert_DeadTree_A", "tree", lambda: dead_tree(3, 3.0), (2.4, 3.6)),
    ("SM_Desert_Rock_A", "rock", lambda: rock(4, (0.7, 0.36, 0.55), layers=4), (0.6, 1.0)),
    ("SM_Desert_Rock_B", "rock", lambda: rock(5, (1.1, 0.75, 0.85), layers=6), (1.2, 2.0)),
    ("SM_Desert_Mesa_Small", "rock", lambda: mesa(6, 3.8, 2.2), (3.0, 5.0)),
    ("SM_Desert_DryShrub_A", "grass", lambda: dry_shrub(7, 0.6), (0.45, 0.8)),
    ("SM_Desert_GrassTuft_A", "grass", lambda: grass_tuft(8, 0.46), (0.3, 0.5)),
]


# --------------------------------------------------------------------------
# GLB writer
# --------------------------------------------------------------------------
def _pad(b, fill=b"\x00"):
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, name, mesh):
    tris, mats = mesh.finalize()
    bin_data = bytearray()
    views, accessors, materials, prims = [], [], [], []

    def add_view(data, target):
        off = len(bin_data)
        bin_data.extend(_pad(data))
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
        return len(views) - 1

    for mi, mat in enumerate(sorted(set(mats))):
        t = tris[mats == mat]
        pos = t.reshape(-1, 3).astype(np.float32)
        fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        fn /= np.linalg.norm(fn, axis=1)[:, None]
        nrm = np.repeat(fn, 3, axis=0).astype(np.float32)
        nv = len(pos)
        if nv <= 65535:
            idx, ctype = np.arange(nv, dtype=np.uint16), 5123
        else:
            idx, ctype = np.arange(nv, dtype=np.uint32), 5125
        accessors.append({"bufferView": add_view(pos.tobytes(), 34962), "componentType": 5126,
                          "count": nv, "type": "VEC3",
                          "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()})
        accessors.append({"bufferView": add_view(nrm.tobytes(), 34962), "componentType": 5126,
                          "count": nv, "type": "VEC3"})
        accessors.append({"bufferView": add_view(idx.tobytes(), 34963), "componentType": ctype,
                          "count": nv, "type": "SCALAR"})
        materials.append({"name": f"M_Desert_{mat}", "doubleSided": False,
                          "pbrMetallicRoughness": {"baseColorFactor": list(PALETTE[mat]) + [1.0],
                                                   "metallicFactor": 0.0, "roughnessFactor": ROUGHNESS}})
        a = len(accessors) - 3
        prims.append({"attributes": {"POSITION": a, "NORMAL": a + 1}, "indices": a + 2, "material": mi, "mode": 4})

    gltf = {
        "asset": {"version": "2.0", "generator": "generate_desert_set.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": materials,
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
def verify_glb(path):
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

    def read(acc_i, dtype, ncomp):
        acc = gltf["accessors"][acc_i]
        bv = gltf["bufferViews"][acc["bufferView"]]
        arr = np.frombuffer(bin_chunk, dtype=dtype, count=acc["count"] * ncomp,
                            offset=bv["byteOffset"] + acc.get("byteOffset", 0))
        return arr.reshape(acc["count"], ncomp) if ncomp > 1 else arr

    tris, top, miny, vol = 0, 0.0, 1e9, 0.0
    for prim in gltf["meshes"][0]["primitives"]:
        pacc = gltf["accessors"][prim["attributes"]["POSITION"]]
        assert "min" in pacc and "max" in pacc, f"{path}: POSITION accessor lacks min/max"
        iacc = gltf["accessors"][prim["indices"]]
        idx = read(prim["indices"], np.uint16 if iacc["componentType"] == 5123 else np.uint32, 1)
        assert idx.max() < pacc["count"], f"{path}: index out of range"
        assert len(idx) % 3 == 0
        pos = read(prim["attributes"]["POSITION"], np.float32, 3).astype(float)
        nrm = read(prim["attributes"]["NORMAL"], np.float32, 3).astype(float)
        assert np.allclose(np.linalg.norm(nrm, axis=1), 1, atol=1e-3), f"{path}: non-unit normals"
        assert np.allclose(pos.min(axis=0), pacc["min"], atol=1e-6) and np.allclose(pos.max(axis=0), pacc["max"], atol=1e-6)
        t = pos[idx].reshape(-1, 3, 3)
        fn = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        # stored normals must agree with winding (face normal direction)
        assert (np.einsum("ij,ij->i", fn, nrm[idx].reshape(-1, 3, 3)[:, 0]) > 0).all(), f"{path}: normal/winding mismatch"
        vol += np.einsum("ij,ij->i", t.mean(axis=1), fn).sum() / 6  # signed volume
        tris += len(t)
        top = max(top, pacc["max"][1])
        miny = min(miny, pacc["min"][1])
    assert abs(miny) < 1e-4, f"{path}: min y = {miny}, expected 0"
    assert vol > 0, f"{path}: negative signed volume -> inverted normals"
    assert tris <= MAX_TRIS, f"{path}: {tris} tris > {MAX_TRIS}"
    return tris, top


# --------------------------------------------------------------------------
# Preview contact sheet (matplotlib only)
# --------------------------------------------------------------------------
def render_preview(results, out_png):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    light = unit([0.4, 0.8, 0.5])
    fig = plt.figure(figsize=(18, 9), dpi=100)
    for k, (name, slot, mesh, tris_n, h) in enumerate(results):
        tris, mats = mesh.finalize()
        fn = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        fn /= np.linalg.norm(fn, axis=1)[:, None]
        shade = 0.45 + 0.55 * np.clip(fn @ light, 0, 1)
        base = np.array([PALETTE[m] for m in mats])
        cols = np.clip(base * shade[:, None], 0, 1)
        ax = fig.add_subplot(2, 4, k + 1, projection="3d")
        polys = tris[:, :, [0, 2, 1]]  # glTF Y-up -> mpl Z-up
        ax.add_collection3d(Poly3DCollection(polys, facecolors=cols, edgecolors="none"))
        ext = max(np.abs(tris[:, :, [0, 2]]).max(), h / 2) * 1.05
        ax.set_xlim(-ext, ext); ax.set_ylim(-ext, ext); ax.set_zlim(0, 2 * ext)
        ax.set_box_aspect((1, 1, 1))
        ax.view_init(elev=18, azim=-55)
        ax.set_axis_off()
        ax.set_title(f"{name}\n[{slot}]  H={h:.2f} m  {tris_n} tris", fontsize=9)
    fig.suptitle("Desert asset set (glTF Y-up, meters, pivot at base)", fontsize=12)
    fig.tight_layout()
    fig.savefig(out_png)


def main():
    MESH_DIR.mkdir(parents=True, exist_ok=True)
    results = []
    print(f"{'name':<26}{'slot':<7}{'tris':>6}{'height[m]':>11}{'bytes':>9}")
    for name, slot, build, (hmin, hmax) in SPECS:
        mesh = build()
        path = MESH_DIR / f"{name}.glb"
        write_glb(path, name, mesh)
        tris, h = verify_glb(path)
        assert hmin <= h <= hmax, f"{name}: height {h:.2f} outside {hmin}-{hmax}"
        print(f"{name:<26}{slot:<7}{tris:>6}{h:>11.2f}{path.stat().st_size:>9}")
        results.append((name, slot, mesh, tris, h))
    render_preview(results, ROOT / "preview.png")
    print(f"OK: {len(results)} meshes verified, preview -> {ROOT / 'preview.png'}")


if __name__ == "__main__":
    main()
