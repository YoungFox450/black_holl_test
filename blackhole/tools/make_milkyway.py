#!/usr/bin/env python3
"""Génère assets/sky/voie-lactee.rgbe.png : la vraie Voie lactée, en HDR.

Le ciel est calculé à partir de catalogues réels, distribués avec d3-celestial
(Olaf Frohn, licence BSD, https://github.com/ofrohn/d3-celestial) :
  - stars.14.json : 118 000 étoiles Hipparcos / Tycho-2 jusqu'à la magnitude
    ~12, avec leur indice de couleur B-V ;
  - mw.json : contours de la Voie lactée à 5 niveaux de brillance (le Grand
    Rift, les nuages du Sagittaire et du Cygne y sont) ;
  - dsos.14.json : objets du ciel profond (Nuages de Magellan, galaxie
    d'Andromède, nébuleuses d'Orion, de la Carène...).

Sortie : carte équirectangulaire en coordonnées GALACTIQUES (centre de
l'image = centre de la Galaxie, longitude croissante vers la gauche, nord en
haut), lumière linéaire HDR. Elle est encodée en RGBE (Radiance) dans un PNG
RGBA 8 bits : R, G, B = mantisses, A = exposant + 128. Le PNG compresse
bien mieux qu'un .hdr, et stb_image le lit sans code en plus.

Utilisation :
    npm pack d3-celestial@0.7.35 && tar xzf d3-celestial-0.7.35.tgz
    python3 tools/make_milkyway.py package/data assets/sky/voie-lactee.rgbe.png
(numpy, scipy et Pillow requis.)
"""

import json
import math
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

W, H = 4096, 2048

# Repère équatorial J2000 -> galactique (lignes : axes galactiques x, y, z).
EQ_TO_GAL = np.array([
    [-0.0548755604, -0.8734370902, -0.4838350155],
    [0.4941094279, -0.4448296300, 0.7469822445],
    [-0.8676661490, -0.1980763734, 0.4559837762],
])


def unit(lon_deg, lat_deg):
    lon, lat = np.radians(lon_deg), np.radians(lat_deg)
    return np.stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)], axis=-1)


def eq_to_gal(ra, dec):
    v = unit(ra, dec) @ EQ_TO_GAL.T
    return np.degrees(np.arctan2(v[..., 1], v[..., 0])), np.degrees(np.arcsin(np.clip(v[..., 2], -1, 1)))


def gal_to_pixel(l, b):
    """(l, b) en degrés -> (x, y) en pixels flottants de l'image de sortie."""
    x = (0.5 - l / 360.0) % 1.0 * W
    y = (0.5 - b / 180.0) * H
    return x, y


def pixel_directions():
    """Directions galactiques (vecteurs unité) et coordonnées (l, b) des pixels."""
    x = (np.arange(W) + 0.5) / W
    y = (np.arange(H) + 0.5) / H
    l = (0.5 - x) * 360.0
    b = (0.5 - y) * 180.0
    L, B = np.meshgrid(l, b)
    return L, B


def blackbody_rgb(T):
    """Même approximation que les shaders, en lumière linéaire."""
    T = np.clip(T, 1000.0, 40000.0) / 100.0
    r = np.where(T <= 66.0, 1.0, np.clip(1.29293618 * np.power(np.maximum(T - 60.0, 1e-3), -0.1332047592), 0, 1))
    g = np.where(T <= 66.0, np.clip(0.39008157 * np.log(T) - 0.63184144, 0, 1),
                 np.clip(1.12989086 * np.power(np.maximum(T - 60.0, 1e-3), -0.0755148492), 0, 1))
    b = np.where(T >= 66.0, 1.0,
                 np.where(T <= 19.0, 0.0, np.clip(0.54320678 * np.log(np.maximum(T - 10.0, 1e-3)) - 1.19625408, 0, 1)))
    c = np.stack([r, g, b], axis=-1) ** 2.2
    lum = c @ np.array([0.2126, 0.7152, 0.0722])
    return c / np.maximum(lum, 1e-6)[..., None]


def bv_to_temperature(bv):
    """Formule de Ballesteros (2012)."""
    return 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62))


# --- Voie lactée diffuse --------------------------------------------------------

