// addrspace.cc
//  Routines to manage address spaces (executing user programs).
//
//  In order to run a user program, you must:
//
//  1. link with the -N -T 0 option
//  2. run coff2noff to convert the object file to Nachos format
//      (Nachos object code format is essentially just a simpler
//      version of the UNIX executable object code format)
//  3. load the NOFF file into the Nachos file system
//      (if you haven't implemented the file system yet, you
//      don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
//  Do little endian to big endian conversion on the bytes in the
//  object file header, in case the file was generated on a little
//  endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader (NoffHeader *noffH)
{
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
//  Create an address space to run a user program.
//  Load the program from a file noffFileName, and set everything
//  up so that we can start executing user instructions.
//
//  Assumes that the object code file is in NOFF format.
//
//  noffFileName is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(char *noffFileName)
{
    NoffHeader noffH;
    int i;
    unsigned int memSize;
    char sFileName[24];


    // Get SpaceId
    bool flag = false;
    for(i = 0; i < NumPhysPages; i++) {
        if(!ProgMap[i]) {
            ProgMap[i] = true;
            flag = true;
            spaceID = i;
            break;
        }
    }
    ASSERT(flag);


    OpenFile *executable = fileSystem->Open(noffFileName);
    if (executable == NULL) {
        printf("Can't open NOFF file %s\n", noffFileName);
        currentThread->Finish();
    }

    // Read NoffHeader and change endian if needed
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    // How big is address space?
    memSize = noffH.code.size + noffH.initData.size + noffH.uninitData.size
            + UserStackSize;  // we need to increase the size
                        // to leave room for the stack
    numPages = divRoundUp(memSize, PageSize);
    memSize = numPages * PageSize;

    ASSERT( maxFramesPerProc <= NumPhysPages &&
       maxFramesPerProc <= freeMM_Map->NumClear() );  // check we're not trying
                                // to run anything too big


    DEBUG('a', "Initializing address space, num pages %d, memory size %d\n",
                    numPages, memSize);

    // First, set up the translation page table
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;  // for TLB only
        pageTable[i].physicalPage = -1;  // Not in phy. memory
        pageTable[i].valid = false;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;  // if the code segment was entirely on
                    // a separate page, we could set its
                    // pages to be read-only
    }


    // Create and open swap file per user process
    sprintf(sFileName, "SWAP%d", spaceID);
    if (!fileSystem->Create(sFileName, 0)) {	 // Create swap file
	    printf("Can't create swap file %s\n", sFileName);
	    currentThread->Finish();
    }
    swapFile = fileSystem->Open(sFileName);
    if (swapFile == NULL) {
        printf("Can't open swap file %s\n", sFileName);
        currentThread->Finish();
    }


    // Fill the entire swap file with 0s for uninitialized data
    char *pageBuf = new char[PageSize];
    bzero(pageBuf, PageSize);
    for(i = 0; i < numPages; i++)
        swapFile->WriteAt(pageBuf, PageSize, i * PageSize);
    delete [] pageBuf;


    // Copy the code segment from excutable to swap file
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
            noffH.code.virtualAddr, noffH.code.size);
        char *buf = new char[noffH.code.size];
        executable->ReadAt(buf, noffH.code.size, noffH.code.inFileAddr);
        swapFile->WriteAt(buf, noffH.code.size, noffH.code.virtualAddr);
        delete [] buf;
    }

    // Copy the initialized data segment from excutable to swap file
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
            noffH.initData.virtualAddr, noffH.initData.size);
        char *buf = new char[noffH.initData.size];
        executable->ReadAt(buf, noffH.initData.size, noffH.initData.inFileAddr);
        swapFile->WriteAt(buf, noffH.initData.size, noffH.initData.virtualAddr);
        delete [] buf;
    }

    delete executable;

    printf("User program: %s, SpaceId: %d, Memory size: %u\n", \
        noffFileName, spaceID, memSize);
    printf("Max frames per user process: %d, Swap file: %s, Page replacement algorithm: %s\n", \
        maxFramesPerProc, sFileName, pageRepAlgName[pageRepAlg]);


    // Initialize pagesInMem array
    pagesInMem = new int[maxFramesPerProc];
    for(i = 0; i < maxFramesPerProc; i++)
        pagesInMem[i] = -1;

    idx = 0;
    bottom = 0;
    count = 0;

    if(bRecRefStr) {  // Record reference string
        lastVirtPage = -1;

        sprintf(sFileName, "REFSTR%d", spaceID);
        if( (fdRefStr = open(sFileName, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1 ) {
            printf("Can't open binary reference string file %s for write\n", sFileName);
            currentThread->Finish();
        }

        sprintf(sFileName, "REFSTR%d.TXT", spaceID);
        if((fpRefStr = fopen(sFileName, "wb")) == NULL) {
            printf("Can't open text reference string file %s for write\n", sFileName);
            currentThread->Finish();
        }
    }

    else if(pageRepAlg == PRA_OPT) {  // Optimal. Get reference string from recorded host file
        sprintf(sFileName, "REFSTR%d", spaceID);
        if( (fdRefStr = open(sFileName, O_RDONLY)) == -1 ) {
            printf("Can't open binary reference string file %s for read\n", sFileName);
            currentThread->Finish();
        }
        int refStrFileLen = lseek(fdRefStr, 0, SEEK_END);
        lseek(fdRefStr, 0, SEEK_SET);
        printf("Binary reference string file %s length: %d\n", sFileName, refStrFileLen);
        if(refStrFileLen % 2) {
            printf("The length of binary reference string file %s must be even\n", sFileName);
            currentThread->Finish();
        }

        optRefStrLen = refStrFileLen / 2;
        printf("Reference string items: %d\n", optRefStrLen);

        optRefStr = new unsigned short[optRefStrLen];
        int res, nRead = 0, nLeft = refStrFileLen;
        while(nLeft) {
            res = read(fdRefStr, (char *)optRefStr + nRead, nLeft);
            if(!res)
                break;
            else if(res == -1) {
                printf("Binary reference string file %s read error\n", sFileName);
                currentThread->Finish();
            }
            else {
                printf("%d bytes read from binary reference string file %s\n", res, sFileName);
                nRead += res;
                nLeft -= res;
            }
        }

        refIdx = 0;
        close(fdRefStr);
    }


    Print();

}  // AddrSpace::AddrSpace


//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
//  Dealloate an address space.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    ProgMap[spaceID] = 0;
    for (int i = 0; i < numPages; i++)
        if (pageTable[i].valid)
            freeMM_Map->Clear(pageTable[i].physicalPage);

    delete [] pageTable;
    delete swapFile;
    delete [] pagesInMem;

    if(pageRepAlg == PRA_OPT)
        delete [] optRefStr;

    if(bRecRefStr) {
        if(fdRefStr != -1)
            close(fdRefStr);
        if(fpRefStr != NULL)
            fclose(fpRefStr);
    }
}


