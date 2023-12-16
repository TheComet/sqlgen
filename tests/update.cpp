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
TEST_F(NAME, update_name_returning_id_returns_negative_if_age_doesnt_exists)
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
TEST_F(NAME, update_name_cb_returns_negative_if_age_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_cb(db, "newname", 420, on_person, &p), Lt(0));
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
TEST_F(NAME, update_age_cb_returns_negative_if_name_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_age_cb(db, "name3", 420, on_person, &p), Lt(0));
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
TEST_F(NAME, update_name_and_age_cb_returns_negative_if_age_doesnt_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.update_name_and_age_cb(db, 3, "newname", 420, on_person, &p), Lt(0));
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
