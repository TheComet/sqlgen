#include <gmock/gmock.h>
#include "sqlgen/tests/exists.h"

#define NAME sqlgen_exists

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        exists_init();
        dbi = exists("sqlite3");
        db = dbi->open("exists.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        exists_deinit();
    }

    struct exists_interface* dbi;
    struct exists* db;
};

TEST_F(NAME, exists_returns_negative_on_error)
{
    ASSERT_THAT(dbi->invalid.exists(db, "name1"), Lt(0));
}
TEST_F(NAME, exists_returns_positive_if_it_exists)
{
    ASSERT_THAT(dbi->valid.exists(db, "name1"), Gt(0));
    ASSERT_THAT(dbi->valid.exists(db, "name2"), Gt(0));
}
TEST_F(NAME, exists_returns_0_if_it_doesnt_exists)
{
    ASSERT_THAT(dbi->valid.exists(db, "name3"), Eq(0));
    ASSERT_THAT(dbi->valid.exists(db, ""), Eq(0));
}
