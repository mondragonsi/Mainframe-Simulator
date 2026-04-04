# Spec 03: DB2 for z/OS Simulator

## IBM Reference
- IBM DB2 for z/OS Application Programming and SQL Guide (SC27-8845)
- IBM DB2 for z/OS SQL Reference (SC27-8859)

## Goal

Simulate DB2 for z/OS so students can write embedded SQL in simulated COBOL programs, precompile/bind, and execute SQL against in-memory tables. Focus on the student learning experience: correct SQL syntax, SQLCA behavior, and cursor management.

## Scope (Phase 1)

Core SQL subset sufficient for typical batch COBOL + DB2 programs:

### DDL
- `CREATE TABLE` — name, columns (type, nullable, default)
- `CREATE INDEX` — unique/non-unique, on table columns
- Column types: `CHAR(n)`, `VARCHAR(n)`, `INTEGER`, `SMALLINT`, `DECIMAL(p,s)`, `DATE`, `TIME`, `TIMESTAMP`

### DML
- `SELECT ... FROM ... WHERE ... ORDER BY ... FETCH FIRST n ROWS ONLY`
- `SELECT ... INTO :host-var` — singleton select
- `INSERT INTO ... VALUES (...)`
- `UPDATE ... SET ... WHERE ...`
- `DELETE FROM ... WHERE ...`

### Cursors
- `DECLARE cursor CURSOR FOR SELECT ...`
- `OPEN cursor`
- `FETCH cursor INTO :host-vars`
- `CLOSE cursor`

### Host Variables
- Prefixed with `:` in SQL
- Mapped to C variables (or simulated COBOL working storage)
- Null indicators: `:hostvar :indicator`

## SQLCA (SQL Communication Area)

```c
/* src/db2/sqlca.h */
typedef struct {
    char sqlcaid[8];      /* 'SQLCA   ' */
    int  sqlcabc;         /* SQLCA length = 136 */
    int  sqlcode;         /* Return code: 0=OK, >0=warning, <0=error */
    short sqlerrml;       /* Length of sqlerrmc */
    char sqlerrmc[70];    /* Error message tokens */
    char sqlerrp[8];      /* Diagnostic information */
    int  sqlerrd[6];      /* Diagnostic integers: [2]=rows affected */
    char sqlwarn[11];     /* Warning flags */
    char sqlstate[5];     /* SQLSTATE (ANSI standard) */
} SQLCA;
```

Key SQLCODEs to simulate:
- 0: successful
- +100: row not found (FETCH beyond last row, SELECT INTO no rows)
- -811: SELECT INTO returned more than one row
- -803: duplicate key on INSERT (unique index violation)
- -805: package not found (bind error)
- -904: resource unavailable (table locked)
- -922: authorization failure

## Simulated Bind Process

Real DB2 requires precompile → compile → bind. Simulate:
1. "Precompile": extract EXEC SQL statements from simulated COBOL source
2. "Bind": validate SQL against catalog, create a "plan"
3. Execute: run the bound plan

For the simulator, inline execution is acceptable for Phase 1. Document as a deviation.

## Catalog Tables (Minimum)

```
SYSIBM.SYSTABLES   — table definitions
SYSIBM.SYSCOLUMNS  — column definitions  
SYSIBM.SYSINDEXES  — index definitions
```

Students should be able to query the catalog with `SELECT * FROM SYSIBM.SYSTABLES`.

## Data Structures

```c
/* src/db2/db2.h */

#define DB2_MAX_COLS    256
#define DB2_MAX_ROWS    100000
#define DB2_MAX_TABLES  64
#define DB2_NAME_LEN    18

typedef enum {
    DB2_CHAR, DB2_VARCHAR, DB2_INTEGER, DB2_SMALLINT,
    DB2_DECIMAL, DB2_DATE, DB2_TIME, DB2_TIMESTAMP,
} DB2_COLTYPE;

typedef struct {
    char name[DB2_NAME_LEN + 1];
    DB2_COLTYPE type;
    int length;
    int precision;
    int scale;
    bool nullable;
} DB2_COLUMN;

typedef struct {
    char name[DB2_NAME_LEN + 1];
    char creator[DB2_NAME_LEN + 1];
    int col_count;
    DB2_COLUMN columns[DB2_MAX_COLS];
    /* Row storage: dynamic array of fixed-length rows */
    unsigned char *rows;
    int row_count;
    int row_capacity;
    int row_length;     /* Computed from columns */
} DB2_TABLE;

typedef struct {
    DB2_TABLE *tables[DB2_MAX_TABLES];
    int table_count;
    SQLCA sqlca;
} DB2_CONTEXT;
```

## SQL Parser (Minimal)

Not a full SQL parser. Parse the specific patterns students use:
- Tokenizer: keywords, identifiers, literals, operators
- Statement recognizer: SELECT/INSERT/UPDATE/DELETE/DECLARE/OPEN/FETCH/CLOSE
- WHERE clause: simple conditions with AND (no subqueries in Phase 1)
- Bind variables: `:hostname`

Use recursive descent parser. No yacc/bison (no external deps).

## Tests

- `test_db2_create_insert_select`: CREATE TABLE → INSERT → SELECT INTO → verify value
- `test_db2_cursor_fetch_loop`: INSERT 5 rows → OPEN cursor → FETCH 5 times → verify +100 on 6th
- `test_db2_sqlcode_803`: INSERT duplicate key → verify SQLCODE = -803
- `test_db2_sqlcode_100`: SELECT INTO with no matching rows → verify SQLCODE = +100
- `test_db2_update_where`: UPDATE SET with WHERE → verify sqlerrd[2] = rows affected
- `test_db2_catalog_query`: SELECT from SYSIBM.SYSTABLES → verify created table appears
