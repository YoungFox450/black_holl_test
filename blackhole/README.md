# Trou noir : moteur de rendu ray tracing (C++ / OpenGL)

On ne simule pas les étoiles : on simule la **lumière**. Pour chaque pixel, un
rayon part de la caméra et on calcule sa trajectoire courbée par la gravité du
trou noir. Selon l'endroit où il finit, le pixel prend la couleur de
l'horizon (noir), du disque d'accrétion ou du fond de galaxie.

| Avec disque d'accrétion | Sans disque (lentille gravitationnelle seule) |
|---|---|
| ![rendu](docs/rendu.jpg) | ![rendu sans disque](docs/rendu-sans-disque.jpg) |

## Organisation

```
blackhole/
├── CMakeLists.txt         build (télécharge GLFW automatiquement)
├── external/glad/         chargeur OpenGL 3.3 core (fichiers générés)
├── shaders/
│   ├── fullscreen.vert    triangle plein écran : 1 pixel = 1 rayon
│   ├── noise.glsl         bruit procédural partagé
│   ├── sky.frag           fond de galaxie, calculé une fois dans une cubemap
│   ├── blackhole.frag     LE ray tracer : géodésiques, disque
│   └── present.frag       agrandit l'image + tone mapping
└── src/
    ├── main.cpp           fenêtre, caméra orbitale, boucle de rendu
    └── shader.cpp/.hpp    compilation des shaders
```

Tout le calcul se fait sur la carte graphique, dans `shaders/blackhole.frag`.
Le C++ ouvre la fenêtre, gère la caméra et dessine un triangle qui couvre
l'écran.

## Performances (carte graphique intégrée Intel)

Le moteur vise un PC modeste (i5 7e génération, Intel HD/UHD 620, OpenGL 3.3) :

1. **Fond de ciel précalculé.** Le fond ne dépend que de la direction : il
   est calculé une seule fois au démarrage dans une cubemap 1024×1024×6
   (`sky.frag`), au lieu de ~300 appels de bruit par pixel à chaque image.
2. **Rendu en résolution réduite, réglée automatiquement.** Le ray tracing
   se fait dans une image plus petite que la fenêtre, puis `present.frag`
   l'agrandit. Le temps GPU est mesuré à chaque image et la résolution
   s'ajuste pour tenir ~30 FPS (`--fps 60` pour viser plus).
3. **Pas d'intégration proportionnel à `r`** (~0,2 radian par pas) : ~22 pas
   par rayon en moyenne au lieu de plusieurs centaines près du trou noir, sans
   écart visible avec un rendu de référence 20 fois plus fin.
4. **Arrêt anticipé** : un rayon qui descend sous la sphère de photons
   (1,5 rs) est forcément capturé, on arrête de le suivre.

Mesuré avec le même rendu logiciel (llvmpipe) en 1280×720 : avant ~218 ms
par image, maintenant ~56 ms en pleine résolution et ~16 ms en
demi-résolution. Pour mesurer sur ta machine :

```
./build/bin/blackhole --bench 50 --scale 0.5
```

## La physique utilisée

Unités : `G = c = 1`, et le rayon de Schwarzschild `rs = 2GM/c² = 1`.

1. **Trajectoire de la lumière (géodésique nulle de Schwarzschild).**
   L'orbite d'un photon autour d'une masse vérifie l'équation de Binet :

   `d²u/dφ² + u = (3/2)·rs·u²`  avec `u = 1/r`

   Sans le terme de droite, le rayon irait tout droit. En coordonnées
   cartésiennes 3D, c'est équivalent à une accélération centrale :

   `d²x/dλ² = −(3/2)·rs·h²·x / r⁵`  où `h = |x × v|` est constant

   On l'intègre pas à pas avec **Runge-Kutta 4** (pas plus petits près du trou
   noir).

2. **Horizon des événements** : si le rayon passe sous `r = rs`, il ne
   ressortira jamais, le pixel est noir. L'ombre visible est plus grande que
   l'horizon : son rayon apparent vaut `(3√3/2)·rs ≈ 2,6 rs` (les rayons qui
   passent plus près tombent en spirale depuis la sphère de photons à
   `1,5 rs`). Le rendu retrouve bien cette taille.

3. **Disque d'accrétion** (dans le plan `y = 0`, de `3 rs` à `12 rs`).
   Le bord intérieur est l'ISCO, la dernière orbite circulaire stable
   (`6GM/c² = 3 rs`).
   - Température d'un disque mince : `T ∝ r^(−3/4)·(1 − √(r_in/r))^(1/4)`
   - Vitesse orbitale : `β = √(M / (r − 2M))`
   - Effet Doppler relativiste : `D = 1 / (γ·(1 − β·cos θ))`
     (le côté qui vient vers nous est plus brillant et plus bleu)
   - Décalage gravitationnel vers le rouge : `√(1 − rs/r)`
   - Intensité observée : `I_obs = g⁴·I_émis` avec `g = D·√(1 − rs/r)`

   On voit aussi le dessus **et** le dessous du disque au-dessus et
   au-dessous de l'ombre : c'est la lumière de l'arrière du disque, courbée
   par-dessus le trou noir.

4. **Fond de galaxie** : une fonction procédurale de la direction (étoiles,
   bande lumineuse type Voie lactée, poussière, nébuleuses). Quand un rayon
   s'échappe, on lit le fond dans sa direction **finale**, déviée : c'est ce
   qui produit l'effet de lentille gravitationnelle (images doubles, anneau
   d'Einstein).

