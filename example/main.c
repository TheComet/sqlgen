#include "mydb.sqlgen.h"
#include <stdio.h>

static int on_pet(const char* name, const char* food, void* user_data)
{
    printf("  %s likes %s\n", name, food);
    return 0;
}

int main(void)
{
    struct mydb* db;
    struct mydb_interface* dbi;

    /* Global init. Must be called once during startup */
    if (mydb_init() != 0)
        goto init_db_failed;
    
    /* Get pointer to interface -- this always succeeds */
    dbi = mydb("sqlite3");

    /* Open database connection */
    db = dbi->open("example.db");
    if (!db)
        goto open_db_failed;

    /* Migrate to latest version if necessary */
    if (dbi->upgrade(db) != 0)
        goto upgrade_db_failed;
    printf("DB version: %d\n", dbi->version(db));

    /* Add a person */
    int person_id = dbi->person.add_or_get(db, "The", "Comet");

    /* Add some pets and associate them with the person */
    int pet_id = dbi->pet.add_or_get(db, "Floofy Fluffballs", "Mouse");
    dbi->person.associate_pet(db, person_id, pet_id);

    pet_id = dbi->pet.add_or_get(db, "Shithead (pronounced shith-ead)", "Mouse");
    dbi->person.associate_pet(db, person_id, pet_id);

    /* Query all pets */
    printf("TheComet's pets:\n");
    dbi->person.get_pet_names(db, "The", "Comet", on_pet, NULL);

    dbi->close(db);
    mydb_deinit();

    return 0;

    upgrade_db_failed : dbi->close(db);
    open_db_failed    : mydb_deinit();
    init_db_failed    : return -1;
}
