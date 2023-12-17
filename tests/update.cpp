#include <gmock/gmock.h>
#include "sqlgen/tests/update.h"

#define NAME sqlgen_update

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        update_init();
        dbi = update("sqlite3");
        db = dbi->open("update.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        update_deinit();
    }

    struct update_interface* dbi;
    struct update* db;
};

TEST_F(NAME, update_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.update_age(db, "name2", 420), Lt(0));
}
TEST_F(NAME, update_returns_0_if_name_exists)
{
    ASSERT_THAT(dbi->valid.update_age(db, "name2", 420), Eq(0));
}
TEST_F(NAME, update_returns_0_if_name_doesnt_exist)
{
    ASSERT_THAT(dbi->valid.update_age(db, "name3", 420), Eq(0));
}

TEST_F(NAME, update_name_returning_id_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.update_name_returning_id(db, "newname1", 69), Lt(0));
}
TEST_F(NAME, update_name_returning_id_returns_id_if_age_exists)
{
    ASSERT_THAT(dbi->valid.update_name_returning_id(db, "newname1", 69), Eq(1));
    ASSERT_THAT(dbi->valid.update_name_returning_id(db, "newname2", 42), Eq(2));
}
TEST_F(NAME, update_name_returning_id_returns_negative_if_name_doesnt_exists)
{
    ASSERT_THAT(dbi->valid.update_name_returning_id(db, "newname", 420), Lt(0));
}
TEST_F(NAME, update_age_returning_id_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.update_age_returning_id(db, "name2", 420), Lt(0));
}
TEST_F(NAME, update_age_returning_id_returns_id_if_name_exists)
{
    ASSERT_THAT(dbi->valid.update_age_returning_id(db, "name1", 420), Eq(1));
    ASSERT_THAT(dbi->valid.update_age_returning_id(db, "name2", 420), Eq(2));
}
TEST_F(NAME, update_age_returning_id_returns_negative_if_name_doesnt_exists)
{
    ASSERT_THAT(dbi->valid.update_age_returning_id(db, "name3", 420), Lt(0));
}

struct person {
    int id;
    std::string name;
    int age;
};
static int on_person(int id, const char* name, int age, void* user) {
    ((person*)user)->id = id;
    ((person*)user)->name = name;
    ((person*)user)->age = age;
    return 2;
}

TEST_F(NAME, update_name_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.update_name_cb(db, "newname1", 69, on_person, &p), Lt(0));
}
TEST_F(NAME, update_name_cb_returns_value_in_cb_function_if_age_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_cb(db, "newname1", 69, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("newname1"));
    ASSERT_THAT(p.age, Eq(69));

    ASSERT_THAT(dbi->valid.update_name_cb(db, "newname2", 42, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("newname2"));
    ASSERT_THAT(p.age, Eq(42));
}
TEST_F(NAME, update_name_cb_returns_0_if_age_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_cb(db, "newname", 420, on_person, &p), Eq(0));
}
TEST_F(NAME, update_age_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.update_age_cb(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, update_age_cb_returns_value_in_cb_function_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_age_cb(db, "name1", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("name1"));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->valid.update_age_cb(db, "name2", 430, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
    ASSERT_THAT(p.age, Eq(430));
}
TEST_F(NAME, update_age_cb_returns_0_if_name_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_age_cb(db, "name3", 420, on_person, &p), Eq(0));
}
TEST_F(NAME, update_name_and_age_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.update_name_and_age_cb(db, 1, "newname1", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, update_name_and_age_cb_returns_value_in_cb_function_if_age_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_and_age_cb(db, 1, "newname1", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("newname1"));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->valid.update_name_and_age_cb(db, 2, "newname2", 430, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("newname2"));
    ASSERT_THAT(p.age, Eq(430));
}
TEST_F(NAME, update_name_and_age_cb_returns_0_if_age_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_and_age_cb(db, 3, "newname", 420, on_person, &p), Eq(0));
}

TEST_F(NAME, update_name_cb_returning_id_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.update_name_cb_returning_id(db, "newname1", 69, on_person, &p), Lt(0));
}
TEST_F(NAME, update_name_cb_returning_id_returns_id_if_age_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_cb_returning_id(db, "newname1", 69, on_person, &p), Eq(1));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("newname1"));
    ASSERT_THAT(p.age, Eq(69));

    ASSERT_THAT(dbi->valid.update_name_cb_returning_id(db, "newname2", 42, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("newname2"));
    ASSERT_THAT(p.age, Eq(42));
}
TEST_F(NAME, update_name_cb_returning_id_returns_negative_if_age_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_cb_returning_id(db, "newname", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, update_age_cb_returning_id_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.update_age_cb_returning_id(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, update_age_cb_returning_id_returns_id_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_age_cb_returning_id(db, "name1", 420, on_person, &p), Eq(1));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("name1"));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->valid.update_age_cb_returning_id(db, "name2", 430, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
    ASSERT_THAT(p.age, Eq(430));
}
TEST_F(NAME, update_age_cb_returning_id_returns_negative_if_name_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_age_cb_returning_id(db, "name3", 420, on_person, &p), Lt(0));
}

static int check_rows_1(int id, const char* name, int age, void* user)
{
    switch (id)
    {
        case 1:
            if (strcmp(name, "name1")) return -1;
            if (age != 69) return -1;
            return 0;
        case 2:
            if (strcmp(name, "name2")) return -1;
            if (age != 42) return -1;
            return 0;
    }
    return -1;
}
TEST_F(NAME, update_all_with_unique_constraint_violation_doesnt_change_any_rows)
{
    ASSERT_THAT(dbi->valid.update_all(db, "newname", 420), Eq(-1));
    ASSERT_THAT(dbi->valid.select_all(db, check_rows_1, NULL), Eq(0));
}

static int on_person_one(int id, const char* name, int age, void* counter)
{
    (*(int*)counter)++;

    if (id != *(int*)counter) return -1;
    if (age != 420) return -1;

    return 1;
}
static int check_rows_2(int id, const char* name, int age, void* user)
{
    switch (id)
    {
        case 1:
            if (strcmp(name, "name1")) return -1;
            if (age != 420) return -1;
            return 0;
        case 2:
            if (strcmp(name, "name2")) return -1;
            if (age != 420) return -1;
            return 0;
    }
    return -1;
}
TEST_F(NAME, update_all_ages_cb_iterate_one)
{
    int counter = 0;
    ASSERT_THAT(dbi->valid.update_all_ages_cb(db, 420, on_person_one, &counter), Eq(1));
    ASSERT_THAT(counter, Eq(1));
    ASSERT_THAT(dbi->valid.select_all(db, check_rows_2, NULL), Eq(0));
}

static int on_person_all(int id, const char* name, int age, void* counter)
{
    (*(int*)counter)++;

    if (id != *(int*)counter) return -1;
    if (age != 420) return -1;

    return 0;
}
TEST_F(NAME, update_all_ages_cb_iterate_all)
{
    int counter = 0;
    ASSERT_THAT(dbi->valid.update_all_ages_cb(db, 420, on_person_all, &counter), Eq(0));
    ASSERT_THAT(counter, Eq(2));
    ASSERT_THAT(dbi->valid.select_all(db, check_rows_2, NULL), Eq(0));
}