5. **Animation du gaz.** Le temps de simulation `t` est en unités `rs/c`.
   - Le gaz tourne à la vitesse angulaire képlérienne vue depuis l'infini,
     `Ω(r) = dφ/dt = √(M/r³)` : l'intérieur tourne bien plus vite que
     l'extérieur, ce qui étire la matière en spirales.
   - Des amas de gaz chaud perdent lentement du moment cinétique et
     spiralent de ~10 rs jusqu'à l'ISCO. Leur angle est l'intégrale exacte
     de Ω : `φ(t) = φ0 + (2√M / v_r)·(1/√r(t) − 1/√r0)`. En tombant ils
     chauffent, car leur température suit `T(r)`.
   - Doppler et décalage gravitationnel s'appliquent à toute cette matière,
     donc un amas brille fort quand il arrive vers nous et s'éteint en
     repartant.

## Installer les outils

- **VS Code** avec les extensions recommandées (VS Code les propose à
  l'ouverture du dossier) : C/C++, CMake Tools, Shader languages support.
- **CMake ≥ 3.16** et **git** (GLFW est téléchargé au premier configure).
- **Un compilateur C++17** :
  - Windows : « Build Tools for Visual Studio » (charge de travail
    *Développement Desktop en C++*), puis ouvrir VS Code depuis
    « Developer PowerShell for VS ». MinGW via MSYS2 marche aussi.
  - Linux : `sudo apt install build-essential cmake git libgl1-mesa-dev
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`
  - macOS : `xcode-select --install` et `brew install cmake`.
- Une carte graphique compatible **OpenGL 3.3**.

## Compiler et lancer dans VS Code

Ouvrir la **racine du dépôt** dans VS Code, puis :

- `Ctrl+Maj+B` : configure et compile (tâche « CMake: compiler »).
- `F5` : compile puis lance sous le débogueur (choisir la configuration
  « gdb / lldb » ou « Visual Studio / MSVC » selon le compilateur).
- Ou `Terminal > Exécuter la tâche… > Lancer le trou noir`.

En ligne de commande :

```
cmake -S blackhole -B build
cmake --build build --config Release
./build/bin/blackhole
```

## Panneau de contrôle

Un panneau (Dear ImGui) s'affiche en haut à gauche ; `F1` ou `Tab` le cache.
Il règle sans raccourci clavier :

![panneau de contrôle](docs/panneau.jpg)


- **Simulation** : pause, vitesse du temps, retour à t = 0.
- **Trou noir** : masse en masses solaires. L'image ne change pas (tout est
  calculé en rs), mais le panneau convertit en vraies grandeurs : taille de
  l'horizon, de la sphère de photons, de la dernière orbite stable, durée
  d'un tour, temps écoulé.
- **Disque d'accrétion** : rayons intérieur et extérieur, température
  maximale, luminosité, nombre d'amas chauds, et on peut couper l'effet
  Doppler ou le décalage gravitationnel pour voir leur rôle.
- **Caméra** : distance, angles, champ de vision, orbite automatique.
- **Rendu** : FPS et temps GPU, résolution (auto ou fixe), FPS visé, pas
  max par rayon, exposition, rechargement des shaders.

Quand la souris est sur le panneau, elle ne fait pas tourner la caméra.

## Commandes

Les lettres marchent en AZERTY comme en QWERTY.

| Action | Effet |
|---|---|
| clic gauche + glisser | tourner autour du trou noir (la caméra garde de l'élan) |
| flèches, ou ZQSD (WASD en QWERTY) | tourner autour du trou noir |
| molette, ou Page↑ / Page↓ | zoom avant / arrière (jusqu'à 2,5 rs) |
| `Espace` | orbite automatique de la caméra |
| `C` | recentrer la caméra |
| `P` | pause de la simulation |
| `+` / `-` | accélérer / ralentir le temps |
| `H` | afficher / cacher le disque d'accrétion |
| `K` / `L` | baisser / augmenter la résolution du rendu (passe en mode fixe) |
| `O` | résolution automatique (activée au démarrage) |
| `R` | recharger les shaders (modifier `blackhole.frag`, sauvegarder, `R`) |
| `F1` ou `Tab` | afficher / cacher le panneau de contrôle |
| `Échap` | quitter |

La barre de titre affiche les FPS, la résolution du rendu, la vitesse du
temps et la distance.

Options :

| Option | Effet |
|---|---|
| `--fps N` | FPS visé par la résolution automatique (30 par défaut) |
| `--scale S` | résolution fixe, fraction de la fenêtre (0.25 à 1) |
| `--sky N` | taille d'une face du ciel (1024 ; 512 si la mémoire manque) |
| `--bench N` | rend N images hors écran et affiche le temps moyen |

Image fixe sans fenêtre (pleine résolution) :
`blackhole --screenshot rendu.ppm --width 1920 --height 1080 [--no-disk] [--time T] [--yaw A] [--pitch A] [--distance D]`

## Pistes pour la suite

- Trou noir en rotation (métrique de Kerr) : ombre asymétrique.
- Rendu progressif / anti-aliasing (plusieurs rayons par pixel).
- Fond de ciel à partir d'une vraie image HDR de la Voie lactée (il suffit
  de remplir la cubemap avec l'image au lieu de `sky.frag`).
- Bloom autour du disque.
