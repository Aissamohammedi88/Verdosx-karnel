#!/usr/bin/env bash
# =============================================================================
# run_freax.sh — Compile, TESTE DIRECTEMENT et lance FreaxOS 2026 sur QEMU
#
# FR : 1) installe les outils manquants   2) compile (make)
#      3) TESTS DIRECTS : unitaires + simulateur + ELF multiboot
#      4) smoke test QEMU sans écran (sortie COM1 vérifiée)
#      5) lance QEMU interactif (ISO GRUB si disponible, sinon -kernel)
#
# EN : 1) installs missing tools  2) builds (make)
#      3) DIRECT TESTS: unit tests + hosted simulator + multiboot ELF
#      4) headless QEMU smoke test (COM1 output checked)
#      5) launches interactive QEMU (GRUB ISO if available, else -kernel)
#
# Usage : ./run_freax.sh [--no-install] [--no-gui] [--desktop]
# =============================================================================
set -u

# Couleurs
G='\033[0;32m'; R='\033[0;31m'; C='\033[0;36m'; Y='\033[1;33m'; N='\033[0m'

# Compteurs de tests / test counters
PASS=0; FAIL=0; SKIP=0

ok()   { echo -e "  ${G}✓ PASS${N} — $1"; PASS=$((PASS+1)); }
ko()   { echo -e "  ${R}✗ FAIL${N} — $1"; FAIL=$((FAIL+1)); }
skip() { echo -e "  ${Y}○ SKIP${N} — $1"; SKIP=$((SKIP+1)); }
rule() { echo -e "\n${C}-----------------------------------------------${N}"; }

# Toujours travailler depuis la racine du projet / always run from project root
cd "$(dirname "$0")" || exit 1

NO_INSTALL=0; NO_GUI=0; DESKTOP_MODE=0
for a in "$@"; do
    case "$a" in
        --no-install) NO_INSTALL=1 ;;
        --no-gui)     NO_GUI=1 ;;
        --desktop)    DESKTOP_MODE=1 ;;
        *) echo "option inconnue / unknown option : $a" ;;
    esac
done

echo -e "${C}===============================================${N}"
echo -e "${C} 🚀 FREAXOS 2026 — Compilation & TEST DIRECT    ${N}"
echo -e "${C}===============================================${N}"

# -----------------------------------------------------------------------------
# 1. Prérequis / prerequisites (QEMU, GRUB, compilateurs)
# -----------------------------------------------------------------------------
echo -e "${Y}▸ Vérification des outils (qemu, grub, gcc, make)...${N}"
MISSING=""
for cmd in qemu-system-i386 grub-mkrescue gcc make; do
    command -v "$cmd" &>/dev/null || MISSING="$MISSING $cmd"
done

if [ -n "$MISSING" ]; then
    if [ "$NO_INSTALL" -eq 1 ]; then
        echo -e "${Y}Outils absents :$MISSING (--no-install : installation sautée)${N}"
    elif ! command -v sudo &>/dev/null; then
        echo -e "${Y}Outils absents :$MISSING${N}"
        echo -e "${Y}Lance manuellement : sudo apt update && sudo apt install -y \\"
        echo -e "  qemu-system-x86 grub-pc-bin grub-common xorriso mtools gcc make${N}"
    else
        echo -e "${Y}Installation des paquets manquants :$MISSING${N}"
        sudo apt update
        sudo apt install -y qemu-system-x86 grub-pc-bin grub-common xorriso mtools gcc make
    fi
else
    echo -e "${G}✓ Tous les outils sont présents.${N}"
fi

# -----------------------------------------------------------------------------
# 2. Compilation du noyau (via le Makefile)
# -----------------------------------------------------------------------------
echo -e "\n${Y}▸ Compilation (make clean && make all)...${N}"
if [ ! -f "Makefile" ]; then
    echo -e "${R}❌ Makefile introuvable — es-tu bien dans le dossier extrait ?${N}"
    exit 1
fi
make clean >/dev/null 2>&1 || true
if ! make all; then
    echo -e "${R}❌ Échec de la compilation. Vérifie les erreurs ci-dessus.${N}"
    exit 1
fi
echo -e "${G}✓ Compilation terminée (simulateur, tests, noyau i386).${N}"

