#include <gmock/gmock.h>
#include "sqlgen/tests/select_first.h"

#define NAME sqlgen_select_first

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        select_first_init();
        dbi = select_first("sqlite3");
        db = dbi->open("select_first.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        select_first_deinit();
    }

    struct select_first_interface* dbi;
    struct select_first* db;
};
