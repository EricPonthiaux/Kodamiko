# Enlumine

Éditeur de code source open source pour **C, C++, Java et JavaScript/TypeScript**,
avec un **terminal Claude Code intégré** et une **gestion git** : changements en
cours, indexation fichier par fichier, bloc par bloc ou ligne par ligne, commits. Il tourne sous Linux, Windows et macOS (Qt 6).

## Fonctionnalités

- **Coloration syntaxique précise** par [tree-sitter](https://tree-sitter.github.io/)
  pour C, C++, Java, JavaScript (JSX), TypeScript et TSX. L'analyse est
  incrémentale : seul ce qui change est ré-analysé à chaque frappe.
- Coloration des fichiers annexes par les lexers de QScintilla : JSON, CMake,
  Markdown, XML, HTML, CSS, shell, Python, YAML, Makefile, INI/TOML, SQL, diff.
- **Terminal intégré** (émulation xterm 256 couleurs / truecolor par libvterm) :
  sessions Claude Code et shells classiques, dans des onglets.
- **Mode Diff** : les changements en cours, séparés comme dans `git status`
  entre indexés et non indexés, avec leur diff, mis à jour à chaque réponse de
  Claude et à chaque commande git. On y indexe ou retire des fichiers, des
  blocs ou des lignes, on abandonne des modifications et on committe.
- **Filet de sécurité** : avant d'abandonner des modifications, l'éditeur garde
  une copie des fichiers (fichiers non suivis compris), récupérable par
  *Git ▸ Récupérer un instantané*.
- **Volet Structure** : les fonctions, classes, méthodes, champs… du fichier
  affiché (titres pour Markdown, fonctions et cibles pour CMake), imbriqués et
  filtrables. Un clic amène le curseur sur le symbole ; le symbole qui
  contient le curseur est sélectionné.
- Rechargement automatique des fichiers ouverts quand ils changent sur disque.
  Si vous aviez des modifications non enregistrées, l'éditeur demande avant
  de recharger, et Ctrl+Z ramène votre version.
- Encodage et fins de ligne préservés à l'octet près (UTF-8 avec ou sans BOM,
  Latin-1, LF/CRLF).
- Visionneuse d'images (PNG, JPEG, GIF, BMP, SVG… selon les greffons Qt) :
  ajustée à l'onglet, zoom par Ctrl+molette, Ctrl++ / Ctrl+- et Ctrl+0.
- Thèmes sombre et clair.

## Compilation

Prérequis :

- **Qt 6.5 ou plus récent** (modules Widgets, Network, PrintSupport, Test) ;
- CMake 3.24 ou plus récent, Ninja, un compilateur C++20 (GCC 11+, Clang 14+, MSVC 2022) ;
- un accès réseau lors de la première configuration.

Toutes les autres dépendances (QScintilla, tree-sitter et ses grammaires,
libvterm, libgit2) sont téléchargées et compilées automatiquement en statique.

```sh
cmake --preset default        # configuration (Release, dans build/)
cmake --build --preset default
ctest --preset default        # tests
./build/bin/enlumine [dossier-du-projet]
```

Sous Windows, lancez ces commandes depuis une « Developer PowerShell for VS 2022 »
après avoir ajouté le dossier `bin` de Qt au `PATH`.

Pour créer un dossier d'installation (utilisé par les paquets) :

```sh
cmake --install build --component enlumine --prefix <dossier>
```

La CI (`.github/workflows/ci.yml`) compile et teste sur les trois systèmes,
puis produit une AppImage, un zip Windows et un dmg macOS.

## Utilisation

La fenêtre a trois modes plein écran, choisis par les onglets en bas à droite :

- **Terminal** : les sessions Claude et les terminaux.
- **Edit** : panneau latéral (onglets **Fichiers** et **Structure**) et
  onglets d'éditeur.
- **Diff** : les changements en cours du dépôt git, en deux sections :
  **Indexés** (ce que le prochain commit contiendra) et **Non indexés**
  (fichiers non suivis compris). Cliquer un fichier fait défiler jusqu'à son
  diff, double-cliquer l'ouvre dans l'éditeur. La vue se met à jour à chaque
  modification de Claude et à chaque commande git lancée dans un terminal.

### Commits ciblés

- **Fichiers** : boutons *Indexer*, *Retirer*, *Abandonner…* ou clic droit sur
  les fichiers choisis (un titre de section vaut tous ses fichiers).
- **Blocs et lignes** : clic droit dans le diff. Sans sélection, l'action porte
  sur le bloc sous la souris ; avec une sélection, sur les lignes `+` et `-`
  sélectionnées seulement.
- **Commit** : message sous la liste, puis *Committer* ou Ctrl+Entrée. *Modifier
  le dernier commit* remplace le dernier commit (`--amend`) et reprend son
  message. Si rien n'est indexé, l'éditeur propose de tout indexer.
