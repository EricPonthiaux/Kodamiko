# Revue : vues Diff (`src/changes`) et Historique (`src/history`)

Fichiers lus : `DiffView`, `DiffHeaderBar`, `HunkActions`, `ChangesView`,
`FilePatch`, `HistoryView`, `CommitDelegate`, `CommitLog`. Comparaison avec
VS Code, JetBrains, Sublime Merge, Fork, GitKraken, la revue de PR GitHub et
Magit.

## 1. Ce qui est déjà au niveau des meilleurs

| Point | Verdict |
|---|---|
| Coloration syntaxique dans le diff. Les lignes supprimées sont colorées dans le contexte de l'ancien code (`collectSides`). | Mieux que VS Code et Fork. Comparable à GitHub et Sublime Merge. |
| Mots modifiés à l'intérieur d'une ligne (LCS sur des tokens). Désactivé quand la ligne est trop réécrite. | Même logique que GitHub et Sublime Merge. |
| Staging et unstaging par ligne via la sélection, avec recalcul correct des en-têtes `@@` (`FilePatch::partial`, cas fichier créé ou supprimé gérés). | Au niveau de Sublime Merge et Magit. Peu d'outils le font proprement. |
| Boutons sur le bloc survolé (Stage / Discard). | Comme VS Code et GitHub Desktop. |
| Discard confirmé et sauvegardé par un snapshot. | Plus sûr que presque tous les clients ; seul JetBrains (Local History) fait autant. |
| En-tête qui suit le fichier en haut de l'écran, compteurs +/−, fichier précédent/suivant. | Bon. |
| Lectures git hors du thread UI, avec un compteur de génération et une relecture si une demande arrive pendant une lecture. | Solide. |
| Disposition de l'historique qui s'adapte à la largeur, badges de refs, avatars, dates relatives. | Soigné. |

## 2. Bugs et risques

1. ✅ **Corrigé.** ~~**Les boutons de bloc restent mal placés après un
   défilement au clavier.**~~ `HunkActions` ne se cachait que sur `Wheel` et
   `Leave`. Après PageDown ou un saut avec ↑/↓, les boutons pointaient vers
   l'ancien bloc, et un clic stageait un autre bloc que celui affiché à côté.
   Les boutons se cachent maintenant à chaque défilement vertical
   (`SCN_UPDATEUI` avec `SC_UPDATE_V_SCROLL`), quelle qu'en soit la cause.
   Test : `blockButtonsHideWhenTheDiffScrolls`, qui échoue sans la
   correction.
2. **Performance du rafraîchissement.** `RepositoryStatus::read` demande
   `GIT_DIFF_SHOW_UNTRACKED_CONTENT` à chaque rafraîchissement, c'est-à-dire
   après chaque commande git et chaque édition de Claude. Ensuite
   `DiffView::render()` reconstruit toute la section d'un coup : tous les
   fichiers concaténés, un `QsciScintilla` temporaire par fichier pour la
   coloration. Avec quelques gros fichiers non suivis (logs, dumps), le thread
   UI se bloque.
