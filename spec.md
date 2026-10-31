# A Level Pseudocode (9618) — Interpreter Language Specification

## 1. Scope and conformance

This spec defines the language the interpreter accepts: Cambridge International AS & A Level Computer Science (9618) pseudocode, as described in the Cambridge *Pseudocode Guide for Teachers* for exams in 2026 (\[Version 1, September 2023\](https://www.cambridgeinternational.org/Images/697401-2026-syllabus-legacy-notice.pdf)), plus the extra built-in functions printed in the insert of 9618 exam papers. Where the guide is silent or ambiguous, this spec makes a decision and records it in Section 13.

**Conformance levels.** The interpreter is built and tested in three levels, each a superset of the last:

| Level | Covers | Sections |
| --- | --- | --- |
| Core (AS) | Atomic types, 1D/2D arrays, expressions, selection, iteration, procedures, functions, string/numeric built-ins, text files | 2–9, 10.1 |
| Extended (A2) | Records, enumerated, pointer and set types, random files | 5.4–5.7, 10.2 |
| OOP (A2) | Classes, inheritance, constructors, visibility | 11 |

**Notation.** Grammar rules use EBNF: `{ x }` means zero or more, `[ x ]` means optional, `|` separates alternatives, and quoted text is literal. `NEWLINE` is a line terminator token. In prose, MUST and MUST NOT mark binding rules; SHOULD marks recommended behaviour.

**Out of scope.** Flowcharts, structure charts, assembly language, and the Python/VB/Java mappings used in exam mark schemes.

## 2. Lexical structure

Source text is UTF-8. The lexer converts it into a stream of tokens, each carrying its line and column. Indentation and spaces between tokens carry no meaning.

### 2.1 Keywords

Keywords MUST be written in upper case, as the guide requires. They are reserved and MUST NOT be used as identifiers.

```
AND APPEND ARRAY BOOLEAN BYREF BYVAL CALL CASE CHAR CLASS CLOSEFILE
CONSTANT DATE DECLARE DEFINE DIV ELSE ENDCASE ENDCLASS ENDFUNCTION
ENDIF ENDPROCEDURE ENDTYPE ENDWHILE FALSE FOR FUNCTION GETRECORD IF
IN INHERITS INPUT INTEGER MOD NEW NEXT NOT OF OPENFILE OR OTHERWISE OUTPUT
PRIVATE PROCEDURE PUBLIC PUTRECORD RANDOM READ READFILE REAL REPEAT
RETURN RETURNS SEEK SET STEP STRING SUPER THEN TO TRUE TYPE UNTIL
WHILE WRITE WRITEFILE
```

A word that matches a keyword in any other case (e.g. `endif`) is a lexical error: *Keywords must be upper case — did you mean ENDIF?*

### 2.2 Identifiers

An identifier starts with a letter and contains only letters `A–Z a–z`, digits `0–9` and `_`. Identifiers are **case-insensitive**: `Countdown` and `CountDown` name the same thing. Error messages use the spelling from the declaration or first use. Built-in function names (Section 9) are reserved and cannot be redefined.

### 2.3 Literals

| Type | Form | Examples | Rule |
| --- | --- | --- | --- |
| INTEGER | digits | `5`, `0`, `2026` | No sign; `-3` is unary minus applied to `3` |
| REAL | digits `.` digits | `4.7`, `0.3`, `4.0` | At least one digit each side; `.5` and `5.` are errors |
| CHAR | `'` one character `'` | `'x'`, `'@'` | Exactly one character; no escape sequences |
| STRING | `"` characters `"` | `"Hello"`, `""` | May be empty; may not span lines; no escape sequences |
| BOOLEAN | keyword | `TRUE`, `FALSE` | — |
| DATE | `dd/mm/yyyy` | `02/01/2005` | 1–2 digit day and month, 4-digit year, no spaces; validated as a real calendar date from `01/01/0001` to `31/12/9999` |

The lexer recognises a DATE when it sees the pattern `d{1,2}/d{1,2}/d{4}` with no whitespace. Division written without spaces in that exact shape is therefore read as a date (see Section 13).

### 2.4 Operators and punctuation

| Token | Meaning | Accepted aliases |
| --- | --- | --- |
| `←` | Assignment | `<-` |
| `+ - * /` | Arithmetic | `–` (U+2013) and `−` (U+2212) for minus |
| `&` | String concatenation | — |
| `= <> < > <= >=` | Comparison | `≠ ≤ ≥` |
| `^` | Pointer type, address-of (prefix), dereference (postfix) | — |
| `( ) [ ] , : .` | Grouping, indexing, separators, member access | — |

Quote aliases: `“ ”` are accepted as string delimiters and `‘ ’ ꞌ` as character delimiters, so code pasted from exam papers and PDFs lexes cleanly. There is no exponent operator.

**`<-` inside expressions.** `IF X<-1 THEN` lexes as `X`, `<-`, `1`. When the parser meets an assignment token where a binary operator is expected, it MUST reinterpret it as `<` followed by unary `-`.

### 2.5 Comments

`//` starts a comment that runs to the end of the line. There are no block comments.

### 2.6 Line structure

A statement ends at a newline. The lexer emits a `NEWLINE` token, but discards it when any of these hold:

1. It is inside unclosed `( )` or `[ ]`.
2. The previous token cannot end a statement: `,` `←` `:` a binary operator, `AND`, `OR`, `NOT`, `OF`, `TO`, `STEP`.
3. The next token cannot start a statement: `THEN`, `OF`, `RETURNS`, `TO`, a binary operator other than `-`, `AND`, `OR`, `)`, `]`.

Rule 3 supports the older exam layout with `THEN` on its own line. Blank lines and comment-only lines produce no tokens.

## 3. Program structure and grammar

A program is one source file containing a mix of top-level statements and definitions. Top-level statements run in order from the top. Definitions (`TYPE`, `PROCEDURE`, `FUNCTION`, `CLASS`) are **hoisted**: they are collected before execution, so a subroutine may be called above the line where it is defined.

Definitions MUST appear only at top level, with one exception: procedures and functions inside a `CLASS` are methods. Subroutines cannot be nested.

### 3.1 Grammar

```
program        = { NEWLINE } { top_item } EOF ;
top_item       = type_def | set_define | subroutine | class_def | stmt ;
block          = { stmt } ;      (* ends at a terminator keyword, see 3.2 *)

(* ---------- declarations ---------- *)
stmt           = declare | constant | assignment | input | output | call
               | if_stmt | case_stmt | for_stmt | repeat_stmt | while_stmt
               | return_stmt | file_stmt ;
declare        = "DECLARE" ident ":" type_spec NEWLINE ;
constant       = "CONSTANT" ident "=" [ "-" ] literal NEWLINE ;
type_spec      = "INTEGER" | "REAL" | "CHAR" | "STRING" | "BOOLEAN" | "DATE"
               | ident
               | "ARRAY" "[" bounds { "," bounds } "]" "OF" type_spec ;
bounds         = expression ":" expression ;
type_def       = "TYPE" ident "=" "(" ident { "," ident } ")" NEWLINE
               | "TYPE" ident "=" "^" type_spec NEWLINE
               | "TYPE" ident "=" "SET" "OF" type_spec NEWLINE
               | "TYPE" ident NEWLINE { declare } "ENDTYPE" NEWLINE ;
set_define     = "DEFINE" ident "(" literal { "," literal } ")" ":" ident NEWLINE ;

(* ---------- subroutines ---------- *)
subroutine     = procedure | function ;
procedure      = "PROCEDURE" ident [ "(" [ params ] ")" ] NEWLINE
                 block "ENDPROCEDURE" NEWLINE ;
function       = "FUNCTION" ident "(" [ params ] ")" "RETURNS" type_spec NEWLINE
                 block "ENDFUNCTION" NEWLINE ;
params         = param { "," param } ;
param          = [ "BYVAL" | "BYREF" ] ident ":" param_type ;
param_type     = type_spec | "ARRAY" "OF" type_spec ;

(* ---------- classes ---------- *)
class_def      = "CLASS" ident [ "INHERITS" ident ] NEWLINE { member } "ENDCLASS" NEWLINE ;
member         = [ "PUBLIC" | "PRIVATE" ] ( ident ":" type_spec NEWLINE | procedure | function )
               | declare
               | assignment ;                 (* attribute initialiser *)

(* ---------- statements ---------- *)
assignment     = lvalue "←" expression NEWLINE ;
lvalue         = ident { "[" expression { "," expression } "]" | "." ident | "^" } ;
input          = "INPUT" lvalue NEWLINE ;
output         = "OUTPUT" expression { "," expression } NEWLINE ;
call           = "CALL" callee [ "(" [ args ] ")" ] NEWLINE
               | callee "(" [ args ] ")" NEWLINE ;   (* method calls only, see 11 *)
callee         = ident | ident "." ident | "SUPER" "." ident ;
args           = expression { "," expression } ;
if_stmt        = "IF" expression "THEN" NEWLINE block
                 [ "ELSE" NEWLINE block ] "ENDIF" NEWLINE ;
case_stmt      = "CASE" "OF" expression NEWLINE { case_clause }
                 [ "OTHERWISE" [ ":" ] clause_body ] "ENDCASE" NEWLINE ;
case_clause    = case_label { "," case_label } ":" clause_body ;
case_label     = label_value [ "TO" label_value ] ;
label_value    = [ "-" ] literal | ident ;      (* constant or enum value *)
clause_body    = [ stmt_inline ] NEWLINE block ;   (* ends at next label, OTHERWISE, ENDCASE *)
for_stmt       = "FOR" ident "←" expression "TO" expression [ "STEP" expression ] NEWLINE
                 block "NEXT" [ ident ] NEWLINE ;
repeat_stmt    = "REPEAT" NEWLINE block "UNTIL" expression NEWLINE ;
while_stmt     = "WHILE" expression [ "DO" ] NEWLINE block "ENDWHILE" NEWLINE ;
return_stmt    = "RETURN" expression NEWLINE ;
file_stmt      = "OPENFILE" expression "FOR" file_mode NEWLINE
               | "READFILE" expression "," lvalue NEWLINE
               | "WRITEFILE" expression "," expression NEWLINE
               | "CLOSEFILE" expression NEWLINE
               | "SEEK" expression "," expression NEWLINE
               | "GETRECORD" expression "," lvalue NEWLINE
               | "PUTRECORD" expression "," lvalue NEWLINE ;
file_mode      = "READ" | "WRITE" | "APPEND" | "RANDOM" ;

(* ---------- expressions: lowest to highest precedence ---------- *)
expression     = or_expr ;
or_expr        = and_expr { "OR" and_expr } ;
and_expr       = not_expr { "AND" not_expr } ;
not_expr       = "NOT" not_expr | comparison ;
comparison     = concat [ rel_op concat ] ;     (* non-associative *)
rel_op         = "=" | "<>" | "<" | ">" | "<=" | ">=" | "IN" ;
concat         = additive { "&" additive } ;
additive       = term { ( "+" | "-" ) term } ;
term           = unary { ( "*" | "/" | "DIV" | "MOD" ) unary } ;
unary          = ( "-" | "+" ) unary | "^" lvalue | postfix ;
postfix        = primary { "[" args "]" | "(" [ args ] ")" | "." ident | "^" } ;
primary        = literal | ident | "(" expression ")"
               | "NEW" ident "(" [ args ] ")" | "SUPER" "." ident ;
literal        = INTEGER_LIT | REAL_LIT | CHAR_LIT | STRING_LIT | DATE_LIT | "TRUE" | "FALSE" ;
```

### 3.2 Parsing notes

- **Block terminators.** A `block` stops, without consuming, at any of: `ELSE`, `ENDIF`, `OTHERWISE`, `ENDCASE`, `NEXT`, `UNTIL`, `ENDWHILE`, `ENDPROCEDURE`, `ENDFUNCTION`, `ENDTYPE`, `ENDCLASS`, end of file, or (inside a CASE) a case-label line. A terminator that does not match the open construct is a syntax error naming both, e.g. *ENDWHILE found, but the IF on line 12 is still open*.
- **Case-label lines.** Inside `CASE`, a line is a label line if it does not start with a keyword and contains `:` at bracket depth 0 before its `NEWLINE`. No ordinary statement fits that shape, so one token of lookahead plus a line scan is enough.
- **`stmt_inline`** is any single statement written on the label's line, as in `'W' : Position ← Position − 10`. Further statements for that label follow on later lines.
- **`NEXT` identifier.** Optional; if present it MUST match the loop variable.
- **`WHILE … DO`.** `DO` is accepted for older papers and ignored. It is not reserved elsewhere, so `DO` is listed as a contextual keyword rather than in 2.1.
- **`CALL X` without parentheses** is accepted, as the guide's own CASE example uses `CALL Beep`.
- **`ELSE IF`.** There is no `ELSEIF`. A nested `IF` inside an `ELSE` block needs its own `ENDIF`. `ELSE IF` written on one line is reported as a syntax error, and the rest of the IF is still parsed so that only one error is reported.
- **Constructor name.** `NEW` is a keyword, but `PROCEDURE NEW` is accepted inside a `CLASS` as its constructor (11.3). Elsewhere it is a syntax error.
- **One variable per DECLARE.** `DECLARE A, B : INTEGER` is a syntax error.

## 4. Data types and values

The language is statically typed where types are declared and checked at runtime everywhere. Every variable has one type for its whole lifetime, fixed at declaration or, for undeclared variables, at first assignment (Section 5.1).

### 4.1 Atomic types

| Type | Holds | Suggested C++ representation | OUTPUT format |
| --- | --- | --- | --- |
| INTEGER | Whole numbers | `int64_t` | Denary, `-` for negatives |
| REAL | Numbers with a fractional part | `double` | Shortest round-trip form, always with a decimal point: `5.0`, `0.1`, `3.14159`; very large or small values use an exponent, still with a decimal point: `1.0e+20`, `1.5e-07` |
| CHAR | One character | `char` (byte) | The character itself |
| STRING | Zero or more characters | `std::string` | The characters, no quotes |
| BOOLEAN | `TRUE` / `FALSE` | `bool` | `TRUE` or `FALSE` |
| DATE | A valid calendar date | struct of day, month, year | `dd/mm/yyyy`, zero-padded |

INTEGER overflow (beyond 64-bit range) is a runtime error (R005), not wraparound. A REAL result too large to represent is the same error.

### 4.2 Composite and user-defined types

| Type | Value semantics | Notes |
| --- | --- | --- |
| ARRAY | Copied on assignment and BYVAL | 1D or 2D (higher dimensions accepted); bounds fixed at declaration |
| Record (`TYPE … ENDTYPE`) | Copied on assignment and BYVAL | Fields accessed with `.` |
| Enumerated | Value | Ordered by position in its declaration, starting at 0 |
| Pointer | Value (the address is copied) | Refers to a variable's storage cell |
| Set | Value | Immutable once defined with `DEFINE` |
| Class instance | **Reference** — assignment shares the object | Section 11 |

A suggested runtime representation is a `std::variant` over the atomic C++ types above plus `std::shared_ptr` handles for arrays, records, objects and sets, with `std::monostate` meaning *unassigned*.

**Type identity.** Records, enumerated types, sets and classes are *nominal*: each definition is a distinct type, even if two have the same fields or values. Arrays and pointers are *structural*: two array types are the same when their element types and number of dimensions match, and two pointer types are the same when their target types match. So `^Counter`, a pointer to an INTEGER, can be stored in a variable of type `TIntPointer = ^INTEGER`.

### 4.3 Implicit conversions

Only two conversions happen automatically. Everything else is a type error.

| From | To | Where it applies |
| --- | --- | --- |
| INTEGER | REAL | Assignment, arguments, return values, mixed arithmetic and comparison |
| CHAR | STRING | Assignment, arguments, return values, `&`, and comparison with a STRING |

REAL never converts to INTEGER implicitly; use `INT`. STRING never converts to CHAR, even when its length is 1. Numbers and strings never convert to each other; use `NUM_TO_STR` and `STR_TO_NUM`. This list is final: no other implicit conversion is made.

The same rules apply to built-in function arguments (Section 9) and file names (Section 10), so a CHAR is accepted wherever a STRING is expected. BYREF arguments are the exception: their type must match exactly (8.3).

### 4.4 Comparison rules

| Operand types | `=` `<>` | `<` `>` `<=` `>=` |
| --- | --- | --- |
| INTEGER / REAL (mixed allowed) | Numeric, exact for REAL | Numeric |
| CHAR / STRING (mixed allowed) | Character by character | Lexicographic by character code, case-sensitive |
| BOOLEAN | Allowed | Type error |
| DATE | Allowed | Chronological |
| Same enumerated type | Allowed | By ordinal position |
| Pointer | Same target cell | Type error |
| Array, record, set, object | Type error | Type error |

## 5. Declarations and scope

### 5.1 Variables

```
DECLARE Counter : INTEGER
DECLARE StudentNames : ARRAY[1:30] OF STRING
```

Declaration is good practice but not mandatory, because exam code routinely uses undeclared variables (e.g. `Temp` in the guide's `SWAP`). The interpreter has two modes:

- **Default mode.** Assigning to a name not visible in any scope creates a variable in the current scope, typed by the assigned value. A variable created this way inside a subroutine is local to it. A `FOR` loop variable that is not declared is created as INTEGER. `INPUT` into an undeclared name infers the type from the text typed (Section 7.2). An undeclared name passed to a BYREF parameter is created the same way and takes the parameter's type.
- **Strict mode** (`--strict`). Every variable MUST be declared before use. Undeclared names are semantic errors (M001) before execution starts, including a use in the main program above the variable's `DECLARE`.

Reading a variable that has been declared but never assigned is a runtime error: *Variable Total used before it was given a value*. The `--defaults` flag instead initialises variables to their type's starting value:

| Type | Starting value with `--defaults` |
| --- | --- |
| INTEGER, REAL | `0`, `0.0` |
| CHAR | `' '` (a space) |
| STRING | `""` |
| BOOLEAN | `FALSE` |
| DATE | `01/01/0001` |
| Enumerated | Its first value |
| Set | The empty set |

Arrays and records always start with every element and field created, each starting as above. Pointers and objects always start null (5.5, 11.3), with or without `--defaults`.

Redeclaring a name in the same scope is a semantic error (M002). So is declaring a name that has already been given a value as an undeclared variable in that scope: the `DECLARE` must come before the first use. Running the same `DECLARE` statement again, for example inside a loop, is not a redeclaration: it creates the variable afresh, with no value and with its array bounds evaluated again.

### 5.2 Constants

```
CONSTANT HourlyRate = 6.50
CONSTANT DefaultText = "N/A"
```

The value MUST be a literal, optionally with a leading minus, which is only allowed on a number. Expressions, variables and other constants are not allowed. The constant's type is the literal's type. Assigning to a constant (M007), passing it BYREF (M006) or taking its address with `^` (M004) is a semantic error.

### 5.3 Arrays

```
DECLARE Grid : ARRAY[1:3, 1:3] OF CHAR
Grid[2, 3] ← 'X'
```

- Bounds are integer expressions evaluated when the `DECLARE` executes. Lower bound may be any integer, including 0 or negative; upper MUST be ≥ lower, or it is runtime error R003. An array may have at most 4,000,000 elements in all (R003), so a mistyped bound fails clearly instead of using up memory.
- Every index access is bounds-checked. Out of range is a runtime error naming the array, the index and the bounds.
- The number of indices MUST equal the number of dimensions (M004 where the array's type is known before running, R002 otherwise).
- Whole-array assignment (`SavedGame ← Grid`) is allowed when element type and every dimension's size match. It copies all elements into the target's existing elements, so the target keeps its own bounds and pointers to its elements stay valid. Arrays of different sizes are runtime error R002.
- Group assignment such as `Names[1 TO 30] ← ""` is not part of the language and is a syntax error.

### 5.4 Enumerated types

```
TYPE Season = (Spring, Summer, Autumn, Winter)
DECLARE ThisSeason : Season
ThisSeason ← Spring
```

Each value name becomes a constant of the enumerated type. Values are ordered by position. `Season + INTEGER` gives the value that many places later; going past either end is a runtime error. OUTPUT prints the value's name.

### 5.5 Pointer types

```
TYPE TIntPointer = ^INTEGER
DECLARE MyPointer : TIntPointer
MyPointer ← ^Counter       // address-of
MyPointer^ ← 5             // dereference
```

- Prefix `^` on a variable (or array element or record field) yields a pointer to its storage cell. The cell's type MUST match the pointer's target type.
- Postfix `^` dereferences, for reading or writing.
- A pointer that has never been assigned is *null*. Dereferencing it is a runtime error. There is no `NULL` keyword.
- Pointers to a subroutine's local variables become invalid when it returns; dereferencing one afterwards is a runtime error. Using shared, reference-counted cells makes this detectable.
- Pointer types are compared by target type (4.2). The address of a constant cannot be taken, nor that of an undeclared variable before it has been given a value. Pointers cannot be output.

### 5.6 Record types

```
TYPE StudentRecord
   DECLARE LastName : STRING
   DECLARE YearGroup : INTEGER
ENDTYPE
DECLARE Pupil1 : StudentRecord
Pupil1.LastName ← "Johnson"
```

Fields may be of any type, including arrays and other records. A record type cannot contain itself, directly or through arrays and other records, since it would be infinitely large (M004). A pointer field breaks the chain, so linked structures use pointers. Field names are case-insensitive, and a record cannot have two fields with the same name (M002). Records may be assigned whole (`Pupil2 ← Pupil1`), which copies every field into the target's existing fields.

### 5.7 Set types

```
TYPE LetterSet = SET OF CHAR
DEFINE Vowels ('A', 'E', 'I', 'O', 'U') : LetterSet
```

`DEFINE` creates a named, read-only set, which behaves as a constant. A set's base type MUST be an atomic or enumerated type (M004). Element literals MUST match the base type after the conversions in 4.3; duplicates are ignored. The guide defines no set operations, so this spec adds one: `<value> IN <set>` returns BOOLEAN (Section 13). Sets cannot be compared with each other or output.

### 5.8 Scope and name resolution

There are three kinds of scope: **global** (top level), **local** (one per subroutine call), and **class** (attributes of the current object). There is no block scope: a variable declared inside an `IF` or loop belongs to the enclosing subroutine or global scope.

A name is resolved in this order: local scope, then the current object's attributes (inside a method), then global scope, then built-ins. Procedures and functions can therefore read and write global variables, as exam code assumes. A local declaration with the same name as a global hides the global inside that subroutine.

Inside a method, the attributes and methods that the method's class can see (11.2) come between the local and global scopes.

Type names, subroutine names, class names, enumerated value names and variable names share one namespace, and built-in function names are reserved. Declaring a variable, type or subroutine with the same name as another of these is a semantic error (M002).

## 6. Expressions

### 6.1 Precedence and associativity

Higher rows bind tighter. All binary operators are left-associative except comparisons, which do not chain: `A < B < C` is a syntax error.

| Level | Operators | Operand types | Result |
| --- | --- | --- | --- |
| 1 (highest) | `[ ]` `( )` `.` postfix `^` | — | — |
| 2 | unary `-` `+`, prefix `^` | Numeric; lvalue for `^` | Same numeric type; pointer |
| 3 | `*` `/` `DIV` `MOD` | Numeric | See 6.2 |
| 4 | `+` `-` | Numeric; enum + INTEGER | See 6.2 |
| 5 | `&` | STRING or CHAR | STRING |
| 6 | `=` `<>` `<` `>` `<=` `>=` `IN` | See 4.4 | BOOLEAN |
| 7 | `NOT` | BOOLEAN | BOOLEAN |
| 8 | `AND` | BOOLEAN | BOOLEAN |
| 9 (lowest) | `OR` | BOOLEAN | BOOLEAN |

`NOT` sits below comparison, so `NOT X = 5` means `NOT (X = 5)`. `&` sits below arithmetic, so `"Total: " & NUM_TO_STR(A + B)` needs no extra brackets around `A + B`.

### 6.2 Arithmetic

| Operation | INTEGER, INTEGER | Any REAL operand |
| --- | --- | --- |
| `+` `-` `*` | INTEGER | REAL |
| `/` | **REAL**, always | REAL |
| `DIV` | INTEGER, truncated toward zero | Type error |
| `MOD` | INTEGER, sign follows the left operand | Type error |

`DIV` and `MOD` satisfy `A = B * (A DIV B) + (A MOD B)`. For example, `-7 DIV 2` is `-3` and `-7 MOD 2` is `-1`. Division or `DIV`/`MOD` by zero is a runtime error (R004), and so is `/` by `0.0`. A result outside the INTEGER range, or a REAL result too large to represent, is R005.

### 6.3 Evaluation

- Operands are evaluated left to right.
- `AND` and `OR` **short-circuit**: the right operand is not evaluated if the left decides the result. This lets `WHILE Index <= 10 AND List[Index] <> 0` run safely. If the left operand is not BOOLEAN, that is reported straight away, without evaluating the right one.
- Function calls are evaluated when reached, so any side effects on global variables happen in left-to-right order.
- A procedure name used in an expression is a semantic error: *Square is a procedure and does not return a value; use CALL*.

## 7. Statements

### 7.1 Assignment

`<lvalue> ← <expression>`. The lvalue is a variable, array element, record field, dereferenced pointer or object attribute. The expression's type MUST match the target's type after the implicit conversions in 4.3. The right side is fully evaluated before anything is stored.

### 7.2 INPUT and OUTPUT

**OUTPUT** evaluates each expression left to right, converts it using the formats in 4.1, writes them with **no separator**, then writes a newline. An enumerated value is written as its name. Arrays, records, sets, objects and pointers cannot be output (M004 where known before running, R002 otherwise).

```
OUTPUT "You have ", Lives, " lives left"
```

**INPUT** reads one line from standard input, strips the trailing newline (and a carriage return before it, for input typed on Windows), and converts it to the target's type:

| Target type | Accepted text | Otherwise |
| --- | --- | --- |
| INTEGER | Optional `+` or `-`, then digits; no spaces | Runtime error (R005 if too large) |
| REAL | Integer or real form (digits `.` digits), optional sign | Runtime error |
| CHAR | Exactly one character (one byte, D17) | Runtime error |
| STRING | Anything, including empty | — |
| BOOLEAN | `TRUE` or `FALSE`, any case | Runtime error |
| DATE | `dd/mm/yyyy`, valid date | Runtime error |
| Undeclared name (default mode) | Integer form → INTEGER; real form → REAL; `TRUE`/`FALSE` → BOOLEAN; one character → CHAR; else STRING | — |

Bad input for the target's type is R002. Only these types can be read: INPUT into an enumerated, array, record, pointer, set or object variable is M004 (R002 if only known while running). INPUT does not print a prompt; any OUTPUT before it is shown first. End of input before a line is read is a runtime error (R014).

### 7.3 IF

```
IF <condition> THEN
   <statements>
ELSE
   <statements>
ENDIF
```

The condition MUST be BOOLEAN; there is no truthiness. `ELSE` is optional. `THEN` may be on the next line (2.6).

### 7.4 CASE

```
CASE OF Move
   'W' : Position ← Position − 10
   'A' TO 'D' : CALL Strafe()
   OTHERWISE : CALL Beep
ENDCASE
```

- The selector is evaluated once. It MUST be INTEGER, CHAR, STRING, enumerated or DATE; REAL and BOOLEAN selectors are type errors.
- Labels MUST be literals, constants or enum values of a type comparable with the selector (M004 before running, R002 when the types are only known while running). `X TO Y` matches any value with X ≤ value ≤ Y.
- Clauses are tested top to bottom. The first match runs and the CASE ends; there is no fall-through.
- `OTHERWISE`, if present, MUST be last. If nothing matches and there is no `OTHERWISE`, execution simply continues after `ENDCASE`.
- Overlapping labels are allowed (the first wins) and produce warning W002, which says whether the later label can never match. Only labels whose values are known before running (literals, constants and enumerated values) are checked.

### 7.5 FOR

```
FOR Index ← 1 TO 30 STEP 1
   <statements>
NEXT Index
```

1. The loop variable MUST be INTEGER. Start, end and step MUST be INTEGER expressions.
2. Start, end and step are evaluated **once**, before the first iteration, in that order. `STEP` defaults to 1.
3. A step of 0 is a runtime error.
4. With a positive step, the body runs while `variable ≤ end`; with a negative step, while `variable ≥ end`. If the condition fails at the start, the body never runs.
5. After each iteration the variable increases by the step. Assigning to the loop variable inside the body is a semantic error (M007). This includes INPUT, READFILE or GETRECORD into it, a nested FOR using the same variable, and passing it to a BYREF parameter. The loop counts with its own copy of the value, so the number of passes is fixed when the loop starts. If adding the step would go past the INTEGER range, that is R005.
6. After the loop ends normally, the variable holds the first value that failed the test, e.g. 31 after \`FOR I ← 1 TO 30\`. If the body never ran, it holds the start value.

### 7.6 REPEAT

```
REPEAT
   <statements>
UNTIL <condition>
```

The body runs at least once. The BOOLEAN condition is tested after each pass; the loop ends when it is `TRUE`.

### 7.7 WHILE

```
WHILE <condition>
   <statements>
ENDWHILE
```

The BOOLEAN condition is tested before each pass; the loop ends when it is `FALSE`. The body may run zero times.

### 7.8 Infinite-loop guard

The interpreter offers `--max-steps N`, which stops with runtime error R012 after N steps. Every statement is a step each time it runs, including the IF, CASE and loop statements themselves, and so is each time round a loop, so a loop with an empty body (`WHILE TRUE DO ENDWHILE`) still stops. This is meant for automated marking and classroom use, where a missing increment otherwise hangs the run.

## 8. Procedures and functions

### 8.1 Procedures

```
PROCEDURE Square(Size : INTEGER)
   FOR Side ← 1 TO 4
      CALL MoveForward(Size)
   NEXT Side
ENDPROCEDURE

CALL Square(100)
```

A procedure is called only with `CALL`, as a complete statement. Writing `Square(100)` without `CALL` is M005; only methods may be called without `CALL` (11.4). The parentheses may be omitted when there are no parameters. A procedure has no `RETURN` statement (M008); it ends at `ENDPROCEDURE`.

### 8.2 Functions

```
FUNCTION Max(Number1 : INTEGER, Number2 : INTEGER) RETURNS INTEGER
   IF Number1 > Number2 THEN
      RETURN Number1
   ELSE
      RETURN Number2
   ENDIF
ENDFUNCTION

OUTPUT "Penalty Fine = ", Max(10, Distance * 2)
```

- A function is called only inside an expression, always with parentheses, even with no arguments. Using `CALL` on a function is a semantic error.
- `RETURN <expression>` ends the function immediately. The value MUST match the declared return type after 4.3 conversions.
- Reaching `ENDFUNCTION` without executing `RETURN` is a runtime error (R007): *Function Max ended without returning a value*. It is reported at the `FUNCTION` line.
- A function used as a statement, with or without `CALL`, is M005.
- `RETURN` outside a function is a semantic error.

### 8.3 Parameter passing

```
PROCEDURE Swap(BYREF X : INTEGER, Y : INTEGER)
```

- Parameters are **BYVAL** by default. A BYVAL parameter receives a copy; changes do not affect the caller. Arrays and records passed BYVAL are copied in full.
- A **BYREF** parameter is bound to the caller's storage cell. Changes are visible to the caller immediately.
- A `BYVAL` or `BYREF` keyword applies to its parameter **and every following parameter** until the other keyword appears. In the example above, both X and Y are BYREF, as the guide states.
- A BYREF argument MUST be an lvalue (variable, array element, record field). Passing a literal, an expression or a constant (including an enumerated value) is a semantic error (M006). Its type MUST match exactly; INTEGER→REAL conversion does not apply to BYREF. An undeclared variable passed BYREF is created and takes the parameter's type (5.1).
- Functions MUST NOT have BYREF parameters. This is a semantic error, as the guide forbids it.
- Argument count and types are checked against the definition. Mismatches are semantic errors where the types are known before running, runtime errors otherwise.
- Arguments are evaluated left to right before the call.

**Array parameters.** The guide does not show array parameter syntax. This spec accepts both forms:

| Form | Meaning |
| --- | --- |
| `Names : ARRAY[1:30] OF STRING` | Argument MUST have exactly these bounds |
| `Names : ARRAY OF STRING` | Any bounds and any number of dimensions; the parameter takes the argument's bounds |

Bounds written in a parameter are evaluated when the call is made, before the subroutine starts. An argument with other bounds is R002.

### 8.4 Call stack and recursion

Each call creates a fresh local scope, so recursion works without special handling. The default maximum call depth is 10,000, adjustable with `--max-depth`. Exceeding it is a runtime error (R011): *Too many nested calls (possible infinite recursion) in Factorial*. Programs run with a 512 MiB stack, and the interpreter also stops with R011 if the calls use up that stack, whatever `--max-depth` says. In a web page the stack is smaller (12.5).

Every runtime error prints the chain of active calls with line numbers. When there are more than 16 calls, only the innermost 8 and outermost 8 are shown, with a count of the rest.

## 9. Built-in functions

All built-ins are functions: they are called in expressions, never with `CALL` (M005). String positions are 1-based throughout, and characters are bytes (D17). The **Source** column shows whether a function is in the guide itself or only in the insert printed with 9618 exam papers.

Arguments follow the conversions in 4.3, so a CHAR is accepted for a STRING parameter and an INTEGER for a REAL one. The wrong number of arguments is M003. An argument of the wrong type is M004 where its type is known before running, R002 otherwise, and the error points at that argument. An argument value the function cannot use is R006, except `MOD` and `DIV` by zero (R004) and results too large for an INTEGER (R005).

### 9.1 String and character functions

| Function | Returns | Behaviour | Errors | Source |
| --- | --- | --- | --- | --- |
| `LENGTH(S : STRING)` | INTEGER | Number of characters in S | — | Guide |
| `LEFT(S : STRING, X : INTEGER)` | STRING | Leftmost X characters | X < 0 or X > LENGTH(S) | Insert |
| `RIGHT(S : STRING, X : INTEGER)` | STRING | Rightmost X characters; `RIGHT("ABCDEFGH", 3)` = `"FGH"` | X < 0 or X > LENGTH(S) | Guide |
| `MID(S : STRING, X : INTEGER, Y : INTEGER)` | STRING | Y characters starting at position X; `MID("ABCDEFGH", 2, 3)` = `"BCD"` | X < 1, Y < 0, or X + Y − 1 > LENGTH(S) | Guide |
| `LCASE(C : CHAR)` | CHAR | Lower-case equivalent; only ASCII letters change | A STRING argument (use `TO_LOWER`) | Guide |
| `UCASE(C : CHAR)` | CHAR | Upper-case equivalent; only ASCII letters change | A STRING argument (use `TO_UPPER`) | Guide |
| `TO_LOWER(S)` | Same as argument (CHAR or STRING) | Lower-cases every ASCII letter | — | Insert |
| `TO_UPPER(S)` | Same as argument (CHAR or STRING) | Upper-cases every ASCII letter | — | Insert |
| `NUM_TO_STR(N)` | STRING | INTEGER or REAL to text, using OUTPUT format (4.1) | — | Insert |
| `STR_TO_NUM(S)` | INTEGER or REAL | Text to number: INTEGER if S has integer form, else REAL; accepts what `IS_NUM` accepts | S not a valid number (R006); an INTEGER too large (R005) | Insert |
| `IS_NUM(S)` | BOOLEAN | TRUE if S (CHAR or STRING) is a valid integer or real: optional leading `+` or `-`, digits, optionally `.` and digits, no spaces | — | Insert |
| `ASC(C : CHAR)` | INTEGER | Character code (ASCII) | — | Insert |
| `CHR(X : INTEGER)` | CHAR | Character with code X | X outside 0–127 | Insert |

### 9.2 Numeric functions

| Function | Returns | Behaviour | Errors | Source |
| --- | --- | --- | --- | --- |
| `INT(X : REAL)` | INTEGER | Integer part, truncated toward zero; `INT(27.5415)` = 27, `INT(-2.7)` = −2; an INTEGER argument is returned unchanged | Result out of 64-bit range (R005) | Guide |
| `RAND(X : INTEGER)` | REAL | Random real in \[0, X) | X ≤ 0 | Guide |
| `ROUND(X : REAL, Places : INTEGER)` | REAL | X rounded to Places decimal places, halves away from zero, working on the decimal digits OUTPUT shows: `ROUND(2.675, 2)` = 2.68 although 2.675 is stored as 2.67499… | Places < 0 | Insert |
| `MOD(A : INTEGER, B : INTEGER)` | INTEGER | Same as `A MOD B` | B = 0 | Insert |
| `DIV(A : INTEGER, B : INTEGER)` | INTEGER | Same as `A DIV B` | B = 0 | Insert |

`MOD` and `DIV` are keywords, but when followed directly by `(` at the start of an operand they parse as these function calls. `RAND` uses the 64-bit Mersenne Twister (`std::mt19937_64`), with its output scaled by the interpreter rather than a library distribution, so `--seed N` gives the same numbers on every platform. Without `--seed` it is seeded randomly.

### 9.3 Date functions

| Function | Returns | Behaviour | Errors | Source |
| --- | --- | --- | --- | --- |
| `DAY(D : DATE)` | INTEGER | Day of month, 1–31 | — | Insert |
| `MONTH(D : DATE)` | INTEGER | Month, 1–12 | — | Insert |
| `YEAR(D : DATE)` | INTEGER | Year | — | Insert |
| `DAYINDEX(D : DATE)` | INTEGER | Day of week, Sunday = 1 … Saturday = 7, for any date from `01/01/0001` (proleptic Gregorian calendar) | — | Insert |
| `SETDATE(Day, Month, Year : INTEGER)` | DATE | Builds a date; the order is day, month, year | Not a valid calendar date | Insert |
| `NOW()` | DATE | Today's date from the system clock, in the computer's local time zone | — | Insert |

### 9.4 File function

| Function | Returns | Behaviour | Errors | Source |
| --- | --- | --- | --- | --- |
| `EOF(F : STRING)` | BOOLEAN | TRUE if no lines remain to read in file F (or F is empty) | F not open for READ | Guide |

## 10. File handling

A file is identified by its name: a STRING literal or a STRING variable holding the name (a CHAR is also accepted, 4.3). Relative names are resolved against the folder containing the program file; absolute paths are used as they are. Names may contain non-ASCII characters (UTF-8). The interpreter keeps a table of open files keyed by name, matched case-insensitively (D14); a file may be open in only one mode at a time.

File problems are runtime error R010, including an empty name or a name that is a folder. Values of the wrong type are R002 (M004 where known before running). Every file statement evaluates the file name first.

### 10.1 Text files

```
OPENFILE "FileA.txt" FOR READ
WHILE NOT EOF("FileA.txt")
   READFILE "FileA.txt", LineOfText
   OUTPUT LineOfText
ENDWHILE
CLOSEFILE "FileA.txt"
```

| Statement | Effect | Runtime errors |
| --- | --- | --- |
| `OPENFILE F FOR READ` | Opens an existing file for reading from the first line | File does not exist; F already open |
| `OPENFILE F FOR WRITE` | Creates F, or empties it if it exists | F already open; cannot create |
| `OPENFILE F FOR APPEND` | Opens F for writing after existing content; creates it if absent | F already open |
| `READFILE F, Var` | Reads the next line, without its line ending, into Var (STRING) | F not open for READ; no lines left; Var not STRING |
| `WRITEFILE F, Expr` | Writes Expr as one line. Atomic values are converted using the OUTPUT formats | F not open for WRITE or APPEND; Expr not atomic |
| `CLOSEFILE F` | Flushes and closes F | F not open |

Line endings `\n` and `\r\n` are both accepted when reading; `\n` is written. A file opened FOR READ is read in full when it is opened. A final line without a line ending still counts, blank lines are kept, and an empty file has no lines. READFILE into an undeclared variable makes it a STRING.

Files still open when the program ends normally are closed automatically with warning W004, pointing at their OPENFILE. After a runtime error they are still closed and saved, but without the warning, since the program never reached its CLOSEFILE. A file that cannot be saved is always reported.

### 10.2 Random files

```
OPENFILE "StudentFile.Dat" FOR RANDOM
SEEK "StudentFile.Dat", 10
GETRECORD "StudentFile.Dat", Pupil
PUTRECORD "StudentFile.Dat", NewPupil
CLOSEFILE "StudentFile.Dat"
```

| Statement | Effect | Runtime errors |
| --- | --- | --- |
| `OPENFILE F FOR RANDOM` | Opens F for record access; creates it if absent | F already open |
| `SEEK F, Address` | Moves the file pointer to record number Address | F not open for RANDOM; Address < 1 |
| `GETRECORD F, Var` | Reads the record at the pointer into Var | Empty slot; stored type differs from Var's type |
| `PUTRECORD F, Var` | Writes Var to the slot at the pointer, replacing what was there | F not open for RANDOM |

- Addresses are **1-based** record numbers: record 1 is the first record.
- Neither GETRECORD nor PUTRECORD moves the file pointer. Code must `SEEK` before each access, as the guide's example does.
- Any variable may be stored, though records are the normal case. Writing past the current end creates empty slots in between. Record addresses go up to 1,000,000 (R010 beyond).
- A random file is held in memory. OPENFILE reads the whole file, SEEK, GETRECORD and PUTRECORD work on that copy, and the file is written back at CLOSEFILE or when the program ends, including after a runtime error. An existing empty file opens as an empty random file; any other file not written by this interpreter is R010.
- Each record stores the exact type of the variable written: its array bounds, record field names and types, and enumerated values. GETRECORD reads only into a variable of exactly that type; INTEGER→REAL does not apply, and a mismatch is R002. GETRECORD into an undeclared variable works only for a single value (INTEGER, REAL, CHAR, STRING, BOOLEAN or DATE). PUTRECORD of a variable with no value is R001; unassigned elements and fields inside an array or record are stored as unassigned.
- Objects and pointers cannot be stored (M004, or R002 if only known while running): an address would mean nothing when read back.

**On-disk format** (version 1). The format is private to this interpreter and only needs to round-trip within it. It is text, but not meant to be edited by hand:

```
PSEUDOCODE RANDOM FILE 1
<number of slots>
then for each slot, either
-                   an empty slot
or
<n>                 the length of the payload in bytes
<payload>           n bytes, then a newline
```

A payload is the stored variable's type name, a newline, a signature describing its exact type, a newline, then the encoded value. A signature is a type name for atomic types, `ENUM Season(Spring,Summer,Autumn,Winter)`, `SET LetterSet OF CHAR`, `ARRAY[1:3] OF REAL`, or `RECORD StudentRecord{LastName:STRING;YearGroup:INTEGER;}`. Values are encoded as:

| Value | Encoding | Example |
| --- | --- | --- |
| Unassigned | `U` | `U` |
| INTEGER | `I` digits `;` | `I-42;` |
| REAL | `R` shortest round-trip form `;` | `R0.1;` |
| CHAR | `C` character code `;` | `C65;` |
| STRING | `S` length in bytes `:` the bytes | `S5:Hello` |
| BOOLEAN | `B1;` or `B0;` | `B1;` |
| DATE | `D` year `/` month `/` day `;` | `D2005/1/2;` |
| Enumerated | `E` ordinal `;` | `E2;` |
| Array | `A` element count `:` then each element, row by row | `A2:I1;I2;` |
| Record | `T` field count `:` then each field | `T2:S3:AnnI11;` |
| Set | `Z` element count `:` then each element | `Z2:C65;C69;` |

## 11. Object-oriented programming

```
CLASS Pet
   PRIVATE Name : STRING
   PUBLIC PROCEDURE NEW(GivenName : STRING)
      Name ← GivenName
   ENDPROCEDURE
   PUBLIC FUNCTION GetName() RETURNS STRING
      RETURN Name
   ENDFUNCTION
ENDCLASS

CLASS Cat INHERITS Pet
   PRIVATE Breed : STRING
   PUBLIC PROCEDURE NEW(GivenName : STRING, GivenBreed : STRING)
      SUPER.NEW(GivenName)
      Breed ← GivenBreed
   ENDPROCEDURE
ENDCLASS

DECLARE MyCat : Cat
MyCat ← NEW Cat("Kitty", "Shorthaired")
OUTPUT MyCat.GetName()
```

### 11.1 Members

- **Attributes** are declared as `[PUBLIC | PRIVATE] Name : Type`. `DECLARE Name : Type` inside a class is also accepted and is public.
- **Methods** are procedures and functions inside the class, optionally prefixed `PUBLIC` or `PRIVATE`.
- Members are **public by default**, as the guide states.
- An assignment directly in the class body (e.g. `Attempts ← 3`) is an attribute initialiser. Initialisers run in order when an object is created, before the constructor, and use the class's attributes by bare name. Any other statement directly in a class body is a syntax error.
- Each attribute and method name may be used only once in a class and its parents. A subclass cannot declare an attribute with the same name as a parent's, even a PRIVATE one, and a method cannot share a name with an attribute (M002). A method with the same name as a parent's method overrides it (11.4).
- Inside a method, attributes and other methods are used by bare name. There is no `SELF` or `THIS` keyword.

### 11.2 Visibility

`PRIVATE` members are accessible only inside methods of the class that declares them. Subclasses cannot use a parent's private members directly; they go through public methods (D15). Inside a subclass's methods a parent's private members are hidden, so using one by bare name is M009 rather than an undeclared variable. Accessing a private member from outside is a semantic error (M009) where the object's class is known statically, a runtime error otherwise.

### 11.3 Construction

- The constructor is a procedure named `NEW`. A class without one gets an implicit constructor with no parameters; passing it arguments is M003. Constructors are not inherited: a subclass without its own `NEW` gets the implicit constructor even if its parent has one.
- `NEW ClassName(args)` is an expression. It creates the object, runs the attribute initialisers of every class in the chain (parent first), then runs the class's `NEW`.
- A subclass constructor does not call its parent's constructor automatically. It calls it explicitly with `SUPER.NEW(args)`. If the parent has no constructor, `SUPER.NEW()` does nothing.
- A variable of class type that has not been assigned an object is null. Using it is a runtime error.

### 11.4 Inheritance and method calls

- `CLASS B INHERITS A` gives B all of A's attributes and methods. Only single inheritance is supported. An unknown parent is M001; a parent that is not a class, or an inheritance cycle, is M004.
- A method in B with the same name as one in A **overrides** it. Calls dispatch on the object's actual class at runtime, including calls by bare name from inside a method. PRIVATE methods are not overridden: calling one runs the declaring class's version.
- `SUPER.Method(args)` calls the parent's version and is valid only inside a method of a class that has a parent (M008).
- Method calls may be written as statements with or without `CALL`: `Player.SetAttempts(5)` and `CALL Player.SetAttempts(5)` are equivalent. Function methods are used in expressions as normal.
- An object of class B may be assigned to a variable or parameter of type A when B inherits from A, directly or indirectly.

### 11.5 Object semantics

Objects are **references**. `Cat2 ← Cat1` makes both names refer to the same object, and passing an object BYVAL copies the reference, not the object. Objects cannot be compared with `=`, output with OUTPUT, or written to text files.

## 12. Errors

Errors are reported in four phases. The first three run before any code executes, so a program with a syntax or semantic error produces no output at all. Execution stops at the first runtime error.

- **Lexical and syntax.** After an error the lexer and parser recover and carry on, so one run reports every such error. At most one syntax error is reported per line, since later ones are usually knock-on effects.
- **Semantic.** The whole program is checked: names, types, arguments, visibility and the rules of Sections 5–11. A check that involves a type only known while running, such as the type of an undeclared variable (fixed by whichever assignment runs first), passes and is made at runtime instead.
- **Runtime.** A runtime error points at the innermost expression it came from, such as the index in `Names[I]`. The few semantic checks that can only fail at runtime keep their M code and are reported as runtime errors, e.g. *Runtime error M009* for a private member of an object whose class was not known before running.

Errors and warnings found before running are printed in source order.

### 12.1 Message format

```
<file>:<line>:<column>: <Phase> error <Code>: <message>
   <the source line>
   <caret under the column>
```

Runtime errors add the active call chain, innermost first:

```
program.txt:14:7: Runtime error R003: Index 31 is outside the bounds of Names [1:30]
   Names[I] ← ""
         ^
   in procedure ClearNames (line 14), called from line 40
```

Messages SHOULD be written for students: plain English, naming the identifier involved, and suggesting the likely fix where one is obvious. Warnings use the same format with `warning` and never stop execution.

### 12.2 Catalogue

| Code | Phase | Condition |
| --- | --- | --- |
| L001 | Lexical | Unterminated string or char literal |
| L002 | Lexical | Invalid character in source |
| L003 | Lexical | Keyword not in upper case |
| L004 | Lexical | Malformed number (e.g. `5.` or `.5`) or invalid date literal |
| S001 | Syntax | Unexpected token (message names what was expected) |
| S002 | Syntax | Block not closed, or closed by the wrong keyword |
| S003 | Syntax | `NEXT` variable does not match the `FOR` variable |
| S004 | Syntax | `OTHERWISE` not the last CASE clause |
| S005 | Syntax | Definition (`TYPE`, `PROCEDURE`, `FUNCTION`, `CLASS`) not at top level |
| M001 | Semantic | Undeclared identifier (strict mode, including use above its DECLARE), or unknown type, subroutine, class, field, attribute or method |
| M002 | Semantic | Name declared twice in one scope (including a DECLARE after the name's first use), or clashes with a type, subroutine, class, enumerated value or built-in |
| M003 | Semantic | Wrong number of arguments |
| M004 | Semantic | Type mismatch detectable before running; an invalid type definition (inheritance cycle, record containing itself, set of a non-atomic type) |
| M005 | Semantic | `CALL` used on a function, a function used as a statement, a procedure used in an expression or called without `CALL` |
| M006 | Semantic | BYREF parameter in a function, or non-lvalue/constant/enumerated value passed BYREF |
| M007 | Semantic | Assignment to a constant or enumerated value, or to a FOR loop variable inside its loop |
| M008 | Semantic | `RETURN` outside a function, or `SUPER` outside a method or in a class with no parent |
| M009 | Semantic | Private member used from outside its class, including a parent's private member used in a subclass |
| R001 | Runtime | Variable used before it was given a value |
| R002 | Runtime | Type mismatch (including bad INPUT for the target type, and file values of the wrong type) |
| R003 | Runtime | Array index out of bounds; array bounds with upper < lower, or more than 4,000,000 elements |
| R004 | Runtime | Division, DIV or MOD by zero |
| R005 | Runtime | INTEGER overflow, or a REAL result too large to represent |
| R006 | Runtime | Invalid built-in argument (e.g. MID past the end, CHR out of range) |
| R007 | Runtime | Function ended without RETURN |
| R008 | Runtime | Null pointer or null object used; dangling pointer |
| R009 | Runtime | Enumerated value moved past its first or last value |
| R010 | Runtime | File error: not found, wrong mode, not open, already open, read past end, empty random slot, empty name or a folder, not a random file written by this interpreter, damaged random file, record address over 1,000,000 |
| R011 | Runtime | Call depth exceeded |
| R012 | Runtime | Step limit exceeded (`--max-steps`) |
| R013 | Runtime | FOR loop STEP is zero |
| R014 | Runtime | End of input reached during INPUT |

### 12.3 Warnings

| Code | Phase | Condition |
| --- | --- | --- |
| W001 | Semantic | Same identifier written with different capitalisation. Reported once per spelling, for variables, constants, parameters, types, classes, subroutines, fields, attributes, methods, enumerated values and built-in names |
| W002 | Semantic | Overlapping CASE labels whose values are known before running (7.4) |
| W003 | Semantic | Variable declared with `DECLARE` but never used. Constants, parameters and attributes are not checked |
| W004 | Runtime | File still open when the program ends normally (10.1) |

Warnings never stop execution or change the exit status.

Exit status: 0 on success, 1 for lexical, syntax or semantic errors, 2 for runtime errors.

### 12.4 Command line

```
PseudocodeInterpreter [options] [file]
```

| Option | Effect | Section |
| --- | --- | --- |
| `--strict` | Every variable must be declared | 5.1 |
| `--defaults` | Variables start with their type's starting value | 5.1 |
| `--max-steps N` | Stop with R012 after N steps | 7.8 |
| `--max-depth N` | Allow calls to nest N deep (default 10,000) | 8.4 |
| `--seed N` | Make `RAND` give the same numbers on every run | 9.2 |
| `--tokens`, `--ast`, `--types` | Print the tokens, the syntax tree or the defined types instead of running | — |
| `--help` | Print the options | — |

With no file, `test.txt` in the current folder is used. Program output goes to standard output; errors and warnings go to standard error.

### 12.5 WebAssembly

The interpreter also compiles to WebAssembly (`wasm/README.md`). Run by Node.js through `build-wasm/pseudocode`, it behaves exactly as described here, and passes the same tests. In a web page, a program is run by `runPseudocode` in `wasm/pseudocode.mjs`, and three things differ:

- All of the program's input is given before it runs. Running out of it is R014, as at the end of standard input.
- Files are held in memory for the one run. Each run starts afresh.
- A page cannot enlarge the browser's stack, so calls nest only about 1,000 to 1,500 deep, whatever `--max-depth` says. Going deeper is R011 (8.4), reported without a line or call chain.

## 13. Decisions, assumptions and open items

The guide states that pseudocode is not a language with a mandatory syntax, so an interpreter must fill gaps. Every choice this spec makes beyond the guide is listed here so it can be reviewed and reversed deliberately.

### 13.1 Decisions where the guide is silent or ambiguous

| # | Topic | Decision | Reason |
| --- | --- | --- | --- |
| D1 | Undeclared variables | Allowed by default; `--strict` requires declarations | Guide calls declaration good practice, and its own examples skip it |
| D2 | Reading an unassigned variable | Runtime error; `--defaults` gives zero values | Catches a common student bug |
| D3 | Keyword case | Upper case enforced, with a did-you-mean error | Guide says keywords are upper case; identifiers are case-insensitive, so `endif` cannot be an identifier either |
| D4 | `10/2/2024` with no spaces | Lexed as a DATE | Guide writes dates as `02/01/2005`; division can always be spaced |
| D5 | `<-` for `←` | Accepted; re-read as `<` `-` where an operator is expected | Students cannot type `←` easily |
| D6 | Line continuation | Newline ignored inside brackets and next to operators, `THEN`, `OF`, `RETURNS` | Supports the guide's continuation lines and older THEN-on-next-line layout |
| D7 | `DIV` / `MOD` with negatives | Truncate toward zero; MOD takes the sign of the left operand | Matches the guide's *integer part* wording and INT's behaviour |
| D8 | Precedence of `&`, `NOT` | `&` below arithmetic; `NOT` below comparison | Matches VB, the closest exam language |
| D9 | `AND` / `OR` | Short-circuit | Makes guarded array loops safe |
| D10 | FOR loop | Bounds evaluated once; loop variable read-only in body; ends holding end + step | Guide is silent; VB behaves the same way |
| D11 | `OUTPUT` of REAL | Always shows a decimal point (`5.0`, `1.0e+20`) | REAL and INTEGER results stay distinguishable; confirmed |
| D12 | INPUT into undeclared name | Type inferred from the text | Keeps exam-style code like `INPUT Move` then `CASE OF Move` working |
| D13 | Random file addresses | 1-based; GETRECORD/PUTRECORD do not move the pointer | Guide's example re-SEEKs before each access |
| D14 | File names | Open-file table matches names case-insensitively | Guide's example opens `StudentFile.Dat` and closes `StudentFile.dat` |
| D15 | PRIVATE and subclasses | Not visible to subclasses | Guide defines only PUBLIC and PRIVATE, no protected level; matches Java, C#, VB and C++; confirmed |
| D16 | Objects | Reference semantics; records and arrays copy | Standard OOP model; guide is silent |
| D17 | Characters | CHAR is one byte; ASC/CHR cover ASCII 0–127; LENGTH counts bytes | Exam questions assume ASCII |
| D18 | `ELSE IF` | No `ELSEIF`; nested IF needs its own ENDIF | Not in the guide |
| D19 | Implicit conversions | Only INTEGER→REAL and CHAR→STRING (4.3); a length-1 STRING never becomes a CHAR | Confirmed; mark schemes are loose here, but one rule is easier to learn |
| D20 | `--defaults` for CHAR and DATE | `' '` (a space) and `01/01/0001` | A space is visible in output, unlike a NUL character; the earliest valid date |
| D21 | Running a DECLARE again | Creates the variable afresh | Array bounds are evaluated when DECLARE runs (5.3) |
| D22 | Type identity | Records, enumerated types, sets and classes are nominal; arrays and pointers are structural (4.2) | `^Counter` must fit a declared pointer type |
| D23 | `ROUND` | Rounds the decimal digits OUTPUT shows | Matches what students work out by hand |
| D24 | Calling procedures | `CALL` is required; only methods may omit it | The guide calls procedures with CALL |
| D25 | Constructors | Not inherited; the implicit constructor takes no arguments | The guide has subclasses call `SUPER.NEW` explicitly |
| D26 | Attribute names in subclasses | A parent's attribute name, even PRIVATE, cannot be reused | Keeps bare names inside methods unambiguous |
| D27 | Random-file storage | Held in memory and written at CLOSEFILE or program end; each record stores its exact type (10.2) | Simple, and lets GETRECORD check types |
| D28 | Files after a runtime error | Closed and saved without W004 | The program never reached its CLOSEFILE |
| D29 | Size limits | Arrays up to 4,000,000 elements; random-file addresses up to 1,000,000; calls limited by a 512 MiB stack | A mistyped bound or address fails clearly instead of using up memory |
| D30 | REAL overflow | R005, like INTEGER overflow | An infinite value has no OUTPUT form |
| D31 | Checks involving unknown types | Made at runtime; the type of an undeclared variable is not guessed before running | Its type depends on which assignment runs first |

### 13.2 Extensions beyond the guide

These are accepted but are not in the guide. A future `--exam-strict` flag could reject them.

| Extension | Section |
| --- | --- |
| `IN` operator for sets | 5.7 |
| Several comma-separated labels on one CASE clause | 3.1 |
| `CASE OF` any expression, not only an identifier | 7.4 |
| `WHILE … DO` | 3.2 |
| `CALL Name` without parentheses; parameterless `PROCEDURE Name` header | 3.2, 8.1 |
| `ARRAY OF Type` parameters with bounds taken from the argument | 8.3 |
| Unicode aliases for quotes, minus and comparison operators | 2.4 |

### 13.3 Known issue in the guide

The guide's `Cat` example declares `PRIVATE Breed : INTEGER` and then assigns it a STRING. Under this spec that is a type error. Section 11 uses `STRING` instead. The test suite SHOULD include the guide's version to confirm the error is reported clearly.

### 13.4 Open items before release

- [x] REAL output format: REALs always print a decimal point (D11).
- [x] A length-1 STRING does not convert to CHAR; there are no implicit conversions beyond 4.3 (D19).
- [x] Subclasses cannot use their parent's PRIVATE attributes (D15).
- [x] Random-file on-disk format fixed as version 1 (10.2).
- [ ] Add `--exam-strict` to reject the extensions in 13.2.

## Sources

- Cambridge International, [*Pseudocode Guide for Teachers*, Computer Science 9618, for exams in 2026, Version 1](https://www.cambridgeinternational.org/Images/697401-2026-syllabus-legacy-notice.pdf), September 2023.
