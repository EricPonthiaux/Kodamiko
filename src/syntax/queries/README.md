# Requêtes de structure (outline.scm)

Requêtes tree-sitter du volet Structure, écrites pour Enlumine (les requêtes
de coloration viennent, elles, des grammaires). Conventions :

- `@definition.<genre>` couvre l'élément entier (corps compris) : sa plage
  sert à imbriquer les éléments et à suivre le curseur ;
- `@declaration.<genre>` : simple déclaration (prototype, signature),
  ignorée à l'intérieur d'une fonction ;
- `@name` : le nom affiché, sur lequel le curseur est placé.

Genres : `namespace`, `class`, `struct`, `interface`, `enum`, `type`,
`function`, `method`, `field`, `macro`. Une fonction contenue dans une
classe, une structure ou une interface est affichée comme une méthode.
