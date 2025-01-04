# Grammar

```
source =
    {declaration | comment}, EOF;
comment =
    "#", COMMENT, (EOL | EOF);

declaration =
    (function | struct | pipeline | import);
function =
    "function", IDENTIFIER, "(", [functionInputs], ")", "->", type, executableBlock;
struct =
    "struct", IDENTIFIER, "{", {structMember, ","}, [structMember], "}";
structMemeber =
    type, IDENTIFIER;

pipeline =
    "pipeline", IDENTIFIER, "{", {pipelineSpec}, "}";
pipelineSpec =
    IDENTIFIER, "=", IDENTIFIER;

type =
    IDENTIFIER;

executableBlock =
    "{", {statement} "}";

statement =
    varStmt | returnStmt | exprStmt;

varStmt =
    "var", IDENTIFIER, ["=", exprStmt], ";";
returnStmt =
    "return", [expression], ";";
exprStmt =
    expression, ";";

expression = assignment;

assignOp = "=" | "+=" | "-=" | "*=" | "/=" | "%=";

assignment =
    logicOr, {assignOp, logicOr};
logicOr = logicAnd, {"or", logicAnd};
logicAnd = equality, {"and", equality};
equality = comparison, {("==" | "!="), comparison};
comparison = term, {(">" | ">=" | "<" | "<="), term};
term = factor, {("-" | "+"), factor};
factor = unary, {("/" | "*"), unary;
unary = {"!" | "-"}, primary;

arguments =
    expression, {",", expression}, [","];

call = "(", [arguments], ")";
listAccess = "[", expression, "]";
fieldAccess = "."", IDENTIFIER;

accessOrCallOrListAccessOrFieldAccess =
    namespacedIdentifier,
    {
        call |
        listAccess |
        fieldAccess
    };

primary = ("(", expression, ")") | literal | accessOrCallOrListAccessOrFieldAccess;

literal =
    "true" | "false" | NUMBER;

NUMBER =
    DIGIT_NON_ZERO, {DIGIT}, [".", DIGIT, {DIGIT}];
IDENTIFIER =
    ALPHA, {ALPHA | DIGIT};
ALPHA =
      "A" | "B" | "C" | "D" | "E" | "F" | "G"
    | "H" | "I" | "J" | "K" | "L" | "M" | "N"
    | "O" | "P" | "Q" | "R" | "S" | "T" | "U"
    | "V" | "W" | "X" | "Y" | "Z"
    | "a" | "b" | "c" | "d" | "e" | "f" | "g"
    | "h" | "i" | "j" | "k" | "l" | "m" | "n"
    | "o" | "p" | "q" | "r" | "s" | "t" | "u"
    | "v" | "w" | "x" | "y" | "z";
ANY_CHAR = ? any characters ?;

DIGIT_NON_ZERO =
    "1" | "2" | "3" | "4" | "5" |
    "6" | "7" | "8" | "9";
DIGIT = "0" | DIGIT_NON_ZERO;
