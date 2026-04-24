# mycc

A small multi-pass compiler project for Dr. Burris's translator semester project.

## Pipeline

1. **Scanner / pass 1 DFSA**
   - Reads the source one character at a time.
   - Uses a table-driven DFSA.
   - Writes `tokens.txt`.

2. **Symbol-table pass / pass 2 DFSA**
   - Reads token/class pairs from `tokens.txt`.
   - Uses a second table-driven pass.
   - Writes `symbols.txt`.

3. **Parser / quad generation**
   - Builds quads from the token stream and symbol table.
   - Uses a precedence table for expressions and a stack-driven block/statement parser.
   - Writes `quads.txt` and `symbols_after_parse.txt`.

4. **Code generation**
   - Lowers quads to NASM/Linux-style x86 assembly.
   - Writes `program.asm` and `code_numeric.txt`.

5. **Listings**
   - Writes `source_listing.txt` and `code_listing.txt`.

## Build

```sh
make run
```

The compiler executable is:

```sh
build/mycc.exe
```

## Running a demo

The main program reads:

```text
tests/inputs/test_code.txt
```

To run one of the included demos, copy it over `test_code.txt` and run the compiler.

Generated files are written into:

```text
tests/outputs/
```

## Included demo set

- `01_formula_demo.txt` - arithmetic / CONST / VAR / integer I/O
- `02_largest_of_two_demo.txt` - IF / ELSE / relational operators
- `03_factorial_while_demo.txt` - WHILE
- `04_largest_of_three_nested_if_demo.txt` - nested IF
- `05_recursive_factorial_procedure_demo.txt` - procedure call + recursion
- `06_array_sum_demo.txt` - subscripted variables / arrays
