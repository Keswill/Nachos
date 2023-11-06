// addrspace.h
//  Data structures to keep track of executing user programs
//  (address spaces).
//
//  For now, we don't keep any information about address spaces.
//  The user level CPU state is saved and restored in the thread
//  executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "copyright.h"
#include "filesys.h"
#include "syscall.h"

#define UserStackSize    1024  // increase this as necessary!

// Page relacement algorithm
enum PageRepAlg { PRA_OPT, PRA_FIFO, PRA_2ND, PRA_E2ND, PRA_LRU, PRA_RAND };


class AddrSpace {
  public:
    AddrSpace(char *noffFileName);  // Create an address space,
                    // initializing it with the program
                    // stored in the file "executable"
    ~AddrSpace();           // De-allocate an address space

    void InitRegisters();       // Initialize user-level CPU registers,
                    // before jumping to user code

    void SaveState();           // Save/restore address space-specific
    void RestoreState();        // info on a context switch

    void Print(void);  // Print page table

    SpaceId getSpaceID(void) { return spaceID; }

    int findPageFIFO(int inPage);        // FIFO
    int findPage2ndChance(int inPage);   // 2nd chance(Clock)
    int findPageE2ndChance(int inPage);  // Enhanced 2nd chance
    void updatePageLRU(int vpn);
    int findPageLRU(int inPage);   // LRU(Stack)
    void updatePageOpt(int vpn);
    int findPageOpt(int inPage);   // Optimal
    int findPageRand(int inPage);  // Random(Not a real algorithm)

    void updatePage(int vpn);
    void replacePage(unsigned int badVAddr);
    void writeBack(int victimPage);

  private:
    TranslationEntry *pageTable;
    int numPages;     // Number of pages in the virtual address space
    SpaceId spaceID;  // User program pid

    OpenFile *swapFile;  // Opened swap file per user process
    int *pagesInMem;     // Page(in memory) number list, items with -1 means no page allocated
    int idx;     // For FIFO/2nd/E2nd/LRU
    int bottom;  // For LRU only
    int count;   // For LRU only

    int lastVirtPage;  // For update page reference string
    int fdRefStr;      // Binary file descriptor to record reference string
    FILE *fpRefStr;    // Text file pointer to record reference string
    int optRefStrLen;  // Items in recorded reference string
    int refIdx;     // Next item index in recorded reference string
    unsigned short *optRefStr;  // Reference string for optimal page replacement algorithm
};

#endif // ADDRSPACE_H

