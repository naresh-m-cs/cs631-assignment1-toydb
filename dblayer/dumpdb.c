#include <stdio.h>
#include <stdlib.h>
#include "codec.h"
#include "tbl.h"
#include "util.h"
#include "../pflayer/pf.h"
#include "../amlayer/am.h"
#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(1);}}


void
printRow(void *callbackObj, RecId rid, byte *row, int len) {
    Schema *schema = (Schema *) callbackObj;
    byte *cursor = row;

    char str[256];

    for (int i = 0; i < schema->numColumns; i++) {

        if (schema->columns[i]->type == VARCHAR) {
            int bytes = DecodeCString(cursor, str, sizeof(str));
            printf("%s", str);
            cursor += bytes;

        } else if (schema->columns[i]->type == INT) {
            int value = DecodeInt(cursor);
            printf("%d", value);
            cursor += sizeof(int);

        } else if (schema->columns[i]->type == LONG) {
            long long value = DecodeLong(cursor);
            printf("%lld", value);
            cursor += sizeof(long long);
        }

        if (i < schema->numColumns - 1)
          printf(",");
    }

    printf("\n");

}

#define DB_NAME "data.db"
#define INDEX_NAME "data.db.0"
	 
void
index_scan(Table *tbl, Schema *schema, int indexFD, int op, int value) {

    int scanDesc;
    int rid;
    int len;
    int err;
    byte record[4096];

    scanDesc = AM_OpenIndexScan(indexFD,'i',sizeof(int),op,(char *)&value);

    if (scanDesc < 0) {
        AM_PrintError("AM_OpenIndexScan failed");
        exit(EXIT_FAILURE);
    }

    while ((rid = AM_FindNextEntry(scanDesc)) >= 0) {

        len = Table_Get(tbl, rid, record, sizeof(record));

        if (len < 0) {
            fprintf(stderr, "Table_Get failed\n");
            exit(EXIT_FAILURE);
        }

        printRow(schema, rid, record, len);
    }

    err = AM_CloseIndexScan(scanDesc);

    if (err < 0) {
        AM_PrintError("AM_CloseIndexScan failed");
        exit(EXIT_FAILURE);
    }
}

int
main(int argc, char **argv) {
    char *schemaTxt = "Country:varchar,Capital:varchar,Population:int";
    Schema *schema = parseSchema(schemaTxt);
    Table *tbl;
    int err;

    err = Table_Open(DB_NAME, schema, false, &tbl);
    checkerr(err);

    if (argc == 2 && *(argv[1]) == 's') {
	Table_Scan(tbl, schema, printRow);
	// invoke Table_Scan with printRow, which will be invoked for each row in the table.
    } else {
	// index scan by default
	int indexFD = PF_OpenFile(INDEX_NAME);
	checkerr(indexFD);

	// Ask for populations less than 100000, then more than 100000. Together they should
	// yield the complete database.
	index_scan(tbl, schema, indexFD, LESS_THAN_EQUAL, 100000);
	index_scan(tbl, schema, indexFD, GREATER_THAN, 100000);

	err = PF_CloseFile(indexFD);
        checkerr(err);
    }
    Table_Close(tbl);
}
