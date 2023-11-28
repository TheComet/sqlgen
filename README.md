# SQL Code Generator

Generates sqlite3 C bindings from SQL statements.

## Building and Basic Usage

sqlgen consists of a single C89 compliant source file. All you have to do is
compile it:
```sh
gcc -o sqlgen sqlgen/sqlgen.c
```

You can now generate header/source file pairs for a particular backend with:
```sh
./sqlgen -b sqlite3 -i mydb.sqlgen --header mydb.h --source mydb.c
```

Alternatively, if you are using CMake in your project, you can also add this
directory as a subdirectory to your main project:
```cmake
add_subdirectory ("sqlgen")
```

This gives you access to a CMake macro:
```cmake
sqlgen_target (mydb
    INPUT mydb.sqlgen
    BACKENDS sqlite3)

add_executable (my_project
    "main.c"
    "${SQLGEN_mydb_OUTPUTS}")
```

## Minimalist example

The header and source files are generated from a definition file, here, called
```mydb.sqlgen```. The most bare-bones definition file possible looks like the
following:
```c
%option prefix="mydb"

%source-includes {
    #include "sqlite3.h"
    #include "mydb.sqlgen.h"
}
```

You can run this through ```sqlgen``` and inspect the header/source files to
see the basic structure.

There are 3 public functions generated, along with a structure containing the
query interface: 
```c
struct mydb;
struct mydb_interface
{
    struct mydb* (*open)(const char* uri);
    void (*close)(struct mydb* ctx);
    int (*version)(struct mydb* ctx);
    int (*upgrade)(struct mydb* ctx);
    int (*reinit)(struct mydb* ctx);
    int (*migrate_to)(struct mydb* ctx, int target_version);
};

int mydb_init(void);
void mydb_deinit(void);
struct mydb_interface* mydb(const char* backend);
```

You will notice that all symbols begin with "mydb". This is set using the option
```prefix="mydb"```. This option is optional, but it is recommended to give
the database a custom name in case you end up having multiple databases. If not
specified, the prefix will default to "sqlgen".

The most basic program, thus, looks like this:
```c
#include "mydb.sqlgen.h"
int main() {
    mydb_init();  /* Global init */
    struct mydb_interface* dbi = mydb("sqlite3");
    struct mydb* db = dbi->open("example.db");
    
    /* do queries here */
    
    dbi->close(db);
    mydb_deinit();
    return 0;
}
```
## Migrations

The first step for any database is creating its structure. ```sqlgen``` has
two directives for managing migrations: ```%upgrade``` and ```%downgrade```:


```c
%option prefix="mydb"

%upgrade 1 {
    CREATE TABLE IF NOT EXISTS people (
        id INTEGER PRIMARY KEY NOT NULL,
        first_name TEXT NOT NULL,
        last_name TEXT NOT NULL,
        UNIQUE(first_name, LAST_NAME));
}
%downgrade 0 {
    DROP TABLE IF EXISTS people;
}

%source-includes {
    #include "sqlite3.h"
    #include "mydb.sqlgen.h"
}
```

You will notice the number following```%upgrade 1 { ... }``` and ```%downgrade 0 { ... }```.
These numbers specify the version you are upgrading/downgrading to. A fresh, empty database
starts at version 0, so the initial migration is an upgrade to version 1. Conversely,
to undo this upgrade, we add the opposite statements to downgrade back to version 0.

How does this look in code? Well, there are a few options:

```c
#include "mydb.sqlgen.h"
int main() {
    ...
    dbi->upgrade(db);        /* Runs all migrations up to the most recent version */
    dbi->migrate_to(db, 1);  /* Specifically migrates to version 1 */
    dbi->reinit(db);         /* Downgrades to 0, then upgrades to most recent version */
    ...
}
```

You likely want to be using ```dbi->upgrade(db)``` in most cases. This will
first check the current version of the database, and then apply all ```%upgrade```
directives that have a newer version than the current version, in order. If
the database is already up-to-date then ```dbi->upgrade(db)``` is a no-op.

