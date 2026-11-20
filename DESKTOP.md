# FreaxOS 2026 — Bureau « Liquid Glass »

*EN : the Liquid Glass desktop — a complete graphical desktop wired to the
portable FreaxOS kernel, rendered 100 % in software.*

## Aperçu / Overview

Le bureau est **une tâche du noyau** : `desktop_task` remplace la tâche
« init » (le shell) via `kernel_set_init_task()`, pendant que les
sous-systèmes (mm, vfs, sched, /proc) tournent normalement. Toute
l'interface est dessinée dans un framebuffer ARGB32 de 1024×600 par le
moteur logiciel `desktop/gfx.c` — sans SDL, sans OpenGL, sans dépendance
graphique à la compilation. L'affichage passe par X11 ouvert avec
`dlopen()` à l'exécution (zéro dépendance de build) ; sans X11, le mode
`--screenshot` rend les mêmes scènes dans des fichiers.

*The desktop is a kernel task: `desktop_task` replaces the shell as the
"init" task while the kernel subsystems keep running. Everything is drawn
into a 1024×600 ARGB32 framebuffer by the in-house software renderer; X11
is dlopen'd at runtime (no build dependency), and a headless
`--screenshot` mode renders the same scenes to files.*

## Composition du code / Code layout

| Module | Rôle |
|---|---|
| `desktop/gfx.c` + `font8x14.c` | primitives 2D (blend alpha, arrondis, dégradés, flou box blur) + police bitmap 9×16 générée (`scripts/genfont.py`, ASCII + accents français) |
| `desktop/theme.c` | 5 fonds d'écran procéduraux, couleur d'accent, intensité du verre, langue FR/EN ; persistance dans `/etc/freaxdesktop.conf` via le VFS du noyau |
| `desktop/html.c` | mini moteur HTML (h1–h3, p, b, i, li, hr, br, pre, liens internes, entités) pour le Navigateur |
| `desktop/wm.c` | gestionnaire de fenêtres : déplacement, z-order, focus, réduire/maximiser/fermer, barre des tâches « liquid glass » (flou du fond + teinte), launcher en grille, desklets |
| `desktop/apps1.c` / `apps2.c` | les 10 applications + icônes vectorielles + registre |
| `desktop/x11_host.c` | frontend X11 via `dlopen("libX11.so.6")`, `XPutImage`, traduction souris/clavier |
| `desktop/desktop_main.c` | point d'entrée : peuplement du ramfs (pages `/www`, bundles `/repo`), mode interactif et mode `--screenshot` |
| `baremetal/splash.c` | splash graphique au boot i386 (mode VGA 13h par écritures directes, 4 s ou touche) |

## Le « liquid glass » / The liquid glass

La barre des tâches, les barres de titre, le launcher et les menus sont
composés en trois passes : **flou box-blur** du fond derrière la zone
(rayon piloté par l'intensité), **teinte translucide** (alpha 40–160 selon
le réglage), puis **bord lumineux** d'un pixel. Les corps de fenêtres sont
translucides mais suffisamment opaques pour rester lisibles. Le tout est
une question de blend alpha dans `gfx.c` — aucun effet GPU.

*Taskbar, title bars, launcher and menus are composed with a box blur of
whatever is behind, a translucent tint and a one-pixel light border — pure
alpha blending, no GPU.*

## Applications / Applications

| App | Ce qui la relie au noyau |
|---|---|
| **Terminal** | exécute le **vrai shell** `freaxsh` via `shell_exec_line()`, sortie capturée par `console_set_capture()`, historique ↑/↓ |
| **Fichiers** | parcourt le **VFS du noyau** (`vfs_list`, `vfs_resolve`) ; un clic sur un fichier l'ouvre dans l'Éditeur |
| **Navigateur** | rend les pages `/www/*.html` du ramfs avec le moteur intégré ; liens internes cliquables ; barre d'URL `freax://freaxos.test/...` |
| **Boutique** | installe/désinstalle réellement des bundles : `/repo/<id>.app` → `/apps/<id>.app`, registre `/etc/apps.conf` ; le launcher lit ce registre |
| **Réglages** | fonds d'écran, couleur d'accent, intensité du verre, langue FR/EN — appliqués en direct et **persistés** dans `/etc/freaxdesktop.conf` |
| **Extensions** | desklets Horloge / Moniteur / Notes, applets tray CPU & MEM — à cocher, persistés |
| **Éditeur** | lit/écrit les fichiers du ramfs via `vfs_read`/`vfs_write` |
| **Calculatrice** | calculs entiers, clavier ou souris |
| **Moniteur** | lit `/proc/meminfo`, `/proc/tasks`, `/proc/uptime` et le journal `/proc/klog` du noyau, rafraîchi chaque seconde |
| **À propos** | mini-Tux, histoire du 0.12 (GPL, février 1992) et crédits |

