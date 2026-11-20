1. Voir le visuel immédiatement (sans rien compiler)

Les captures officielles sont incluses dans le zip : dézippe et open  docs/img/— desktop-01-bureau-liquid-glass.png, desktop-02-launcher.png, desktop-03-navigateur.png, desktop-04-boutique.png, plus les Réglages thème Menthe et le thème Dark Pro.

Chez toi, après extraction :
Bash

cd ~/FreaxOS && ./run_freax.sh --no-install --no-gui   # build + tests + 6 captures

2. Ce que j'ai construit — le bureau complet relié au noyau

Le bureau est une du groupe :  desktop_taskLe remplacement le shell comme « init » (kernel_set_init_task), pendentif que mm/vfs/sched//proc tourner. Rendu 100% logiciel ARGB32 1024×600, zéro dépendance graphique (X11 ouvert par  dlopenà l'exécution).

    Gestionnaire de fenêtres :, déplacement z-order, focus, paysage/maximiseur/fermer
    Barre des liquides de travail en verre : louf du fond +te teint translucide + bord lumineux, intensité réglable
    10 applications : Terminal (le vrai freaxsh, historique ↑/↓), Fichiers (VFS), Navigateur (moteur HTML maison, liens cliquables), Boutique (entraîner  /repo/*.app→  /apps+ registre /etc/apps.conf, le coup de lanceur), Réglages (5 fondsuraux d'arrêt, couleur d'accent, intensité du verre, langue verre  — persistés dans /etc/freaxdesktop.conf), Extensions (bureaux Horloge/Moniteur/Notes, plateau d'applets CPU/MEM au choix), Éditeur, Calculatrice, Moniteur (lit le vrai /proc), À proposer (mini-Tux)
    Splash graphique bare-metal : au boot QEMU/clé USB, mode VGA 13h programmé par writings directs de registres visuel — Liquid Glass 4 s, puis le shell
    Launcher en grille avec citations vectorielles, des bulletins flottants, notifications de progression d'installation

Validation : 398 tests unitaires verts (74 nouveaux pour le bureau), zéro avertissement -Wall -Wextra,  run_freax.shcomplet = 6 PASS / 0 FAIL, et j'ai corrigé 4 bugs par trouvés la vérification visuelle (traits alpha opaques, UTF-8 3 octets « ??? », parse  /procà strtok partagé, comestivation des titres).
3. Démarrer en VM avant la clé USB
Bash

cd ~/FreaxOS
make clean && make all          # build complet (sim + tests + desktop + noyau i386)
qemu-system-i386 -kernel build/freax-i386.elf -m 32M -serial stdio
                                # → splash graphique, puis shell texte
./build/freax-desktop           # le bureau interactif en fenêtre X11
./run_freax.sh --desktop        # tout-en-un : build + tests + bureau

Clé USB (une fois validée en VM) :
Bash

make iso 2>/dev/null || { mkdir -p iso/boot/grub; cp build/freax-i386.elf iso/boot/
printf 'set timeout=0\nmenuentry "FreaxOS 2026" {\n  multiboot /boot/freax-i386.elf\n  boot\n}\n' > iso/boot/grub/grub.cfg
grub-mkrescue -o freax.iso iso/; }
sudo dd if=freax.iso of=/dev/sdX bs=4M status=progress && sync

Limites assumunées (dans docs/DESKTOP.md)

Le Navigateur est hors-ligne (pages internes du ramfs — pas encore de pile TCP/IP), les applets CPU/MEM est de l'activité réelle de l'ordonnanceur, une instance par app, et le bureau interactif tourne histoire ter pair (X11) que le bare-metal montre le splash le puis shell texte VGA.

Fichiers: FreaxOS-2026-v1.0.0.zip (91 fichiers, intégrité e) — fourchettes: desktop/, baremetal/splash.c, docs/DESKTOP.md, moins capture PNG, tests/test_desktop.c, Makefile et  run_freax.shà jour.
