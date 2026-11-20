cd ~/FreaxOS

python3 - <<'PY'
from pathlib import Path
p = Path("Makefile")
s = p.read_text()

old1 = "CORE_C    += kernel/tux_stub.c\nMOD_LIB   :=\nendif\n"
new1 = old1 + """
# FIX bare-metal : la lib Rust est compilee pour l'HOTE (x86_64) — elle ne
# peut pas etre liee dans un noyau freestanding 32 bits. Le lien bare-metal
# i386 utilise donc TOUJOURS le repli C kernel/tux_stub.c.
ifeq ($(USE_RUST),yes)
BM_STUB   := kernel/tux_stub.c
else
BM_STUB   :=
endif
"""
assert old1 in s, "bloc Rust introuvable (Makefile deja patche ?)"
s = s.replace(old1, new1, 1)

old2 = "$(CORE_C) $(BM_C)) \\"
new2 = "$(CORE_C) $(BM_STUB) $(BM_C)) \\"
assert old2 in s, "ligne BM_OBJS introuvable"
s = s.replace(old2, new2, 1)

p.write_text(s)
print("Makefile patche : le bare-metal utilise maintenant tux_stub.c (repli C)")
PY

# Relier le noyau (les objets déjà compilés sont conservés, ça prend 2 secondes)
make baremetal

# Vérifier puis lancer
ls -lh build/freax-i386.elf
qemu-system-i386 -kernel build/freax-i386.elf -m 32M -serial stdio -display gtk
