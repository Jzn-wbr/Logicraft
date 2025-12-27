# SigmaCraft

Petit projet C++/OpenGL/SDL2 géré via CMake et vcpkg. Les ressources (maps + images) restent à la racine pour simplifier l’exécution.

## Structure
- `src/` : code C++ (main + rendu + monde + types)
- `images/` : textures BMP
- `maps/` : sauvegardes `.bulldog`
- `config.cfg` : configuration utilisateur
- `CMakeLists.txt` + `vcpkg.json` : build et dépendances
- `build/` : fichiers générés par CMake (ajouté au `.gitignore`)

## Prérequis
- CMake (≥ 3.20)
- vcpkg cloné/bootstrappé (ex. `C:\vcpkg`)
- Visual Studio Build Tools 2022 (outil `cl.exe`) ou équivalent compatible

## Build
Depuis la racine :
```
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release
```

## Lancer
```
.\build\Release\sigmacraft.exe
```
Le binaire détecte automatiquement la racine contenant `images/` et `maps/` même si vous lancez depuis un autre dossier.

## Notes
- Par défaut, SDL2 et GLEW sont liés dynamiquement : `SDL2.dll` et `glew32.dll` sont copiés à côté de l’exe pour un lancement immédiat. Pour éviter la copie, définissez `-DVCPKG_APPLOCAL_DEPS=OFF` et ajoutez `C:\vcpkg\installed\x64-windows\bin` au `PATH`, ou utilisez le triplet statique `x64-windows-static`.