//----------------------------------------------------------------------
// AddrSpace::InitRegisters
//  Set the initial values for the user-level register set.
//
//  We write these directly into the "machine" registers, so
//  that we can immediately jump to user code.  Note that these
//  will be saved/restored into the currentThread->userRegisters
//  when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
    machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we don't
    // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
//  On a context switch, save any machine state, specific
//  to this address space, that needs saving.
//
//  For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState()
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
//  On a context switch, restore the machine state so that
//  this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState()
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}

//----------------------------------------------------------------------
// AddrSpace::Print
//  Print page table info with virtual memory
//----------------------------------------------------------------------

void AddrSpace::Print(void)
{
    printf("SpaceId: %d, Page table dump: %d pages in total\n", spaceID, numPages);
    printf("===============================\n");
    printf(" Page, Frame, Valid, Use, Dirty\n");
    for (int i = 0; i < numPages; i++)
        printf("%5d,  %4d,     %d,   %d,     %d\n",\
        i, pageTable[i].physicalPage,\
        pageTable[i].valid, pageTable[i].use, pageTable[i].dirty);
    printf("===============================\n\n");
}


//----------------------------------------------------------------------
// AddrSpace::findPageFIFO
//  Find a victim page to be replaced.
//  FIFO page replacement algorithm.
//---------------------------------------------------------------------
int AddrSpace::findPageFIFO(int inPage)
{
    int victim = pagesInMem[idx];
    pagesInMem[idx] = inPage;
    idx = (idx + 1) % maxFramesPerProc;

    return victim;
}


//----------------------------------------------------------------------
// AddrSpace::findPage2ndChance
//  Find a victim page to be replaced.
//  2nd chance(Clock) page replacement algorithm.
//---------------------------------------------------------------------
int AddrSpace::findPage2ndChance(int inPage)
{
    bool bFound = false;
    int victim;

    while(!bFound) {
        if(pagesInMem[idx] < 0) {
            victim = -1;
            pagesInMem[idx] = inPage;
            bFound = true;
        }
        else if(pageTable[pagesInMem[idx]].use)
            pageTable[pagesInMem[idx]].use = false;
        else {
            victim = pagesInMem[idx];
            pagesInMem[idx] = inPage;
            bFound = true;
        }

        idx = (idx + 1) % maxFramesPerProc;
    }

    return victim;
}


