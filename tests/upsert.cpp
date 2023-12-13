#include <gmock/gmock.h>
#include "sqlgen/tests/upsert.h"

#define NAME sqlgen_upsert

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        upsert_init();
        dbi = upsert("sqlite3");
        db = dbi->open("upsert.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        upsert_deinit();
    }

    struct upsert_interface* dbi;
    struct upsert* db;
};

TEST_F(NAME, upsert_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.upsert(db, "name3", 420), Lt(0));
}
TEST_F(NAME, upsert_returns_0_if_it_exists)
{
    ASSERT_THAT(dbi->valid.upsert(db, "name2", 420), Eq(0));
}
TEST_F(NAME, upsert_returns_0_if_it_doesnt_exist)
{
    ASSERT_THAT(dbi->valid.upsert(db, "name3", 420), Eq(0));
}

TEST_F(NAME, upsert_id_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.upsert_returning_id(db, "name2", 420), Lt(0));
}
TEST_F(NAME, upsert_id_exists)
{
    ASSERT_THAT(dbi->valid.upsert_returning_id(db, "name2", 420), Eq(2));
}
TEST_F(NAME, upsert_id_doesnt_exist)
{
    ASSERT_THAT(dbi->valid.upsert_returning_id(db, "name3", 420), Eq(3));
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
TEST_F(NAME, upsert_cb_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.upsert_cb(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, upsert_cb_returns_value_in_cb_function_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
}
TEST_F(NAME, upsert_cb_returns_value_in_cb_function_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb(db, "name3", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(3));
    ASSERT_THAT(p.name, Eq("name3"));
}
TEST_F(NAME, upsert_cb_sets_age_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb(db, "name1", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->valid.upsert_cb(db, "name2", 421, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(421));
}
TEST_F(NAME, upsert_cb_sets_age_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb(db, "name3", 422, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(422));
}

TEST_F(NAME, upsert_cb_returning_id_returns_negative_on_error)
{
    struct person p;
    ASSERT_THAT(dbi->invalid.upsert_cb_returning_id(db, "name2", 420, on_person, &p), Lt(0));
}
TEST_F(NAME, upsert_cb_returning_id_returns_id_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name1", 420, on_person, &p), Eq(1));
    ASSERT_THAT(p.id, Eq(1));
    ASSERT_THAT(p.name, Eq("name1"));

    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name2", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.id, Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
}
TEST_F(NAME, upsert_cb_returning_id_returns_new_id_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name3", 420, on_person, &p), Eq(3));
    ASSERT_THAT(p.id, Eq(3));
    ASSERT_THAT(p.name, Eq("name3"));
}
TEST_F(NAME, upsert_cb_returning_id_sets_age_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name1", 420, on_person, &p), Eq(1));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name2", 421, on_person, &p), Eq(2));
    ASSERT_THAT(p.age, Eq(421));
}
TEST_F(NAME, upsert_cb_returning_id_sets_age_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->valid.upsert_cb_returning_id(db, "name3", 422, on_person, &p), Eq(3));
    ASSERT_THAT(p.age, Eq(422));
}

TEST_F(NAME, insert_or_set_age_alters_age_if_name_exists)
{
    struct person p;
    ASSERT_THAT(dbi->insert_or_set_age(db, "name1", 420, on_person, &p), Eq(2));
    ASSERT_THAT(p.name, Eq("name1"));
    ASSERT_THAT(p.age, Eq(420));

    ASSERT_THAT(dbi->insert_or_set_age(db, "name2", 421, on_person, &p), Eq(2));
    ASSERT_THAT(p.name, Eq("name2"));
    ASSERT_THAT(p.age, Eq(421));
}
TEST_F(NAME, insert_or_set_age_inserts_person_if_name_doesnt_exist)
{
    struct person p;
    ASSERT_THAT(dbi->insert_or_set_age(db, "name3", 422, on_person, &p), Eq(2));
    ASSERT_THAT(p.name, Eq("name3"));
    ASSERT_THAT(p.age, Eq(422));
}
