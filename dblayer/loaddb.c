#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "codec.h"
#include "../pflayer/pf.h"
#include "../amlayer/am.h"
#include "tbl.h"
#include "util.h"

#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(1);}}

#define MAX_PAGE_SIZE 4000


#define DB_NAME "data.db"
#define INDEX_NAME "data.db.0"
#define CSV_NAME "data.csv"


/*
Takes a schema, and an array of strings (fields), and uses the functionality
in codec.c to convert strings into compact binary representations
 */
int
encode(Schema *sch, char **fields, byte *record, int spaceLeft) {

    int offset = 0;

    for (int i = 0; i < sch->numColumns; i++) {
        int len = 0;

        switch (sch->columns[i]->type) {
            case VARCHAR:
                len = EncodeCString(fields[i],
                                    record + offset,
                                    spaceLeft);
                break;

            case INT:
                len = EncodeInt(atoi(fields[i]),
                                record + offset);
                break;

            case LONG:
                len = EncodeLong(atoll(fields[i]),
                                 record + offset);
                break;

            default:
                fprintf(stderr, "Unknown column type\n");
                exit(EXIT_FAILURE);
        }

        offset += len;
        spaceLeft -= len;
    }

    return offset;

}

Schema *
loadCSV() {
    // Open csv file, parse schema
    FILE *fp = fopen(CSV_NAME, "r");
    if (!fp) {
	perror("data.csv could not be opened");
        exit(EXIT_FAILURE);
    }

    char buf[MAX_LINE_LEN];
    char *line = fgets(buf, MAX_LINE_LEN, fp);
    if (line == NULL) {
	fprintf(stderr, "Unable to read data.csv\n");
	exit(EXIT_FAILURE);
    }

    // Open main db file
    Schema *sch = parseSchema(line);
    Table *tbl;
    int err;
    int indexFD;

       err = Table_Open(DB_NAME, sch, true, &tbl);
		checkerr(err);

	err = AM_CreateIndex(DB_NAME, 0, 'i', sizeof(int));
	if (err < 0) {
    		AM_PrintError("AM_CreateIndex failed");
    			exit(EXIT_FAILURE);
	}       

		indexFD = PF_OpenFile(INDEX_NAME);
		checkerr(indexFD);
   

    char *tokens[MAX_TOKENS];
    char record[MAX_PAGE_SIZE];

    while ((line = fgets(buf, MAX_LINE_LEN, fp)) != NULL) {
	int n = split(line, ",", tokens);
	assert (n == sch->numColumns);
	int len = encode(sch, tokens, record, sizeof(record));
	RecId rid;
	
	err = Table_Insert(tbl, (byte *)record, len, &rid);
	checkerr(err);

	printf("%d %s\n", rid, tokens[0]);

	// Indexing on the population column 
	int population = atoi(tokens[2]);

	err = AM_InsertEntry(indexFD, 'i',sizeof(int),(char *)&population,rid);
                     


	
	    
	checkerr(err);
    }
    fclose(fp);
    Table_Close(tbl);
    err = PF_CloseFile(indexFD);
    checkerr(err);
    return sch;
}

int
main() {
    loadCSV();
}