def rasterize_level(polygons, w, h):
    """Masque (h, w) des points du ciel équatorial à l'intérieur des contours.

    Test de parité sur la sphère : on compte les arêtes au nord du point (le
    pôle nord céleste n'est pas dans la Voie lactée). Chaque arête marque les
    colonnes qu'elle couvre à la ligne de sa latitude ; une somme cumulée du
    nord vers le sud donne la parité.
    """
    marks = np.zeros((h + 1, w), dtype=np.int32)
    cols_ra = 180.0 - (np.arange(w) + 0.5) / w * 360.0     # RA de chaque colonne (-180..180, vers la gauche)
    for poly in polygons:
        for ring in poly:
            ring = np.asarray(ring, dtype=np.float64)
            a = ring
            bb = np.roll(ring, -1, axis=0)
            for (lon0, lat0), (lon1, lat1) in zip(a, bb):
                d = (lon1 - lon0 + 540.0) % 360.0 - 180.0  # plus court chemin en longitude
                if abs(d) < 1e-9:
                    continue
                lo, hi = (lon0, lon0 + d) if d > 0 else (lon0 + d, lon0)
                # colonnes dont la RA est dans [lo, hi) (modulo 360)
                rel = (cols_ra - lo) % 360.0
                sel = np.nonzero(rel < (hi - lo))[0]
                if sel.size == 0:
                    continue
                t = rel[sel] / (hi - lo)
                if d < 0:
                    t = 1.0 - t
                lat = lat0 + (lat1 - lat0) * t
                row = np.clip(np.ceil((0.5 - lat / 180.0) * h - 0.5), 0, h).astype(np.int64)
                np.add.at(marks, (row, sel), 1)
    return (np.cumsum(marks, axis=0)[:h] % 2).astype(np.float32)


def milky_way(data_dir, L, B):
    mw = json.load(open(f"{data_dir}/mw.json"))
    ew, eh = 2048, 1024
    level = np.zeros((eh, ew), dtype=np.float32)
    for f in mw["features"]:
        level += rasterize_level(f["geometry"]["coordinates"], ew, eh)
    # Contours adoucis à deux échelles (ce sont des isophotes, pas des bords nets).
    level = 0.5 * ndimage.gaussian_filter(level, 4.0, mode="wrap") + 0.5 * ndimage.gaussian_filter(level, 12.0, mode="wrap")

    # Reprojection équatorial -> galactique.
    v = unit(L, B) @ EQ_TO_GAL                      # galactique -> équatorial
    ra = np.degrees(np.arctan2(v[..., 1], v[..., 0]))
    dec = np.degrees(np.arcsin(np.clip(v[..., 2], -1, 1)))
    px = ((180.0 - ra) / 360.0 * ew - 0.5) % ew
    py = (0.5 - dec / 180.0) * eh - 0.5
    lev = ndimage.map_coordinates(level, [py, px], order=1, mode="grid-wrap")
    return lev  # 0 .. 5


def value_noise(dirs, freq, seed):
    """Bruit de valeur 3D (trilinéaire) évalué sur des directions."""
    rng = np.random.default_rng(seed)
    n = int(freq) * 2 + 4
    grid = rng.random((n, n, n), dtype=np.float32)
    p = (dirs * freq + freq + 1.5).reshape(-1, 3).T
    return ndimage.map_coordinates(grid, p, order=1, mode="wrap").reshape(dirs.shape[:-1])


def fbm(dirs, freq, octaves, seed):
    total, amp, norm = 0.0, 1.0, 0.0
    for i in range(octaves):
        total = total + amp * value_noise(dirs, freq * 2 ** i, seed + i)
        norm += amp
        amp *= 0.5
    return total / norm


# --- Étoiles ------------------------------------------------------------------------

def splat_stars(img, data_dir):
    stars = json.load(open(f"{data_dir}/stars.14.json"))["features"]
    ra = np.array([s["geometry"]["coordinates"][0] for s in stars])
    dec = np.array([s["geometry"]["coordinates"][1] for s in stars])
    mag = np.array([s["properties"]["mag"] for s in stars], dtype=np.float64)
    bv = np.array([float(s["properties"]["bv"]) if s["properties"]["bv"] not in ("", None) else 0.65 for s in stars])
    l, b = eq_to_gal(ra, dec)
    x, y = gal_to_pixel(l, b)
    flux = np.power(10.0, -0.4 * mag)              # magnitude 0 -> 1
    col = blackbody_rgb(bv_to_temperature(np.clip(bv, -0.4, 2.0))) * flux[:, None]
    # Dépôt bilinéaire : l'énergie est conservée, pas de crénelage.
    x0 = np.floor(x - 0.5).astype(np.int64)
    y0 = np.floor(y - 0.5).astype(np.int64)
    fx = (x - 0.5) - x0
    fy = (y - 0.5) - y0
    for dx, dy, w in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        xi = (x0 + dx) % W
        yi = np.clip(y0 + dy, 0, H - 1)
        for c in range(3):
            np.add.at(img[..., c], (yi, xi), col[:, c] * w)
    print(f"{len(stars)} étoiles")


