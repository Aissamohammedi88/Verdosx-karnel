# 1. Trouver le zip FreaxOS et l'extraire dans ~/FreaxOS
#    (le zip n'a pas de sous-dossier : il faut le dézipper dans un dossier dédié)
ZIP=$(find ~ -maxdepth 3 -iname "FreaxOS*.zip" 2>/dev/null | head -1)
echo "zip trouvé : $ZIP"
unzip -o "$ZIP" -d ~/FreaxOS
cd ~/FreaxOS

# 2. Ajouter le support 32 bits (le noyau i386 en a besoin ; souvent absent par défaut)
sudo apt install -y gcc-multilib libc6-dev-i386

# 3. Compiler
make clean && make all

# 4. Vérifier que le noyau existe bien
ls -lh build/freax-i386.elf

# 5. Lancer FreaxOS sur QEMU
qemu-system-i386 -kernel build/freax-i386.elf -m 32M -serial stdio -display gtk