# -----------------------------------------------------------------------------
# 3. TESTS DIRECTS / DIRECT TESTS
# -----------------------------------------------------------------------------
rule
echo -e "${C} 🧪 TESTS DIRECTS${N}"
rule

# T1 — Tests unitaires du noyau (kernel + shell + mm + vfs + sched + syscalls)
echo -e "${Y}▸ T1 : tests unitaires (freax-tests)...${N}"
if [ -x "build/freax-tests" ]; then
    TOUT=$(./build/freax-tests 2>&1)
    echo "$TOUT" | grep -E "^== suite|^   ->" | sed 's/^/    /'
    if echo "$TOUT" | grep -Eq "[0-9]+ OK, 0 KO"; then
        ok "tests unitaires — $(echo "$TOUT" | grep -oE "[0-9]+ OK, 0 KO" | head -1)"
    else
        ko "tests unitaires — échecs détectés :"
        echo "$TOUT" | grep "FAIL" | head -10 | sed 's/^/      /'
    fi
else
    ko "build/freax-tests introuvable"
fi

# T2 — Le simulateur démarre et répond
echo -e "${Y}▸ T2 : simulateur hébergé (freax-sim --version)...${N}"
VOUT=$(./build/freax-sim --version 2>&1)
if echo "$VOUT" | grep -q "FreaxOS 2026 v"; then
    ok "simulateur — $(echo "$VOUT" | head -1)"
else
    ko "simulateur — pas de bannière de version"
fi

