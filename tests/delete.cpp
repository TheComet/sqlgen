#include <gmock/gmock.h>
#include "sqlgen/tests/delete.h"

#define NAME sqlgen_delete

using namespace testing;

struct NAME : public Test
{
    void SetUp() override {
        delete_db_init();
        dbi = delete_db("sqlite3");
        db = dbi->open("delete.db");
        dbi->reinit(db);
    }

    void TearDown() override {
        dbi->close(db);
        delete_db_deinit();
    }

    struct delete_db_interface* dbi;
    struct delete_db* db;
};