During development,  it is often useful to re-initialize the database because
you'll be making tweaks to the schema iteratively. You might consider doing
something like the following to make things easier:
```c
if (argv_contains(argc, argv, "--reinit-db"))
   dbi->reinit(db);
else
   dbi->upgrade(db);
```

And finally, ```dbi->migrate_to(db, target_version)``` is a combination of both. If
the current version is higher than the target version, then all ```%downgrade```
directives down to the target version will be executed. If the current version
is lower than the target version, then all ```%upgrade``` directives up to the
target version will be executed.

## Adding Queries

Let's go back to our definition file and add some more queries:

```c
%option prefix="mydb"
//%option debug-layer

%upgrade 1 {
    CREATE TABLE IF NOT EXISTS people (
        id INTEGER PRIMARY KEY NOT NULL,
        first_name TEXT NOT NULL,
        last_name TEXT NOT NULL,
        UNIQUE(first_name, LAST_NAME));
}
%upgrade 2 {
    CREATE TABLE pets (
        id INTEGER PRIMARY KEY NOT NULL,
        name TEXT NOT NULL,
        fave_food TEXT NOT NULL,
        UNIQUE(name));
    CREATE TABLE owned_pets (
        person_id INTEGER NOT NULL,
        pet_id INTEGER NOT NULL,
        UNIQUE(person_id, pet_id));
}
%downgrade 1 {
    DROP TABLE IF EXISTS owned_pets;
    DROP TABLE IF EXISTS pets;
}
%downgrade 0 {
    DROP TABLE IF EXISTS people;
}

%query person,add_or_get(const char* first_name, const char* last_name) {
    type insert
    table people
    return id
}
%query person,associate_pet(int person_id, int pet_id) {
    type insert
    table owned_pets
}
%query pet,add_or_get(const char* name, const char* fave_food) {
    type insert
    table pets
    return id
}
%query person,get_pet_names(const char* first_name, const char* last_name) {
    type select-all
    stmt {
        SELECT name, fave_food FROM people
        JOIN owned_pets ON owned_pets.person_id=people.id
        JOIN pets ON owned_pets.pet_id=pets.id
        WHERE first_name=? AND last_name=?
    }
    callback const char* name, const char* fave_food
}

%source-includes {
    #include "sqlite3.h"
    #include "mydb.sqlgen.h"
}
```

In the main program, we can now add some people/pets and then query them:
```c
#include <stdio.h>
#include "mydb.sqlgen.h"

static int on_pet(const char* name, const char* food, void* user_data)
{
    printf("  %s likes %s\n", name, food);
    return 0;
}

int main()
{
    mydb_init();
    struct mydb_interface* dbi = mydb("sqlite3");
    struct mydb* db = dbi->open("test.db");
    dbi->upgrade(db);
    printf("DB version: %d\n", dbi->version(db));

    int person_id = dbi->person.add_or_get(db, "The", "Comet");
    int pet_id = dbi->pet.add_or_get(db, "Floofy Fluffballs", "Mouse");
    dbi->person.associate_pet(db, person_id, pet_id);
    
    pet_id = dbi->pet.add_or_get(db, "Shithead (pronounced shith-ead)", "Mouse");
    dbi->person.associate_pet(db, person_id, pet_id);

    printf("TheComet's pets:\n");
    dbi->person.get_pet_names(db, "The", "Comet", on_pet, NULL);

    dbi->close(db);
    mydb_deinit();
    return 0;
}
```

Running this program will print:
```
DB version: 2
TheComet's pets:
  Floofy Fluffballs likes Mouse
  Shithead (pronounced shith-ead) likes Mouse
```

## Debug Layer

By passing the option ```./sqlgen --debug-layer``` or by adding ```%option debug-layer``` 
to the definition file, we can tell the code generator to wrap each function in the
interface and print out all of the parameters and results:
```c
%option prefix="mydb"
%option debug-layer

...
```