//----------------------------------------------------------------------
// AddrSpace::findPageE2ndChance
//  Find a victim page to be replaced.
//  Enhanced 2nd chance page replacement algorithm.
//---------------------------------------------------------------------
int AddrSpace::findPageE2ndChance(int inPage)
{
    int victim;

    for(int loop = 1; loop <= 4; loop++) {
        for(int i = 0; i < maxFramesPerProc; i++) {
            if(loop == 1 || loop == 3) {
                if(loop == 1 && pagesInMem[idx] < 0) {
                    pagesInMem[idx] = inPage;
                    idx = (idx + 1) % maxFramesPerProc;
                    return -1;
                }
                else if(!pageTable[pagesInMem[idx]].use && !pageTable[pagesInMem[idx]].dirty) {
                    victim = pagesInMem[idx];
                    pagesInMem[idx] = inPage;
                    idx = (idx + 1) % maxFramesPerProc;
                    return victim;
                }
            }
            else {  // loop == 2 || loop == 4
                if(!pageTable[pagesInMem[idx]].use && pageTable[pagesInMem[idx]].dirty) {
                    victim = pagesInMem[idx];
                    pagesInMem[idx] = inPage;
                    idx = (idx + 1) % maxFramesPerProc;
                    return victim;
                }
                else if(loop == 2 && pageTable[pagesInMem[idx]].use)
                    pageTable[pagesInMem[idx]].use = false;
            }

            idx = (idx + 1) % maxFramesPerProc;
        }  // for(i)
    }  // for(loop)

    // Unreachable
    printf("Fatal error! Enhanced 2nd chance failed to find victim frame\n");
    return -1;
}


//----------------------------------------------------------------------
// AddrSpace::updatePageLRU
//  Update stack for LRU page replacement algorithm.
//---------------------------------------------------------------------
void AddrSpace::updatePageLRU(int vpn)
{
    int i, j;

    for(i = 0; i < count; i++) {
        if(pagesInMem[(bottom + i) % maxFramesPerProc] == vpn) {  // Found in stack, merge
            for(j = i; j < count; j++)
                pagesInMem[(bottom + j) % maxFramesPerProc] = pagesInMem[(bottom + j + 1) % maxFramesPerProc];
            pagesInMem[(bottom + count - 1) % maxFramesPerProc] = vpn;
            return;
        }
    }

    printf("Fatal error! Page %d not in LRU stack\n", vpn);
}


//----------------------------------------------------------------------
// AddrSpace::findPageLRU
//  Find a victim page to be replaced.
//  LRU page replacement algorithm
//---------------------------------------------------------------------
int AddrSpace::findPageLRU(int inPage)
{
    int victim;

    if(count < maxFramesPerProc) {
        pagesInMem[(bottom + count) % maxFramesPerProc] = inPage;
        count++;
        return -1;
    }

    victim = pagesInMem[bottom];
    pagesInMem[bottom] = inPage;
    bottom = (bottom + 1) % maxFramesPerProc;
    return victim;
}


//----------------------------------------------------------------------
// AddrSpace::updatePageOpt
//  Update index pointer for optimal page replacement algorithm.
//---------------------------------------------------------------------
void AddrSpace::updatePageOpt(int vpn)
{
    if(optRefStr[refIdx] == vpn)  // Same page reference
        return;
    else {
        if(++refIdx >= optRefStrLen) {
            printf("Fata error! Run out of optimal reference string\n");
            currentThread->Finish();
        }
        if(optRefStr[refIdx] != vpn) {  // Next page reference not match
            printf("Fata error! Optimal reference string item #%d mismatch, expect %d, is %d\n",\
                    refIdx, optRefStr[refIdx], vpn);
            currentThread->Finish();
        }
        if(refIdx == optRefStrLen - 1) {
            char sFileName[24];
            sprintf(sFileName, "REFSTR%d", spaceID);
            printf("Reach the last reference string item in %s\n", sFileName);
        }
    }
}