3. ✅ **Corrigé.** ~~**Coloration syntaxique lente.**~~ `scratchStyles`
   (`DiffView.cpp`) envoyait un `SCI_GETSTYLEAT` par octet, soit jusqu'à
   512 Ko de messages. Il fait maintenant un seul `SCI_GETSTYLEDTEXT` par côté
   (`SCI_GETSTYLEDTEXTFULL` n'existe pas dans QScintilla 2.14.1). Test de
   non-régression : `diffColorsTheCodeAsTheEditor`.
4. ✅ **Corrigé.** ~~**L'historique d'un fichier s'arrête au
   renommage.**~~ `CommitLog::read` suit le fichier comme `git log
   --follow` : au commit qui crée le chemin, une détection des renommages
   (seulement à ce commit-là) donne l'ancien nom, cherché ensuite dans les
   commits plus anciens. `CommitSummary::path` donne le nom du fichier à
   chaque commit, et l'historique montre le diff sous ce nom. Le suivi
   s'arrête à un renommage venu de l'extérieur du dossier ouvert. L'outil
   MCP `get_file_history` donne aussi ce `path`.
5. ½ **En partie corrigé.** ~~**Le filtre ne voit que les 2000 commits
   chargés**~~ : quand le filtre cache presque tout, la fin de la liste est
   visible et l'historique continue de se charger, jusqu'à remplir la vue
   ou atteindre le premier commit. Reste : le filtre ne cherche que dans le
   résumé, pas dans le corps du message.
6. `FilePatch::parse` est appelé deux fois par fichier, dans `rebuildList`
   puis dans `showSection`. Le coût est faible, mais il s'ajoute au point 2.

## 3. Ce qui manque par rapport au marché

### Diff (par ordre de priorité)

1. **Affichage côte à côte.** Tous les grands outils proposent unifié et côte
   à côte. C'est le plus gros manque.
2. ✅ **Fait.** ~~**Afficher plus de contexte**~~ : les flèches du
   séparateur `⋯` révèlent les lignes inchangées par 20 (↑ au-dessus du
   bloc, ↓ sous le bloc précédent, ↕ tout d'un coup quand il en reste 20 au
   plus), et un dernier séparateur « ⋯ ↓ 17 more lines » mène à la fin du
   fichier. La nouvelle version est lue avec le diff (`FileDiff::newText` :
   index, disque ou commit), et les lignes révélées restent hors du patch,
   donc le staging ne change pas. Diff et historique.
3. ✅ **Fait.** ~~**Ignorer les espaces**~~ : le bouton ␣ de l'en-tête du
   diff relit le diff avec `-w` (`Whitespace::Ignore`), dans la vue Diff et
   dans l'historique, où le fichier choisi reste choisi. Le patch ne
   correspond alors plus aux fichiers : Stage, Unstage et Discard par bloc
   ou par ligne sont désactivés, et Espace ou Suppr expliquent pourquoi dans
   la barre d'état. Le staging de fichiers entiers reste possible. Le
   nombre de lignes de contexte n'est plus utile depuis le dépliage.
4. ✅ **Fait.** ~~**Ouvrir le fichier à la ligne du diff.**~~ F4 ou le menu
   « Open in Editor » ouvrent le fichier à la ligne sous le curseur
   (`DiffView::newFileLine` ; une ligne supprimée mène à la ligne suivante
   restée). Reste à faire : l'édition directe dans le diff (JetBrains), et
   la même chose dans l'historique.
5. ✅ **Fait.** ~~**Raccourcis clavier.**~~ F7 / Maj+F7 bloc suivant ou
   précédent et Alt+↓ / Alt+↑ fichier suivant ou précédent (diff et
   historique) ; Espace stage ou déstage, Suppr discard, F4 ouvre (vue
   Diff). La navigation part du curseur s'il est visible, sinon du haut de
   la vue, et le curseur reste près du bloc après un stage : Espace
   enchaîne les blocs comme dans lazygit.
6. Gros diffs : replier un fichier, marquer un fichier comme vu, charger à la
   demande au-delà d'une taille donnée (GitHub).
7. Éditeur de fusion à 3 panneaux pour les conflits. Aujourd'hui la barre
   d'opération dit seulement d'éditer les fichiers ; VS Code, JetBrains et
   Sublime Merge ont un vrai éditeur de fusion.
8. Détection du code déplacé (`--color-moved`) et diff d'images. Utile, mais
   secondaire.

### Historique

1. **Graphe des branches** et choix de la branche ou de toutes les refs.
   Seul HEAD est parcouru. C'est l'élément central chez Fork, GitKraken,
   Sublime Merge et JetBrains.
2. ✅ **Fait.** ~~**Pagination ou défilement infini**~~ : 500 commits
   d'abord, puis deux fois plus chaque fois que la fin de la liste devient
   visible. Chaque lecture reprend au début, mais le doublement garde un
   coût total linéaire. La position de défilement et le commit choisi
   restent en place.
3. **Recherche plus riche** : corps du message, auteur, plage de dates,
   contenu (`-S`/`-G`), comme JetBrains et Sublime Merge.
4. **Actions sur un commit** : revert, cherry-pick, reset, créer une branche
   ou un tag, comparer avec la copie locale, parcourir l'arbre à cette
   révision. Aujourd'hui il n'y a que copier et checkout.
5. **Sélection de plusieurs commits** pour un diff cumulé, et comparaison de
   deux commits.
6. **Commits de fusion** : choix du parent ou diff combiné. Aujourd'hui le
   diff se fait toujours contre le premier parent.
7. **Annotate/blame lié à l'historique**, et historique d'une sélection
   (`git log -L`), comme JetBrains et GitLens.
8. Liens cliquables dans le message (URL, `#123`), indication de signature
   GPG, trailers `Co-authored-by`.

## 4. Écarts avec CLAUDE.md

- ✅ ~~**Taille des fichiers**~~ : `DiffView` est découpé (`WordDiff`,
  `DiffLayout`, `DiffStyles`, `DiffSyntaxHighlighter`). Le volet diff de
  `ChangesView` est dans `ChangesViewDiff.cpp`, et la confirmation du
  discard dans `DiscardConfirmation`.
- **`delete menu;`** dans `ChangesView::showDiffMenu` (§5 : pas de
  `delete`). Un `std::unique_ptr<QMenu>` convient.
- **Paramètre de sortie** `bool *lines` dans `diffSelection` (§5 : la sortie
  doit être une valeur de retour). Un struct `{selection, isLines}` le
  remplace.
- **Booléens qui changent le comportement** :
  `applyDiffSelection(line, bool discard)` (§2) ; il faudrait un
  `enum class`. `DiffView::goToChange` prend maintenant un `Direction`.
- **Taille de `HistoryView.cpp`** : 780 lignes, à la limite des 800 de
  §8. Le volet du commit (`createCommitPane`, `showDetails`,
  `messageHtml`) pourrait aller dans son propre fichier, comme
  `ChangesViewDiff.cpp`.
- **Fonctions trop longues** : le constructeur de `ChangesView`
  (~170 lignes), `HistoryView::createCommitPane` et `showDetails` dépassent
  largement l'objectif de 40 lignes.
- **Couleurs en dur copiées trois fois** : `#3fb950` / `#f85149` dans
  `DiffView::colorsFor`, `DiffHeaderBar::follow` et
  `HistoryView::showDetails`. Elles devraient venir du `Theme`.
- **Accès fragile** : `static_cast<QLabel *>(commitPages_->widget(0))`, deux
  fois dans `HistoryView`. Garder un membre `commitHint_` est plus sûr.
- **Statut sous forme de texte** : `DiffFile::status` contient
  `"renamed from x.cpp"`, un texte d'interface qui sert de donnée.
- **Pluriels** : `tr("%1 days ago")` et `tr("%1 commits")` devraient
  utiliser `%n`.
- **Casts** : `static_cast<int>(size())` est utilisé là où §4 demande
  `int(...)`.

## 5. Recommandation

Dans l'ordre, pour le meilleur rapport valeur/effort :

1. ✅ ~~Corriger les bugs 1 et 3, qui sont petits et sans risque.~~ Fait.
2. ✅ ~~Ajouter les raccourcis clavier et l'ouverture du fichier à la ligne
   du diff.~~ Fait.
3. ✅ ~~Ajouter « afficher plus de contexte » et « ignorer les espaces ».~~
   Fait.
4. ✅ ~~Ajouter le suivi des renommages dans l'historique d'un fichier,
   puis la pagination.~~ Fait.
5. Les deux gros chantiers : le diff côte à côte, puis le graphe des
   branches.