The output now looks much more verbose:
```
Opening database "example.db"
retval=0000022A711737D0
Upgrading db...
retval=0
Getting version...
retval=2
DB version: 2
db_sqlite3.person.add_or_get("The", "Comet")
retval=1
INSERT INTO people (first_name, last_name) VALUES ('The', 'Comet') ON CONFLICT DO UPDATE SET first_name=excluded.first_name RETURNING id;

db_sqlite3.pet.add_or_get("Floofy Fluffballs", "Mouse")
retval=1
INSERT INTO pets (name, fave_food) VALUES ('Floofy Fluffballs', 'Mouse') ON CONFLICT DO UPDATE SET name=excluded.name RETURNING id;

db_sqlite3.person.associate_pet(1, 1)
retval=0
INSERT OR IGNORE INTO owned_pets (person_id, pet_id) VALUES (1, 1);

db_sqlite3.pet.add_or_get("Shithead (pronounced shith-ead)", "Mouse")
retval=2
INSERT INTO pets (name, fave_food) VALUES ('Shithead (pronounced shith-ead)', 'Mouse') ON CONFLICT DO UPDATE SET name=excluded.name RETURNING id;

db_sqlite3.person.associate_pet(1, 2)
retval=0
INSERT OR IGNORE INTO owned_pets (person_id, pet_id) VALUES (1, 2);

TheComet's pets:
db_sqlite3.person.get_pet_names("The", "Comet")
  name | fave_food
  "Floofy Fluffballs" | "Mouse"
  Floofy Fluffballs likes Mouse
  "Shithead (pronounced shith-ead)" | "Mouse"
  Shithead (pronounced shith-ead) likes Mouse
retval=0
SELECT name, fave_food FROM people JOIN owned_pets ON owned_pets.person_id=people.id JOIN pets ON owned_pets.pet_id=pets.id WHERE first_name='The' AND last_name='Comet'

Closing database
```

## More Details on Queries

A query statement must always contain at least the ```type``` and either a ```table```
name, or a SQL ```stmt``` if your query is complex.
```c
%query example(int a, int b, const char* c) {
    type insert
    table example
}
```
This generates the statement ```INSERT OR IGNORE INTO example(a, b, c) VALUES (a, b, c);```. Notice
that the order of the function arguments to ```example``` matters, as well as their
types. The type information is used to correctly bind the function arguments to the
SQL statement.

### Returning data from the db

There are 2 ways to return data out of these functions. The first method uses the
```return``` value of the function itself. This is convenient, but limited, since
all interface functions are declared with ```int``` as their return types, meaning,
you cannot return strings, or uint64_t, or any other type that cannot be cast to int.
```c
%query example(int a, int b, const char* c) {
    type insert
    table example
    return id
}
```
This is the most common case, and generates the SQL statement
```INSERT INTO example(a, b, c) VALUES (a, b, c) ON CONFLICT DO UPDATE SET a=excluded.a RETURNING id;``` If
you are fairly new to SQL you might be wondering "what the heck is this", and the short answer
is don't ask. In the case of a value already existing, if we hadn't written ```ON CONFLICT DO UPDATE SET a=excluded.a```,
then the ```RETURNING id``` would actually not execute, and the statement would not return anything.
By re-inserting an already existing value back into the table, we trigger the statement
to always return a value, regardless of whether it was inserted or not.

If you want to return more complex types, and especially if you want to return multiple
columns, you will have to use callback functions. You can specify which values you
want to have returned with the ```callback``` statement:
```c
%query example(int a, int b, const char* c) {
    type insert
    table example
    callback int a, const char* c
}
```
The callback statement defines the C function's declaration. So in this case, the C callback function
will have the signature ```int my_callback(int a, const char* c, void* user_ptr);```

Every callback always takes a ```void*``` as its last parameter for passing on custom
data. This pointer is passed in to the generated query function ```dbi->example(a, b, my_callback, user_ptr);```

```c
static int on_row(int a, const char* c, void* user_ptr) {
    return 1;  /* Indicates that you wish to stop iterating over rows */
    return 0;  /* Indicates that you wish to continue iterating over rows */
    return -1; /* Indicates an error. Iteration will stop */
}

/* Returns the same value that "on_row()" returns, which means we can error check */
if (dbi->example(db, 1, 2, "test", on_row, NULL) < 0)
    error();
```

### Query Types

