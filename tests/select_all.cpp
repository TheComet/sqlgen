#include <gmock/gmock.h>
#include "sqlgen/tests/select_all.h"

#define NAME sqlgen_select_all

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        select_all_init();
        dbi = select_all("sqlite3");
        db = dbi->open("select_all.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        select_all_deinit();
    }

    struct select_all_interface* dbi;
    struct select_all* db;
};