- **Abandonner** des modifications remet les fichiers dans l'état de l'index
  (les fichiers non suivis sont supprimés). Une copie est prise avant :
  *Git ▸ Récupérer un instantané* la remet sur disque.

Les opérations passent par la commande `git`, qui doit être installée : les
hooks du dépôt (pre-commit…), la signature des commits et votre configuration
s'appliquent comme dans un terminal. Un échec affiche le message de git,
sortie des hooks comprise. La branche courante est indiquée dans la barre
d'état.

| Action | Raccourci |
|---|---|
| Mode Terminal / Edit / Diff | Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Basculer vers le terminal et revenir | F11 |
| Structure du fichier (filtre) | Ctrl+Shift+O |
| Rafraîchir le diff | F5 |
| Préparer un commit | Ctrl+Shift+G |
| Committer (dans le message) | Ctrl+Entrée |
| Ouvrir un dossier | Ctrl+K, Ctrl+O |
| Nouvelle session Claude | Ctrl+Shift+L |
| Nouveau terminal | Ctrl+Shift+T |
| Copier / coller dans le terminal | Ctrl+Shift+C / Ctrl+Shift+V (Cmd+C / Cmd+V sous macOS) |
| Enregistrer / tout enregistrer | Ctrl+S / Ctrl+Alt+S |

La session Claude utilise la commande `claude`
([Claude Code](https://claude.com/claude-code)), qui doit être installée.

Quand le terminal a le focus, il reçoit toutes les touches, y compris Ctrl+C,
Échap et Maj+Tab. Les raccourcis de l'éditeur reprennent effet dès que vous
cliquez dans un fichier. Seuls Ctrl+1/2/3 et F11 restent actifs depuis le
terminal, pour changer de mode.

## Comment l'éditeur suit Claude

1. L'éditeur lance `claude --settings <fichier>` avec deux *hooks* :
   `PostToolUse` (outils d'édition) et `Stop`. La configuration Claude de
   l'utilisateur n'est pas modifiée.
2. Chaque hook exécute `enlumine-hook`, un petit programme livré avec
   l'éditeur. Il transmet l'événement à l'éditeur par un socket local, puis
   attend son accusé de réception.
3. Sur `PostToolUse`, l'éditeur recharge les fichiers ouverts modifiés. Sur
   `PostToolUse` et `Stop`, il recalcule le mode Diff à partir du dépôt git
   du projet : les modifications faites par des commandes shell (`sed`,
   générateurs…) apparaissent donc aussi. Le suivi de Claude ne modifie
   jamais le dépôt : seules vos actions dans le mode Diff le font.

## Architecture

```
src/
  app/        fenêtre principale, assemblage des modules
  editor/     éditeur QScintilla, onglets, lecture/écriture des fichiers, surveillance disque
  syntax/     coloration : tree-sitter (requêtes highlights.scm), lexers QScintilla, thèmes
  outline/    volet Structure : requêtes outline.scm (syntax/queries), Markdown, CMake
  terminal/   PTY (forkpty / ConPTY), émulation VT (libvterm), widget terminal
  claude/     lancement de Claude Code, hooks, serveur local
  git/        état du dépôt (libgit2), opérations (commande git), patchs partiels, instantanés
  changes/    mode Diff : indexation, commit, récupération des instantanés
  workspace/  arborescence des fichiers
tools/enlumine-hook/   relais hooks Claude Code → éditeur
tests/                 tests Qt Test (ctest)
cmake/                 dépendances tierces
```

Les modules dépendent d'interfaces (`ISyntaxHighlighter`, `IOutlineProvider`, `IPty`). Ajouter un
langage ou un type de PTY se fait en ajoutant une implémentation, sans
modifier le reste.

**Ajouter un langage tree-sitter** : déclarer la grammaire dans
`cmake/Dependencies.cmake` et `cmake/TreeSitter.cmake`, ajouter sa requête de
coloration dans `TREE_SITTER_QUERY_FILES`, écrire sa requête de structure
`src/syntax/queries/<langage>/outline.scm` (conventions dans le `README.md`
du dossier), puis l'enregistrer dans `src/syntax/TreeSitterGrammars.cpp` et
`src/syntax/LanguageRegistry.cpp`. Pour un fichier sans grammaire, la
structure s'obtient en implémentant `IOutlineProvider` (voir
`src/outline/TextOutlines.cpp`) et en l'ajoutant à `OutlineProviders::create`.

## Licence

Enlumine est distribué sous **GNU GPL v3** (voir `LICENSE`), licence imposée
par QScintilla. Composants tiers :

| Composant | Licence |
|---|---|
| Qt 6 | LGPL v3 |
| QScintilla | GPL v3 |
| tree-sitter et grammaires | MIT |
| libvterm | MIT |
| libgit2 | GPL v2 avec exception de liaison |
