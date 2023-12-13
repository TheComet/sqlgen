#include <gmock/gmock.h>
#include "sqlgen/tests/insert.h"

#define NAME sqlgen_insert

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        insert_init();
        dbi = insert("sqlite3");
        db = dbi->open("insert.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        insert_deinit();
    }

    struct insert_interface* dbi;
    struct insert* db;
};

TEST_F(NAME, insert_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.insert_or_ignore(db, "name3", 420), Lt(0));
}
TEST_F(NAME, insert_or_ignore_returns_0_if_it_exists)
{
    ASSERT_THAT(dbi->valid.insert_or_ignore(db, "name2", 420), Eq(0));
}
TEST_F(NAME, insert_or_ignore_returns_0_if_it_doesnt_exist)
{
    ASSERT_THAT(dbi->valid.insert_or_ignore(db, "name3", 420), Eq(0));
}

TEST_F(NAME, insert_or_get_id_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.insert_or_get_id(db, "name2", 420), Lt(0));
}
TEST_F(NAME, insert_or_get_id_exists)
{
    ASSERT_THAT(dbi->valid.insert_or_get_id(db, "name2", 420), Eq(2));
}
TEST_F(NAME, insert_or_get_id_doesnt_exist)
{
    ASSERT_THAT(dbi->valid.insert_or_get_id(db, "name3", 420), Eq(3));
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
TEST_F(NAME, insert_or_get_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.insert_or_get_cb(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, insert_or_get_cb_returns_value_in_cb_function_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_cb(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
}
TEST_F(NAME, insert_or_get_cb_returns_value_in_cb_function_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_cb(db, "name3", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(3));
    ASSERT_THAT(p.name, Eq("name3"));
}
TEST_F(NAME, insert_or_get_cb_doesnt_alter_age_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_cb(db, "name1", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(69));

    ASSERT_THAT(dbi->valid.insert_or_get_cb(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(42));
}
TEST_F(NAME, insert_or_get_cb_sets_age_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_cb(db, "name3", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(420));
}

TEST_F(NAME, insert_or_get_id_and_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.insert_or_get_id_and_cb(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, insert_or_get_id_and_cb_returns_id_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name1", 420, on_person, &p), Eq(1));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("name1"));

    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
}
TEST_F(NAME, insert_or_get_id_and_cb_returns_new_id_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name3", 420, on_person, &p), Eq(3));
    ASSERT_THAT(p.id, Eq(3));
    ASSERT_THAT(p.name, Eq("name3"));
}
TEST_F(NAME, insert_or_get_id_and_cb_doesnt_alter_age_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name1", 420, on_person, &p), Eq(1));
    ASSERT_THAT(p.age, Eq(69));

    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(42));
}
TEST_F(NAME, insert_or_get_id_and_cb_sets_age_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.insert_or_get_id_and_cb(db, "name3", 420, on_person, &p), Eq(3));
    ASSERT_THAT(p.age, Eq(420));
}
