# Black Hole Test

Moteur de rendu **ray tracing** d'un trou noir, écrit en **C++ / OpenGL**,
pensé pour tourner en temps réel sur un PC modeste à carte graphique intégrée.

![Rendu du trou noir avec son disque d'accrétion](blackhole/docs/rendu.jpg)

## L'idée

On ne simule pas les étoiles : on simule **la lumière**. Pour chaque pixel,
un rayon part de la caméra et on calcule sa trajectoire, courbée par la
gravité du trou noir (géodésiques de Schwarzschild). Selon l'endroit où il
finit, le pixel prend la couleur :

- de l'**horizon des événements** (noir) si le rayon tombe dans le trou noir ;
- du **disque d'accrétion** s'il le traverse ;
- du **fond de galaxie**, lu dans la direction *déviée* du rayon, s'il
  s'échappe. C'est ce qui produit la lentille gravitationnelle et l'anneau
  d'Einstein.

| Avec disque d'accrétion | Sans disque (lentille gravitationnelle seule) |
|---|---|
| ![rendu](blackhole/docs/rendu.jpg) | ![rendu sans disque](blackhole/docs/rendu-sans-disque.jpg) |

## Ce que fait le moteur

- Ray tracing entièrement sur la carte graphique, dans un fragment shader
  (1 pixel = 1 rayon), intégration Runge-Kutta 4.
- Ombre du trou noir à la bonne taille (≈ 2,6 rayons de Schwarzschild).
- Disque d'accrétion animé : rotation képlérienne, grumeaux chauds qui
  spiralent vers le centre, effet Doppler relativiste et décalage
  gravitationnel vers le rouge.
- On voit le dessus **et** le dessous du disque autour de l'ombre (lumière de
  l'arrière du disque courbée par le trou noir).
- Fond de galaxie procédural (étoiles, bande type Voie lactée, nébuleuses).
- Caméra libre avec inertie, orbite automatique, pause et vitesse du temps.
- Résolution de rendu réglée automatiquement pour tenir ~30 FPS.
- **Simulation d'étoiles** (touche `E`) : Soleil, Proxima du Centaure,
  Sirius A et B, Rigel, Bételgeuse, Aldébaran, étoile à neutrons, ou une
  étoile créée à partir de sa masse. Couleur de corps noir, bord assombri,
  granulation, taches et lentille gravitationnelle pour les objets compacts.
  Les étoiles peu compactes sont tracées en ligne droite (la déviation y est
  inférieure à 0,1°), ce qui les rend environ 2,5 fois plus rapides.
- **Panneau de contrôle** (Dear ImGui, police Inter, `F1` ou `Tab`) : lecture
  et vitesse du temps toujours visibles, onglets Objet (trou noir, disque,
  étoiles), Vue, Rendu et Aide, ligne d'état avec les FPS, bulle d'aide sur
  chaque réglage.

![Panneau de contrôle](blackhole/docs/panneau.jpg)

![Étoiles simulées](blackhole/docs/etoiles.jpg)

## Démarrage rapide

Outils : **VS Code**, **CMake ≥ 3.16**, **git** et un **compilateur C++17**
(sous Windows : « Build Tools for Visual Studio » ou MinGW via MSYS2).
Une carte graphique compatible **OpenGL 3.3** suffit.

1. Ouvrir la racine du dépôt dans VS Code et accepter les extensions
   recommandées (C/C++, CMake Tools, Shader languages support).
2. `Ctrl+Maj+B` pour compiler, `F5` pour lancer.

En ligne de commande :

```
cmake -S blackhole -B build
cmake --build build --config Release
./build/bin/blackhole
```

GLFW est pris sur le système s'il est installé, sinon CMake le télécharge.
Le détail par système (paquets Linux, macOS) est dans
[`blackhole/README.md`](blackhole/README.md#installer-les-outils).

## Commandes principales

| Action | Effet |
|---|---|
| clic gauche + glisser, flèches, ZQSD / WASD | tourner autour du trou noir |
| molette, Page↑ / Page↓ | zoom |
| `Espace` | orbite automatique |
| `P` / `+` / `-` | pause, accélérer, ralentir le temps |
| `H` | afficher / cacher le disque |
| `F1` ou `Tab` | afficher / cacher le panneau de contrôle |
| `E` | passer du trou noir à une étoile, et retour |
| `N` / `B` | étoile suivante / précédente |
| `K` / `L` / `O` | baisser, augmenter la résolution, ou la laisser automatique |
| `Échap` | quitter |

La liste complète des touches et des options (`--fps`, `--scale`, `--bench`,
`--screenshot`…) est dans [`blackhole/README.md`](blackhole/README.md#commandes).

## Machine cible et performances

Le projet doit tourner sur un **Intel Core i5 7e génération** avec sa
**carte graphique Intel intégrée** et 16 Go de RAM. D'où ces choix :

- **OpenGL 3.3 core** (et pas 4.3 / compute shaders) pour rester compatible
  et léger ;
- fond de ciel calculé une seule fois au démarrage dans une cubemap ;
- ray tracing dans une image plus petite que la fenêtre, puis agrandie ;
- pas d'intégration proportionnel à la distance et arrêt anticipé des
  rayons capturés.

Pour mesurer sur ta machine : `./build/bin/blackhole --bench 50 --scale 0.5`.
Les détails et les mesures sont dans
[`blackhole/README.md`](blackhole/README.md#performances-carte-graphique-intégrée-intel).

## Organisation du dépôt

```
.
├── README.md                 ce fichier
├── .vscode/                  tâches de compilation, débogage, extensions
└── blackhole/
    ├── README.md             documentation technique et physique détaillée
    ├── CMakeLists.txt        build (GLFW système ou téléchargé, glad et ImGui embarqués)
    ├── assets/fonts/         police Inter du panneau (licence OFL)
    ├── docs/                 images de rendu
    ├── external/glad/        chargeur OpenGL 3.3 core (fichiers générés)
    ├── external/imgui/       Dear ImGui (panneau de contrôle)
    ├── shaders/
    │   ├── blackhole.frag    le ray tracer : géodésiques, horizon, disque
    │   ├── star.frag         ray tracer des étoiles
    │   ├── sky.frag          fond de galaxie, calculé une fois en cubemap
    │   ├── present.frag      agrandissement et tone mapping
    │   ├── noise.glsl        fonctions de bruit partagées
    │   └── fullscreen.vert   triangle plein écran
    └── src/
        ├── main.cpp          fenêtre, caméra, temps, boucle de rendu
        ├── app.hpp           état de la simulation partagé
        ├── ui.cpp/.hpp       panneau de contrôle
        ├── ui_kit.cpp/.hpp   thème et composants du panneau
        ├── star.cpp/.hpp     modèle physique des étoiles
        └── shader.cpp/.hpp   chargement et compilation des shaders
```

La physique (équation des photons, horizon, disque, Doppler, modèle des
étoiles) est expliquée pas à pas dans [`blackhole/README.md`](blackhole/README.md#la-physique-utilisée).

## Historique

| Étape | Contenu |
|---|---|
| 1. Moteur de base | Rayons courbés (Schwarzschild, RK4), horizon, disque avec Doppler, fond procédural, config VS Code |
| 2. Caméra et animation | Caméra libre avec inertie, disque d'accrétion animé, contrôle du temps |
| 3. Optimisation Intel | Ciel en cubemap, résolution automatique, pas adaptatif, arrêt anticipé |
| 4. Build et CI | GLFW du système utilisé s'il existe, compilation automatique Linux / Windows sur GitHub |
| 5. Documentation | README racine complet, images de rendu à jour |
| 6. Étoiles et panneau | Simulation d'étoiles, panneau de contrôle Dear ImGui |
| 7. Couleurs | Couleurs de corps noir converties en lumière linéaire : étoiles froides et disque bien orangés |
| 8. Nouveau panneau | Thème moderne avec onglets et cartes, police Inter, étoiles 2,5 fois plus rapides |

## Pistes pour la suite

En préparation (PR ouvertes) :

- activité des étoiles calculée à partir de leur rotation et de leur type
  (taches, cycle, éruptions, oscillations) ;
- pulsars et magnétars, quasar avec jets relativistes, astéroïdes en orbite.

Idées pour la suite :

- Trou noir en rotation (métrique de Kerr) : ombre asymétrique.
- Anti-aliasing (plusieurs rayons par pixel) et bloom autour du disque.
- Fond de ciel à partir d'une vraie image HDR de la Voie lactée.
- Plusieurs objets dans la même scène (étoile en orbite autour du trou noir).

## Régénérer les images de ce README

```
./build/bin/blackhole --screenshot rendu.ppm --width 1280 --height 720 --time 40
./build/bin/blackhole --screenshot sans-disque.ppm --width 1280 --height 720 --no-disk
```

puis convertir les `.ppm` en `.jpg` (par exemple avec ImageMagick) dans
`blackhole/docs/`.
