# Verdosx-karnel
A 1992-inspired, 2026-secured monolithic kernel. Written in Rust for memory safety. No bloat, full control. The sovereign OS foundation.
# 🧬 Verdosx

![Status](https://img.shields.io/badge/Status-In_Development-orange)
![Kernel](https://img.shields.io/badge/Kernel-Monolithic-blue)
![Language](https://img.shields.io/badge/Language-Rust-red)
![License](https://img.shields.io/badge/License-Apache_2.0-blue)

> **Le noyau qui n'a pas besoin de 30 ans de legacy pour être rapide.**
> **Meilleur que Windows. Pas par prétention. Par design.**

---

## 🛑 Le Problème avec les OS Modernes

Windows a été conçu en 1985. Il traîne 40 ans de code legacy, des couches de compatibilité, et des failles de sécurité par milliers. 
Linux a été conçu en 1991. Il est génial, mais il est devenu un monstre de 30 millions de lignes que même Linus ne peut plus lire entièrement.

Nous avons décidé de repartir de zéro.

## 🚀 Verdosx : L'Alternative Souveraine

Verdosx n'est pas un "Windows-like". C'est un **noyau monolithique moderne**, écrit en **Rust** pour garantir la sécurité mémoire, et inspiré de la simplicité des systèmes des années 90 (quand un OS tenait sur une disquette).

### Pourquoi Verdosx est Meilleur que Windows ?

| Critère | Windows 11 | Verdosx |
| :--- | :--- | :--- |
| **Langage** | C / C++ (Legacy) | **Rust (Memory Safe)** |
| **Taille du noyau** | ~10 Go (avec bloatware) | **< 10 Mo (minimaliste)** |
| **Télémétrie** | Forcée, invasive | **Zéro. 100% Local.** |
| **Sécurité** | Dépend des patches mensuels | **Sécurité par conception** |
| **Boot Time** | 15 secondes | **< 2 secondes (objectif)** |
| **Contrôle** | Microsoft décide | **Vous décidez.** |

---

## 🧬 L'Architecture

Verdosx est né de la fusion de mon premier système (Verdos) et de la lignée des noyaux Unix/Linux.

- **Langage** : Rust (no_std) + Assembly.
- **Bootloader** : Limine (moderne, UEFI).
- **Cible** : x86_64 (bare metal).
- **Philosophie** : Monolithique, minimal, auditable.

## 📜 Roadmap

- [x] Conception de l'architecture (Verdos Revival)
- [ ] Bootloader (UEFI) fonctionnel
- [ ] "Hello World" sur VGA
- [ ] Gestion de la mémoire (Paging)
- [ ] Interruptions (IDT)
- [ ] Pilote clavier
- [ ] Shell basique
- [ ] **Objectif final : Booter sur un vrai PC.**

---

## 🛠️ Comment compiler

*(À compléter une fois le code source disponible)*

```bash
git clone https://github.com/Aissamohammedi88/verdosx.git
cd verdosx
cargo build --release
# Créer l'image ISO avec Limine
make iso

🤝 Contribuer
. Démarrer en VM avant la clé USB
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

Si vous en avez marre de Windows et de ses mises à jour forcées, rejoignez le mouvement.
📜 Licence

Apache 2.0.