The ```type``` statement roughly describes the type of query you wish to perform, and
defines how the generated C function behaves. 
These are:
```c
%query example() {
    type insert | upsert | update | delete | exists | select-single | select-all
}
```
```insert``` will generate either a "insert or get" operation (if you also specify a "return"
or "callback" statement, or a "insert or ignore" operation. The C function
will return 0 on success (or return the value, if you specified "return"),
-1 on failure. 
```
%query example() {
    type insert
    table example
    // return ...
    // callback ...
}
```
```upsert``` will generate a "insert or update" operation. This is different from a
"insert or get" operation, because in the case of the value already existing,
the existing value is modified and returned, whereas with the "insert or get"
operation, the existing value is returned un-modified.
```
%query example() {
    type upsert
    table example
    // return ...
    // callback ...
}
```
```update``` will generate a "UPDATE ... SET ... WHERE ..." operation. Because it is unclear
which columns need to be updated and which columns are the conditions for the
search, you must provide a list of column names to update. All remaining parameters
in the function's list are assumed to be the WHERE condition.

In this example, this will produce ```UPDATE example SET col1=?, col=? WHERE col3=? AND col4=?;```
```
%query example(int col1, int col2, int col3, int col4) {
    type update col1, col2
    table example
    // return ...
    // callback ...
}
```
```delete``` will generate a ```DELETE FROM ... WHERE ...``` statement. Values can
be returned if necessary.
```
%query example(int col1, int col2) {
    type delete
    table example
    // return ...
    // callback ...
}
```
```exists``` will check if 1 or more rows matching the criteria exist. This query type
does not support returning values. The C function will return 1 if it exists, 0 if it does
not exist, and -1 if an error occurred.
```
%query example(int col1, int col2) {
    type exists
    table example
}
```
```select-first``` will return the first row it finds. Note that you may want to
order the rows if you expect more than one to match the criteria. This can be achieved
by specifying a custom ```stmt```
```
%query example(int col1, int col2) {
    type select-first
    table example
    callback int col3, int col4
}
```
```
%query example(int col1, int col2) {
    type select-first
    stmt { SELECT col3, col4 FROM example ORDER BY id LIMIT 1; }
    callback int col3, int col4
}
```
```select-all``` will return the all rows matching the criteria.
```
%query example(int col1, int col2) {
    type select-all
    table example
    callback int col3, int col4
}
```

### Custom statements

In all of the above examples, one can replace ```table``` with ```stmt``` and achieve
the same result. The statement can span multiple lines, and is copied verbatim into
the call to ```sqlite3_prepare_v2()```.

One must pay attention to the order of arguments in the C function as well as the
order of arguments in the ```callback``` when writing custom  statements.

Function arguments are bound in the order they appear in the function signature,
meaning, the order of ```WHERE col1=? AND col2=?``` must be the same as the function
signature.

The order of the columns in the ```SELECT``` statement must match the order of the
arguments to ```callback```.

```c
%query example(int col1, int col2) {
    type select-all
    stmt {
        SELECT name, occupation, salary
        FROM people
        JOIN occupations ON people.occupation_id = occupations.id
        WHERE col1 = ? AND col2 = ?
        ORDER BY salary;
    }
    callback const char* name, const char* occupation, int salary
}
```

Sometimes, you might run into a situation where you need to bind the same
parameter twice, or change the bind order. For this, you can use the ```bind```
statement to specify the order:
```c
%query example(uint64_t time_stamp) {
    type select-all
    stmt {
        SELECT name, birth_date, death_date
        FROM people
        WHERE birth_date >= ? AND death_date <= ?;
    }
    bind time_stamp, time_stamp
    callback const char* name, uint64_t birth_date, uint64_t death_date
}
```

### NULL parameters

If you are passing values in to the db that may be null, you can mark those in
the ```%query``` statement with ```null```:
```c
%query example(const char* name null, int age null) {
    ...
}
```
This will generate the following binding code:
```c
if ((ret = name == NULL ? sqlite3_bind_null(ctx->test, 1) : sqlite3_bind_text(ctx->test, 1, name, -1, SQLITE_STATIC)) != SQLITE_OK ||
    (ret = age < 0 ? sqlite3_bind_null(ctx->test, 2) : sqlite3_bind_int(ctx->test, 2, age)) != SQLITE_OK)
{
    sqlgen_error(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));
    return -1;
}
```

In other words, by passing in a negative value for ```age```, or by passing in
NULL for ```name```, the database will bind NULL in those cases.

When returning NULL values out of the db, you should also qualify those columns
in the ```callback``` statement with ```null```:
```c
%query example(...) {
    type select-all
    table people
    callback const char* name null, int age null
}
```

This will generate the following callback code:
```c
on_row(
    sqlite3_column_type(ctx->example_get, 0) == SQLITE_NULL ? NULL : (const char*)sqlite3_column_text(ctx->example_get, 0),
    sqlite3_column_type(ctx->example_get, 1) == SQLITE_NULL ? -1 : sqlite3_column_int(ctx->example_get, 1),
    user_ptr);
```
In other words, if the value in the db's column is NULL, in your callback, you will either
receive NULL if the value is a string, or -1 if the value is an integer. When dealing with unsigned
types, you will receive ```(uint64_t)-1```, ```(uint32_t)-1```, etc.

### Query Groups and Global Queries

The query ```%query example() {}``` is called through the interface via ```dbi->example(db);```

The query ```%query group,example() {}``` is called through the interface via ```dbi->group.example(db);```

This syntax lets you group relevant queries together into sub-structures in the interface and
makes code completion more friendly.

## Functions

Aside from ```%query``` there is also a concept of a "function":
```c
%function example(const char* a) {
    /* Arbitrary C code here */
    return 0;
}
```
The return type is fixed to ```int``` (for now). This creates an entry in the interface structure,
letting you call ```dbi->example(...);```. Unlike ```%query```, ```%function``` lets
you run arbitrary C code within the generated code's translation unit.

This is useful for situations where you may want to migrate the database, or add additional
utility functions for interacting with sqlite3.

## Redirecting output

The default function for handling SQL error messages prints to ```stdout``` and has
the following signature:
```c
static void
sqlgen_error(int error_code, const char* error_code_str, const char* error_msg);
```
You can choose to implement this function yourself:
```c
%option log-sql-err="my_sql_error_handler"
%source-preamble {
    void
    my_sql_error_handler(int error_code, const char* error_code_str, const char* error_msg)
    {
        log_err("%s (%d): %s\n", error_code_str, error_code, error_msg);
    }
}
```
The debug layer prints to ```stdout``` by default as well. You can choose to implement
your own handler for this as well:
```c
%option log-dbg="my_dbg_printf"
%source-preamble {
    void
    my_dbg_printf(const char* fmt, ...)
    {
        va_list va;
        va_start(va, fmt);
        vfprintf(fmt, va);
        va_end(va);
    }
}
```

## Overriding malloc/free

There is exactly one location where ```malloc()``` and ```free()``` get called in the interface,
namely, in ```dbi->open()``` and ```dbi->close()```. Nevertheless, you can choose to
replace these functions with your own by setting:
```c
%option malloc="my_malloc"
%option free="my_free"
```

If you wish to override SQLite3's memory allocator, then you will have to provide
your own ```init()``` and ```deinit()``` functions, and provide SQLite3 with the
appropriate structure.
```c
%option prefix="mydb"
%option custom-init
%option custom-deinit
%header-postamble {
    int mydb_init(void);
    void mydb_deinit(void);
}
%source-postamble {
    static struct sqlite3_mem_methods dbgmem = {
        dbgmem_malloc,
        dbgmem_free,
        dbgmem_realloc,
        dbgmem_allocated_size,
        dbgmem_round_up,
        dbgmem_init,
        dbgmem_deinit,
        NULL
    };
    
    int mydb_init(void) {
        sqlite3_config(SQLITE_CONFIG_MALLOC, &dbgmem);
        
        if (sqlite3_initialize() != SQLITE_OK)
            return -1;
        return 0;
    }
    
    /* Not really required but it's here for completion sake */
    void mydb_deinit(void) {
        sqlite3_shutdown();
    }
}
```
