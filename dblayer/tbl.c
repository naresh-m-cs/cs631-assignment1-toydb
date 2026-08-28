
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tbl.h"
#include "codec.h"
#include "../pflayer/pf.h"

#define SLOT_COUNT_OFFSET 2
#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(EXIT_FAILURE);}}
#define PAGE_SIZE 4000
#define HEADER_SIZE 2

int  getLen(int slot, byte *pageBuf);
int  getNumSlots(byte *pageBuf);
void setNumSlots(byte *pageBuf, int nslots);
int  getNthSlotOffset(int slot, char* pageBuf);



int getNumSlots(byte *pageBuf) {
    int nslots;
    memcpy(&nslots, pageBuf, 2);
    return nslots;
}

void setNumSlots(byte *pageBuf, int nslots) {
    memcpy(pageBuf, &nslots, 2);
}

int getNthSlotOffset(int slot, char *pageBuf) {
    int headerSize = 4; // 2 bytes for nslots + 2 bytes for free space pointer
    int slotSize = 4;   // 4 bytes per slot entry (2 bytes offset + 2 bytes length)
    
    return headerSize + (slot * slotSize);
}
/**
   Opens a paged file, creating one if it doesn't exist, and optionally
   overwriting it.
   Returns 0 on success and a negative error code otherwise.
   If successful, it returns an initialized Table*.
 */
int
Table_Open(char *dbname, Schema *schema, bool overwrite, Table **ptable)
{
    // Initialize PF, create PF file,
    PF_Init();

    int err;

    if (overwrite) {
        // If overwrite is requested, destroy the old file first (ignore error if it didn't exist)
        PF_DestroyFile(dbname);
        
        // Create a new file
        err = PF_CreateFile(dbname);
        checkerr(err);
    } else {
        // Try creating the file. If it already exists, PF_CreateFile will return a negative error code.
        // Since overwrite is false, an existing file is expected, so we just try to open it.
        err = PF_CreateFile(dbname);
        // We don't call checkerr(err) here because failing because the file 
        // already exists is normal when opening an existing database!
    }


    // open the file
    int fd = PF_OpenFile(dbname);
    checkerr(fd);

    // allocate Table structure  and initialize and return via ptable

    // 1. Allocate memory for Table structure
    Table *tbl = (Table *)malloc(sizeof(Table));
    if (! tbl) {
        return -1; // memory allocation error
    }

    tbl->schema = schema;
    tbl->fd = fd;

    *ptable = tbl;
    return 0;
    // The Table structure only stores the schema. The current functionality
    // does not really need the schema, because we are only concentrating
    // on record storage. 
}

void
Table_Close(Table *tbl) {
    if (tbl) {
        // Close the underlying paged file descriptor
        int err = PF_CloseFile(tbl->fd);
        checkerr(err);
        
        // Free the table structure memory
        free(tbl);
    }
    // Unfix any dirty pages, close file.
}


int
Table_Insert(Table *tbl, byte *record, int len, RecId *rid) {
    // Allocate a fresh page if len is not enough for remaining space
    // Get the next free slot on page, and copy record in the free
    // space
    // Update slot and free space index information on top of page.

    int pageNum;
    char *pageBuf;
    int err;

    // 1. Get the last page of the file, or allocate a new one if empty
    int totalPages = PF_GetNumPages(tbl->fd);
    if (totalPages < 0) checkerr(totalPages);

    if (totalPages == 0) {
        err = PF_AllocPage(tbl->fd, &pageNum, &pageBuf);
        checkerr(err);
        setNumSlots(pageBuf, 0); // Initialize slot count to 0
    } else {
        pageNum = totalPages - 1;
        err = PF_GetThisPage(tbl->fd, pageNum, &pageBuf);
        checkerr(err);
    }

    // 2. Inspect current page metadata
    int nslots = getNumSlots(pageBuf);
    
    // The top free space boundary is right after the last slot entry
    int freeSpaceTop = getNthSlotOffset(nslots, pageBuf);

    // Find the lowest record offset on the page (bottom-up boundary)
    int lowestRecordOffset = PAGE_SIZE;
    for (int i = 0; i < nslots; i++) {
        int slotAddr = getNthSlotOffset(i, pageBuf);
        int recordOff;
        memcpy(&recordOff, pageBuf + slotAddr, 2); // Read record offset from slot
        if (recordOff < lowestRecordOffset) {
            lowestRecordOffset = recordOff;
        }
    }

    // 3. Check if there is enough space for [New Record (len)] + [New Slot Entry (4 bytes)]
    int slotSize = 4;
    int spaceNeeded = len + slotSize;
    int spaceAvailable = lowestRecordOffset - freeSpaceTop;

    if (spaceAvailable < spaceNeeded) {
        // Page is full! Unfix and allocate a new page.
        err = PF_UnfixPage(tbl->fd, pageNum, FALSE);
        checkerr(err);

        err = PF_AllocPage(tbl->fd, &pageNum, &pageBuf);
        checkerr(err);

        nslots = 0;
        setNumSlots(pageBuf, 0);
        freeSpaceTop = getNthSlotOffset(0, pageBuf);
        lowestRecordOffset = PAGE_SIZE;
    }

    // 4. Calculate new record position (growing bottom-up from the bottom of the page)
    int newRecordOffset = lowestRecordOffset - len;

    // 5. Copy record data into the page buffer
    memcpy(pageBuf + newRecordOffset, record, len);

    // 6. Write the new slot entry [Offset][Length] at the slot array position
    int slotAddr = getNthSlotOffset(nslots, pageBuf);
    memcpy(pageBuf + slotAddr, &newRecordOffset, 2);
    memcpy(pageBuf + slotAddr + 2, &len, 2);

    // 7. Update header slot count
    setNumSlots(pageBuf, nslots + 1);

    // 8. Construct the RecId (Upper 16 bits = pageNum, Lower 16 bits = slotIndex)
    *rid = (pageNum << 16) | nslots;

    // 9. Mark page as dirty and unfix it
    err = PF_DirtyPage(tbl->fd, pageNum);
    checkerr(err);
    err = PF_UnfixPage(tbl->fd, pageNum, TRUE);
    checkerr(err);

    return 0;
}


#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(EXIT_FAILURE);}}

/*
  Given an rid, fill in the record (but at most maxlen bytes).
  Returns the number of bytes copied.
 */
int
Table_Get(Table *tbl, RecId rid, byte *record, int maxlen) {
    int slot = rid & 0xFFFF;
    int pageNum = rid >> 16;

    UNIMPLEMENTED;
    // PF_GetThisPage(pageNum)
    // In the page get the slot offset of the record, and
    // memcpy bytes into the record supplied.
    // Unfix the page
    return len; // return size of record
}

void
Table_Scan(Table *tbl, void *callbackObj, ReadFunc callbackfn) {

    UNIMPLEMENTED;

    // For each page obtained using PF_GetFirstPage and PF_GetNextPage
    //    for each record in that page,
    //          callbackfn(callbackObj, rid, record, recordLen)
}


