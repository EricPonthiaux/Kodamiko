# Audit des fenêtres Diff et Historique de Duet

Audit réalisé le 19 octobre 2026.

**Ta fenêtre de diff est déjà convaincante pour relire et préparer un commit. L’historique est plus en retrait : il permet de consulter des changements, mais manque encore des outils nécessaires pour enquêter efficacement dans un projet.**

J’ai inspecté le code et le rendu réel des composants Qt en **1400 × 800 et 900 × 700**, sur ton dépôt et un dépôt de test, avec les thèmes sombre et clair. La comparaison s’appuie sur les documentations actuelles de VS Code, IntelliJ IDEA et Fork.

Les points à conserver sont solides :

- Coloration syntaxique, différences au niveau des mots, numéros avant/après et signes `+`/`−` : la lecture ne repose pas uniquement sur les couleurs.
- Staging par fichier, bloc et sélection de lignes.
- Séparation claire entre changements indexés et non indexés.
- Sauvegarde avant abandon des modifications : un vrai atout.
- En-tête de fichier qui reste visible pendant le défilement.
- Historique avec auteurs, dates, références et détails du commit ; panneaux redimensionnables.

Voilà où tu te situes par rapport aux références :

| Usage | Référence du marché | Duet actuellement |
|---|---|---|
| Lire un changement | Choix unifié / côte à côte, adaptation à la largeur dans [VS Code](https://code.visualstudio.com/docs/sourcecontrol/staging-commits) | Unifié uniquement |
| Comprendre le contexte | Déplier le code inchangé, ignorer les espaces, rejoindre la ligne source dans [IntelliJ](https://www.jetbrains.com/help/idea/differences-viewer.html) | Contexte du patch uniquement ; options absentes |
| Préparer un commit précis | Staging de blocs et de lignes dans [VS Code](https://code.visualstudio.com/docs/sourcecontrol/staging-commits) | Fonctionnel, avec une ambiguïté d’interaction |
| Explorer l’historique | Graphe, branches, comparaisons de révisions dans [VS Code](https://code.visualstudio.com/docs/sourcecontrol/history) et [IntelliJ](https://www.jetbrains.com/help/idea/log-tab.html) | Liste des ancêtres de HEAD ; pas de comparaison arbitraire |
| Examiner les images | Diff visuel dans [Fork](https://git-fork.com/) | Mention « Binary file » |

**Les problèmes que je traiterais en priorité :**

1. **Une recherche sans résultat laisse affiché un ancien diff.**  
   Reproduit : la liste des commits devient vide, mais les détails et le diff du commit précédent restent visibles. Le compteur continue également d’indiquer le total chargé. L’utilisateur peut croire que le contenu affiché correspond à sa recherche. Il faut vider les panneaux dépendants et afficher « Aucun commit correspondant ». [Capture du problème](/tmp/duet-diff-audit/fixture-history-empty-search.png) · [Code concerné](src/history/HistoryView.cpp#L398).

2. **L’historique perd les versions antérieures à un renommage.**  
   Dans mon scénario, `git log --follow` retrouve trois commits ; Duet n’en retrouve que deux. Le parcours recherche le chemin actuel sans suivre son ancien nom. C’est une limite importante pour comprendre l’évolution d’un fichier après une réorganisation. [Code concerné](src/git/CommitLog.cpp#L103).

3. **Depuis l’historique, ouvrir un fichier ouvre sa version actuelle.**  
   Le double-clic utilise le fichier sur disque, sans transmettre la révision consultée. Un fichier supprimé depuis ne s’ouvre pas. Je proposerais deux actions explicites : **« Ouvrir cette révision »**, en lecture seule, et **« Ouvrir le fichier actuel »**. [Code concerné](src/history/HistoryView.cpp#L228).

4. **Le bouton de staging d’un bloc ignore la sélection de lignes.**  
   Le menu contextuel sait agir sur les lignes sélectionnées, mais le bouton flottant efface la sélection puis traite le bloc entier. Même comportement pour l’abandon du bloc. C’est intentionnel dans le code, mais surprenant : dans VS Code, les boutons s’adaptent à la sélection. Il faut soit suivre cette convention, soit afficher clairement « Indexer tout le bloc ». [Code concerné](src/changes/ChangesView.cpp#L196) · [Comportement de VS Code](https://code.visualstudio.com/docs/sourcecontrol/staging-commits#stage-and-revert-from-the-diff-editor).

5. **Les commandes clavier essentielles ne suivent pas la vue active.**  
   `Ctrl+F` ne fonctionne que dans le mode Édition. Les raccourcis suivant/précédent changement concernent également l’éditeur, tandis que les flèches du diff n’ont pas de raccourci propre. Rechercher dans un grand patch et parcourir ses changements devrait être immédiat. [Code concerné](src/app/MainWindowEditing.cpp#L105).

6. **La recherche d’historique est plus limitée que son libellé ne le suggère.**  
   « Message » recherche seulement le titre du commit. J’ai vérifié qu’un identifiant présent dans le corps du message ne ressort pas. La recherche porte aussi sur **2 000 commits chargés au maximum**, sans pagination. Il faut rechercher dans les messages complets, permettre de charger la suite et rendre le périmètre visible. [Filtre](src/history/HistoryView.cpp#L398) · [Limite de chargement](src/history/HistoryView.cpp#L325).

7. **En fenêtre étroite, la disposition privilégie trop les métadonnées.**  
   À 900 px, l’historique réserve environ 55 % de la largeur à la liste et aux détails. Le diff reçoit environ 400 px, dont une centaine pour la marge : beaucoup de lignes deviennent illisibles sans défilement horizontal. Le message du commit réduit aussi fortement la liste des fichiers. Je donnerais la priorité au diff, avec détails repliables et possibilité de masquer temporairement les autres panneaux. [Capture sur ton dépôt](/tmp/duet-diff-audit/project-history-narrow.png).

Visuellement, **la base est cohérente et sobre**. Les corrections utiles concernent surtout la place et la hiérarchie : le nom, le statut et les statistiques du fichier sont répétés dans l’en-tête fixe puis dans le diff ; les colonnes de statistiques réduisent la place disponible pour les noms de fichiers ; plusieurs informations secondaires sont trop atténuées en thème sombre. Cela se voit dans la [vue Diff actuelle](/tmp/duet-diff-audit/project-diff-wide.png).

Pour rejoindre les meilleures références, je procéderais dans cet ordre :

1. **Fiabilité de lecture** : recherche vide, suivi des renommages, ouverture des révisions et portée des actions sur une sélection.
2. **Confort quotidien** : recherche dans le diff, raccourcis, contexte dépliable, espaces ignorables, largeur mieux répartie et mode côte à côte.
3. **Exploration avancée** : comparaison entre deux commits ou avec le fichier actuel, choix des branches, puis graphe et filtres structurés.

Les trois suites `tst_changes`, `tst_git` et `tst_history` passent après reconstruction. Aucun fichier source n’a été modifié lors de l’audit. Les performances sur de très gros dépôts et l’usage avec lecteur d’écran restent à mesurer.

Les captures référencées sont des fichiers locaux temporaires conservés dans `/tmp/duet-diff-audit/`. Les numéros de ligne correspondent au code examiné à la date de l’audit.