# --- Objets étendus -------------------------------------------------------------

# (désignation, couleur, éclat de surface)
EXTENDED = [
    ("LMC", (0.85, 0.85, 1.0), 0.006),
    ("SMC", (0.85, 0.85, 1.0), 0.004),
    ("M 31", (1.0, 0.9, 0.75), 0.010),
    ("M 33", (0.85, 0.88, 1.0), 0.004),
    ("M 42", (1.0, 0.35, 0.45), 0.030),
    ("NGC 3372", (1.0, 0.35, 0.45), 0.020),
    ("M 8", (1.0, 0.35, 0.45), 0.015),
    ("M 20", (1.0, 0.45, 0.6), 0.010),
    ("NGC 7000", (1.0, 0.3, 0.4), 0.006),
    ("M 45", (0.6, 0.7, 1.0), 0.010),
]


def extended_objects(img, data_dir, gdirs):
    dsos = {f["properties"]["desig"]: f for f in json.load(open(f"{data_dir}/dsos.14.json"))["features"]}
    for name, color, sb in EXTENDED:
        f = dsos.get(name)
        if not f:
            print("absent :", name)
            continue
        ra, dec = f["geometry"]["coordinates"]
        dims = [float(d) for d in str(f["properties"]["dim"]).split("x")]
        a = dims[0] / 60.0 * 0.5                     # demi-grand axe (degrés)
        bax = (dims[1] if len(dims) > 1 else dims[0]) / 60.0 * 0.5
        c = unit(np.array(ra), np.array(dec)) @ EQ_TO_GAL.T
        cosd = gdirs @ c
        dist = np.degrees(np.arccos(np.clip(cosd, -1, 1)))
        sigma = 0.35 * math.sqrt(a * bax)
        blob = np.exp(-0.5 * (dist / sigma) ** 2) * (dist < 4 * sigma)
        img += blob[..., None] * np.array(color, dtype=np.float32) * sb


def encode_rgbe_png(img, path):
    m = img.max(axis=-1)
    e = np.where(m > 1e-32, np.floor(np.log2(np.maximum(m, 1e-32))) + 1, -128)
    scale = np.where(m > 1e-32, 256.0 / np.power(2.0, e), 0.0)
    rgb = np.clip(np.floor(img * scale[..., None]), 0, 255).astype(np.uint8)
    a = np.clip(e + 128, 0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([rgb, a]), "RGBA").save(path, optimize=True)


def main():
    data_dir, out = sys.argv[1], sys.argv[2]
    L, B = pixel_directions()
    gdirs = unit(L, B).astype(np.float32)

    lev = milky_way(data_dir, L, B)
    # Texture : nuages d'étoiles et poussière, plus fins près du plan.
    clumps = fbm(gdirs, 24.0, 4, 1)
    dust = fbm(gdirs, 40.0, 3, 9)
    near_plane = np.exp(-np.abs(B) / 6.0)
    texture = (0.35 + 1.3 * clumps) * (1.0 - 0.6 * near_plane * np.clip((dust - 0.45) * 3.0, 0, 1))
    glow = np.power(lev / 5.0, 2.2) * texture
    # Fond d'étoiles non résolues : plus dense vers le plan et le centre.
    to_center = 0.5 + 0.5 * np.cos(np.radians(L)) * np.cos(np.radians(B))
    glow += 0.06 * np.exp(-np.abs(B) / 20.0) * (0.5 + to_center)

    # Couleur : bulbe jaune-orangé vers le centre, disque blanc-bleuté ailleurs.
    warm = np.array([1.0, 0.82, 0.62], dtype=np.float32)
    cool = np.array([0.82, 0.88, 1.0], dtype=np.float32)
    mixc = np.power(to_center, 3.0)[..., None]
    img = (glow[..., None] * (warm * mixc + cool * (1 - mixc)) * 0.006).astype(np.float32)

    extended_objects(img, data_dir, gdirs)
    splat_stars(img, data_dir)
    print("min/moy/max :", img.min(), img.mean(), img.max())
    encode_rgbe_png(img, out)


if __name__ == "__main__":
    main()