# T3 — Le /proc dynamique liste ses nœuds
echo -e "${Y}▸ T3 : /proc dynamique (ls /proc)...${N}"
PSTAT=$(./build/freax-sim -c "ls /proc" 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$PSTAT" | grep -q "meminfo" && echo "$PSTAT" | grep -q "tasks"; then
    ok "/proc — nœuds meminfo, tasks, cpuinfo... présents"
else
    ko "/proc — nœuds manquants"
fi

# T4 — Roundtrip ramfs : write puis cat (script de 2 commandes, 1 session)
echo -e "${Y}▸ T4 : ramfs roundtrip (write + cat)...${N}"
printf 'write /tmp/freax.txt bonjour\ncat /tmp/freax.txt\nhalt\n' > build/rt.txt
ROUT=$(./build/freax-sim --script build/rt.txt 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
if echo "$ROUT" | grep -cx "bonjour" | grep -q "^1$"; then
    ok "ramfs — write /tmp/freax.txt puis cat → « bonjour »"
else
    ko "ramfs — le roundtrip write/cat a échoué"
fi

# T5 — Le noyau bare-metal : ELF32 i386 + en-tête multiboot valide
echo -e "${Y}▸ T5 : ELF bare-metal multiboot (freax-i386.elf)...${N}"
if [ -f "build/freax-i386.elf" ]; then
    if readelf -h build/freax-i386.elf 2>/dev/null | grep -q "Intel 80386" && \
       grep -abo $'\x02\xb0\xad\x1b' build/freax-i386.elf >/dev/null 2>&1; then
        ok "ELF32 i386 — magic multiboot 0x1BADB002 présent ($(grep -abo $'\x02\xb0\xad\x1b' build/freax-i386.elf | head -1 | cut -d: -f1))"
    else
        ko "ELF32 — format i386 ou en-tête multiboot invalide"
    fi
else
    skip "gcc -m32 indisponible sur cet hôte → noyau bare-metal non compilé"
fi

# T7 — captures du bureau Liquid Glass (sans affichage) / desktop screenshots
echo -e "${Y}▸ T7 : captures du bureau Liquid Glass...${N}"
if [ -x "build/freax-desktop" ]; then
    mkdir -p docs/img
    if ./build/freax-desktop --screenshot docs/img >/dev/null 2>&1 \
       && ls docs/img/desktop-*.ppm >/dev/null 2>&1; then
        NSHOT=$(ls docs/img/desktop-*.ppm 2>/dev/null | wc -l)
        ok "bureau Liquid Glass — $NSHOT captures générées dans docs/img/"
        echo "      (visualisez les .ppm ou les .png inclus dans le zip)"
    else
        ko "bureau — echec du mode --screenshot"
    fi
else
    skip "bureau — build/freax-desktop absent"
fi

# -----------------------------------------------------------------------------
# 4. Smoke test QEMU sans écran (le noyau parle sur COM1)
# -----------------------------------------------------------------------------
rule
if command -v qemu-system-i386 &>/dev/null && [ -f "build/freax-i386.elf" ]; then
    echo -e "${C} 💨 SMOKE TEST QEMU (headless)${N}"
    echo -e "${Y}▸ T6 : boot 10 s, sortie COM1 capturée dans build/smoke.log...${N}"
    timeout 10 qemu-system-i386 \
        -kernel build/freax-i386.elf \
        -m 32M -display none \
        -serial file:build/smoke.log \
        -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 || true
    if grep -q "FreaxOS 2026" build/smoke.log 2>/dev/null; then
        ok "QEMU headless — bannière reçue sur COM1 :"
        grep -m3 "FreaxOS\|Inspire\|Inspired" build/smoke.log | sed 's/^/      /'
    else
        ko "QEMU headless — pas de bannière sur COM1 (voir build/smoke.log)"
    fi
else
    skip "T6 smoke QEMU — qemu-system-i386 absent ou ELF absent"
fi

# -----------------------------------------------------------------------------
# 5. Lancement interactif sur QEMU
# -----------------------------------------------------------------------------
rule
if [ "$FAIL" -gt 0 ]; then
    echo -e "${R}❌ $FAIL test(s) en échec — lancement interactif annulé.${N}"
elif [ "$DESKTOP_MODE" -eq 1 ]; then
    echo -e "\n${C}===============================================${N}"
    echo -e "${G}🖥️  LANCEMENT DU BUREAU FREAXOS (X11)...${N}"
    echo -e "${C}===============================================${N}"
    ./build/freax-desktop
elif [ "$NO_GUI" -eq 1 ]; then
    echo -e "${Y}▸ --no-gui : lancement interactif sauté.${N}"
elif command -v qemu-system-i386 &>/dev/null; then
    # ISO GRUB si grub-mkrescue existe, sinon démarrage direct par -kernel
    if command -v grub-mkrescue &>/dev/null; then
        echo -e "${Y}▸ Création de l'ISO bootable (grub-mkrescue)...${N}"
        mkdir -p iso/boot/grub
        cp build/freax-i386.elf iso/boot/
        cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=0
set default=0
menuentry "FreaxOS 2026" {
    multiboot /boot/freax-i386.elf
    boot
}
EOF
        grub-mkrescue -o freax.iso iso/ >/dev/null 2>&1 \
            && echo -e "${G}✓ ISO créée : freax.iso${N}" \
            || echo -e "${Y}ISO impossible → démarrage direct par -kernel${N}"
    fi

    # Choix de l'affichage : gtk > sdl > none+serial / pick display: gtk > sdl > none
    DISP=""
    if qemu-system-i386 -display help 2>/dev/null | grep -q "gtk"; then DISP="gtk"
    elif qemu-system-i386 -display help 2>/dev/null | grep -q "sdl"; then DISP="sdl"
    fi

    echo -e "\n${C}===============================================${N}"
    echo -e "${G}🚀 LANCEMENT DE FREAXOS SUR QEMU...${N}"
    echo -e "${C}===============================================${N}"
    if [ -n "$DISP" ]; then
        [ -f "freax.iso" ] && BOOT=(-cdrom freax.iso) || BOOT=(-kernel build/freax-i386.elf)
        qemu-system-i386 "${BOOT[@]}" -m 32M -serial stdio -display "$DISP"
    else
        echo -e "${Y}Pas d'affichage graphique → mode terminal (COM1 sur stdio, Ctrl-C pour quitter)${N}"
        [ -f "freax.iso" ] && BOOT=(-cdrom freax.iso) || BOOT=(-kernel build/freax-i386.elf)
        qemu-system-i386 "${BOOT[@]}" -m 32M -display none -serial stdio
    fi
else
    skip "lancement interactif — qemu-system-i386 absent (installe-le puis relance)"
fi

# -----------------------------------------------------------------------------
# Bilan / summary
# -----------------------------------------------------------------------------
rule
echo -e "${C} 📋 BILAN / SUMMARY : ${G}${PASS} PASS${N} | ${R}${FAIL} FAIL${N} | ${Y}${SKIP} SKIP${N}"
rule
[ "$FAIL" -eq 0 ] || exit 1
