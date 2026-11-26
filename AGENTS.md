# Travailler sur Logicraft

## Architecture actuelle

- `logicraft_core` contient le monde, la simulation et le codec de snapshots. Un appel à `updateLogic` avance un monde d’un tick. Le client peut fournir un rappel pour invalider ses maillages. Garder cette cible compilable sans SDL ni OpenGL.
- `src/snapshot.cpp` encode le même état sur un flux mémoire ou fichier. Le lecteur vérifie les dimensions attendues et construit un monde temporaire avant de remplacer le monde actif.
- Le client SDL dans `main.cpp` gère les entrées, les menus et la caméra. `render.cpp` possède les textures et maillages. Une modification de l’interface doit garder les zones cliquables et dessinées sur le même calcul de géométrie.

## Contrat des maps

Le format v11 utilise des entiers little-endian. Après la signature BULLDOG et son octet nul viennent cinq entiers 32 bits (version, largeur, hauteur, profondeur, seed), puis le tick 64 bits. Les cellules sont parcourues en y, z, x et occupent neuf octets chacune : type, puissance, état du bouton ou mémoire logique, valeur du bouton, largeur du bouton, largeur B1, ordre, fréquence et largeur du signal. La table finale associe aux panneaux leur index 32 bits et leur texte de longueur 16 bits.

Garder les identifiants de blocs stables. Toute évolution du format doit prévoir la migration des anciennes maps et un test d’aller-retour. Tester un signal nul sur plusieurs bits : sa valeur seule ne permet pas de reconstruire sa largeur. Les fichiers fournis dans maps servent de cas de lecture.

## Évolution proposée vers un serveur

1. Introduire des commandes métier pour poser un bloc, configurer un bus et actionner un bouton. Le client local passe par le même validateur que le futur serveur.
2. Faire avancer la simulation à fréquence fixe indépendamment des images. Le serveur possède le monde, valide les commandes et leur attribue un tick et un numéro de séquence. Le client garde la caméra, le HUD et les animations.
3. À la connexion, transmettre un snapshot au tick T. Publier ensuite les changements de cellules ordonnés après T. Sur rupture de séquence, redemander un snapshot. Définir les limites de taille, l’authentification et les droits d’édition avant d’ouvrir ce transport.
4. Garder les snapshots complets tant que le coût est acceptable. Mesurer le temps par tick et la taille des maps avant d’introduire stockage par chunks, compression ou journal de commandes.

## Vérification du noyau

```sh
cmake -S . -B build/core -DLOGICRAFT_BUILD_CLIENT=OFF
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

Pour le client, utiliser la configuration CMake avec SDL2 et GLEW décrite dans le README. Tester les changements de rendu sur un circuit actif, avec les menus ouverts et fermés.