//----------------------------------------------------------------------
// AddrSpace::findPageOpt
//  Find a victim page to be replaced.
//  Optimal page replacement algorithm
//---------------------------------------------------------------------
int AddrSpace::findPageOpt(int inPage)
{
    int victim;

    if(pagesInMem[idx] < 0) {
        pagesInMem[idx] = inPage;
        idx = (idx + 1) % maxFramesPerProc;
        return -1;
    }

    int i, j, lookPage, dist = -1, index = 0;
    bool bInfinite;
    for(i = 0; i < maxFramesPerProc; i++) {
        lookPage = pagesInMem[i];
        bInfinite = true;
        for(j = refIdx; j < optRefStrLen; j++) {
            if(lookPage == optRefStr[j]) {  // 1st hit of this frame
                if(j - refIdx > dist) {
                    dist = j - refIdx;
                    index = i;
                }
                bInfinite = false;
                break;
            }
        }
        if(bInfinite) {
            pagesInMem[i] = inPage;
            return lookPage;
        }
    }

    victim = pagesInMem[index];
    pagesInMem[index] = inPage;
    return victim;
}


//----------------------------------------------------------------------
// AddrSpace::findPageRand
//  Find a victim page to be replaced.
//  Random page replacement algorithm(Not a real algorithm)
//---------------------------------------------------------------------
int AddrSpace::findPageRand(int inPage)
{
    int index, victim;

    if(pagesInMem[idx] < 0) {
        pagesInMem[idx] = inPage;
        idx = (idx + 1) % maxFramesPerProc;
        return -1;
    }

    index = Random() % maxFramesPerProc;
    victim = pagesInMem[index];
    pagesInMem[index] = inPage;
    return victim;
}


//----------------------------------------------------------------------
// AddrSpace::updatePage
//  Update LRU stack & page reference string.
//---------------------------------------------------------------------
void AddrSpace::updatePage(int vpn)
{
    if(pageRepAlg == PRA_LRU)
        updatePageLRU(vpn);
    else if(pageRepAlg == PRA_OPT) {
        updatePageOpt(vpn);
        return;
    }

    if(bRecRefStr && vpn != lastVirtPage) {
        lastVirtPage = vpn;

        // Write new page number to reference string files
        if(fdRefStr != -1) {
            if(vpn > SHRT_MAX)
                printf("Can't record page %d, page number must <= %d\n", vpn, SHRT_MAX);
            else
                write(fdRefStr, &vpn, sizeof(unsigned short));
        }

        if(fpRefStr != NULL)
            fprintf(fpRefStr, "%d\n", vpn);
    }
}


//----------------------------------------------------------------------
// AddrSpace::replacePage
//  Swap pages.
//---------------------------------------------------------------------
void AddrSpace::replacePage(unsigned int badVAddr)
{
    int inPage, outPage;

    stats->numPageFaults++;
    inPage = badVAddr / PageSize;

    switch(pageRepAlg) {
        case PRA_FIFO:
            outPage = findPageFIFO(inPage);
            break;
        case PRA_2ND:
            outPage = findPage2ndChance(inPage);
            break;

        case PRA_E2ND:
            outPage = findPageE2ndChance(inPage);
            break;

        case PRA_OPT:
            outPage = findPageOpt(inPage);
            break;

        case PRA_RAND:
            outPage = findPageRand(inPage);
            break;

        case PRA_LRU:
        default:
            outPage = findPageLRU(inPage);
            break;
    }

    if(outPage < 0) {  // Allocated frames not used out
        printf("Demand page %d in", inPage);
        pageTable[inPage].physicalPage = freeMM_Map->Find();
        if(pageTable[inPage].physicalPage < 0) {
            printf("\nPanic! Run out of user physical memory\n");
            currentThread->Finish();
        }
        printf("(frame %d)\n", pageTable[inPage].physicalPage);
    }
    else {  // Swap out, swap in
        printf("Swap page %d out, demand page %d in(frame %d)\n", outPage, inPage, pageTable[outPage].physicalPage);
        writeBack(outPage);
        pageTable[outPage].valid = false;
        pageTable[inPage].physicalPage = pageTable[outPage].physicalPage;
    }
    pageTable[inPage].valid = true;
    pageTable[inPage].use = true;
    pageTable[inPage].dirty = false;

    // Read to the physical frame just swapped out
    swapFile->ReadAt(&(machine->mainMemory[pageTable[inPage].physicalPage * PageSize]),
               PageSize, inPage * PageSize);
    Print();
}


//----------------------------------------------------------------------
// AddrSpace::writeBack
//  Write back the victim page if it's dirty.
//---------------------------------------------------------------------

void AddrSpace::writeBack(int victimPage)
{
    if(pageTable[victimPage].dirty) {
        printf("Write back victim page %d to disk\n", victimPage);
        swapFile->WriteAt(&(machine->mainMemory[pageTable[victimPage].physicalPage * PageSize]),
                    PageSize, victimPage * PageSize);
        stats->numPageWrites++;
    }
}