## API d'installation / Install API

```c
#include "desktop/desktop.h"
store_install("calc");        /* 0 = ok : copie /repo/calc.app -> /apps/ */
store_uninstall("calc");      /* retire le bundle + met à jour /etc/apps.conf */
store_is_installed("calc");   /* 1 si présent dans le registre */
```

Le launcher affiche les apps « de base » (terminal, files, browser, store,
settings) en permanence et les autres selon le registre. Les bundles sont
de vrais fichiers VFS avec un en-tête `[bundle]`.

## Lancer le bureau / Running the desktop

```bash
make desktop                     # construit build/freax-desktop
./build/freax-desktop            # fenêtre X11 (libX11 trouvée via dlopen)
make shot                        # 6 scènes sans écran -> docs/img/*.ppm
python3 scripts/ppm2png.py docs/img   # (option) PPM -> PNG
./run_freax.sh --desktop         # build + tests + lancement du bureau
```

Raccourcis : **F2** ouvre/ferme le launcher, **Échap** le referme, les
fenêtres se déplacent par leur barre de titre, la molette fait défiler le
Navigateur, Fichiers et le Terminal garde l'historique.

Sans X11 (SSH, CI) : `--screenshot` rend les mêmes scènes en PPM ; les PNG
officiels sont livrés dans `docs/img/` — c'est aussi la façon la plus
rapide de « voir le visuel » sans compiler.

## Splash bare-metal / Bare-metal splash

Au boot QEMU/réel, `baremetal/splash.c` programme le VGA en **mode 13h**
(320×200, 256 couleurs @ 0xA0000) par écritures directes des registres
(sequencer, CRTC, graphics controller, attribute controller, DAC) — aucun
appel BIOS en mode protégé — dessine le visuel Liquid Glass avec la même
police que le bureau, attend une touche ou 4 s, puis restaure le mode
texte 03h et démarre le shell. La sortie série COM1 n'est pas touchée :
`make smoke` continue de fonctionner.

```bash
qemu-system-i386 -kernel build/freax-i386.elf -m 32M -serial stdio
```

## VM avant la clé USB / VM before the USB stick

```bash
# ISO bootable GRUB (grub-mkrescue), comme une vraie installation
mkdir -p iso/boot/grub
cp build/freax-i386.elf iso/boot/
printf 'set timeout=0\nmenuentry "FreaxOS 2026" {\n  multiboot /boot/freax-i386.elf\n  boot\n}\n' \
  > iso/boot/grub/grub.cfg
grub-mkrescue -o freax.iso iso/
qemu-system-i386 -cdrom freax.iso -m 32M -serial stdio
```

Une fois validé en VM, la même ISO gravée sur clé USB boote pareil :

```bash
sudo dd if=freax.iso of=/dev/sdX bs=4M status=progress && sync
```

*(BIOS/legacy boot ; le UEFI natif n'est pas câblé dans cette version.)*

## Limites assumées / Known limits

- Le Navigateur est **hors-ligne** : pas de pile TCP/IP dans cette version,
  il rend les pages internes du ramfs (l'architecture réseau reste à venir).
- Le Moniteur tray CPU/MEM est un indicateur **dérivé de l'activité réelle
  de l'ordonnanceur**, pas un compteur matériel.
- Une instance par application (réouvrir focalise au lieu de dupliquer).
- Le bureau tourne côté hôte (X11/screenshots) ; en bare-metal, le visuel
  est le splash graphique puis le shell texte VGA.
