# Implémentation du syscall ouichefs

1. Appliquez le patch fournit sur votre répértoire Linux (ce répértoire doit impérativement avoir été clean avan (make clean)). Le patch aura également était make une première fois avec le .config (je fournirai un patch pour ceux qui ont pas le .config plus tard).  Ensuite refaire un make.

> Le patch se fait depuis la racine de votre rep linux avec la commande *patch -p1 NOMDUPATCH*.

2. Une fois le patch appliqué et le noyau recompilé, ajoutez au fichier fs.c (de ouichefs) ce qu'il y a dans le fichier syscallouichefs.c (syscall.c contient une autre méthode, donc pas important). 

3. VOus pouvez ensuite, après avoir chargé le module, tester le syscall avec le fichier testsyscall.c (dans /share/)

