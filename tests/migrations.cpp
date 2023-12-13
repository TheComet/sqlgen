#include <gmock/gmock.h>
#include "sqlgen/tests/migrations.h"

#define NAME sqlgen_migrations

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        migrations_init();
        dbi = migrations("sqlite3");
        db = dbi->open("migrations.db");
        dbi->migrate_to(db, 0);
    }

    void TearDown() override {
        dbi->close(db);
        migrations_deinit();
    }

    struct migrations_interface* dbi;
    struct migrations* db;
};

TEST_F(NAME, migrate_0_1_0)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, 0), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
}
TEST_F(NAME, migrate_0_2_0)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 2), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(2));
    ASSERT_THAT(dbi->migrate_to(db, 0), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
}
TEST_F(NAME, migrate_0_2_1_0)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 2), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(2));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, 0), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
}
TEST_F(NAME, migrate_0_1_2_0)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, 2), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(2));
    ASSERT_THAT(dbi->migrate_to(db, 0), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
}
TEST_F(NAME, migrate_negative)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, -1), Lt(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, -1), Lt(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, -5), Lt(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
}
TEST_F(NAME, migrate_nonexisting_version)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 3), Lt(0));
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
    ASSERT_THAT(dbi->migrate_to(db, 3), Lt(0));
    ASSERT_THAT(dbi->version(db), Eq(1));
}
static int on_person_get(const char* name, void* user)
{
    *(std::string*)user = name;
    return 0;
}
TEST_F(NAME, migrate_0_1_0_with_data)
{
    ASSERT_THAT(dbi->version(db), Eq(0));
    ASSERT_THAT(dbi->migrate_to(db, 1), Eq(0));

    int id1 = dbi->v1.person_add(db, "name1");
    int id2 = dbi->v1.person_add(db, "name2");
    ASSERT_THAT(id1, Gt(0));
    ASSERT_THAT(id2, Gt(0));

    std::string name1, name2, name3;
    ASSERT_THAT(dbi->v1.person_get(db, id1, on_person_get, &name1), Eq(1));
    ASSERT_THAT(dbi->v1.person_get(db, id2, on_person_get, &name2), Eq(1));
    ASSERT_THAT(dbi->v1.person_get(db, 999, on_person_get, &name3), Eq(0));
    ASSERT_THAT(name1, Eq("name1"));
    ASSERT_THAT(name2, Eq("name2"));
    ASSERT_THAT(name3, Eq(""));
}
