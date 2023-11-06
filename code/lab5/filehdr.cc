// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"


FileHeader::FileHeader(void)
{
    memset(dataSectors, 0, sizeof(dataSectors));
    bHdrChange = false;
    lastModTime = 0;
}

FileHeader::~FileHeader()
{
    if(bHdrChange)
        WriteBack(sectorNo);
}

//----------------------------------------------------------------------
// FileHeader:: ChangeFileSize
// 	Allocate new sectors if needed.
//----------------------------------------------------------------------

bool FileHeader::ChangeFileSize (int newFileSize)
{
    int i;

    if(newFileSize <= numBytes) // Need not change file size
        return true;

    int numSectorsSet = divRoundUp(newFileSize, SectorSize); // Sectors after change
    if (numSectorsSet == numSectors) { // File size extened, but sectors unchanged
        numBytes = newFileSize; 
        bHdrChange = true;
        return true;
    }

    BitMap *freeMap = new BitMap(NumSectors);
    OpenFile *bitMapFile = new OpenFile(0);  // Sector 0 is FreeMapSector
    freeMap->FetchFrom(bitMapFile); 

    if (numSectorsSet > MaxFileSectors || freeMap->NumClear() < (numSectorsSet - numSectors)) {
        printf("Failed to chang file size, no room\n");
        delete bitMapFile;
        delete freeMap;
        return false;
    }

    if(numSectorsSet <= NumIndex1) { // Only 1st stage index needed
        for (i = numSectors; i < numSectorsSet; i++) {
            dataSectors[i] = freeMap->Find();
            //printf("Add index1 data sector %d\n", dataSectors[i]);
        }
    }
    else { // 2nd stage index needed
        if(numSectors <= NumIndex1) {  // Use all 1st stage index first, then create 2nd stage index
            for (i = numSectors; i <= NumIndex1; i++) {
                dataSectors[i] = freeMap->Find();
                /*if(i == NumIndex1)
                    printf("Index2 sector # is: %d\n", dataSectors[i]);
                else
                    printf("Add index1 data sector %d\n", dataSectors[i]);*/
            }
            numSectors = NumIndex1;
        }

        int dataSectors2[NumIndex2];
	    synchDisk->ReadSector(dataSectors[NumIndex1], (char *)dataSectors2);
        for (i = 0; i < numSectorsSet - numSectors; i++) {
	        dataSectors2[i + numSectors - NumIndex1] = freeMap->Find();
            //printf("Add index2 data sector %d\n", dataSectors2[i + numSectors - NumIndex1]);
        }
	    synchDisk->WriteSector(dataSectors[NumIndex1], (char *)dataSectors2);
        //printf("Update index2 sector\n");
    }

    freeMap->WriteBack(bitMapFile);
    numBytes = newFileSize;
    numSectors = numSectorsSet;
    bHdrChange = true;
    DEBUG('f', "File size is %d, %d sectors\n", numBytes, numSectors);
    delete bitMapFile;
    delete freeMap;
    return true;
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{
    int i;

    numBytes = fileSize;
    numSectors  = divRoundUp(fileSize, SectorSize);
    if (numSectors > MaxFileSectors || freeMap->NumClear() < numSectors)
	    return false;		// not enough space

    if(numSectors <= NumIndex1) { // Only 1st stage index needed
        for (i = 0; i < numSectors; i++)
	        dataSectors[i] = freeMap->Find();
        dataSectors[NumIndex1] = -1;
    }
    else { // 2nd stage index needed
        for (i = 0; i <= NumIndex1; i++)
	        dataSectors[i] = freeMap->Find();

        int dataSectors2[NumIndex2];
        for (i = 0; i < numSectors - NumIndex1; i++)
	        dataSectors2[i] = freeMap->Find();

	    synchDisk->WriteSector(dataSectors[NumIndex1], (char *)dataSectors2);
    }
    return true;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    if(numSectors <= NumIndex1) { // Only 1st stage index exist
        for (int i = 0; i < numSectors; i++) {
	        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	        freeMap->Clear((int) dataSectors[i]);
	    }
    }
    else { // 2nd stage index exist
        for (int i = 0; i <= NumIndex1; i++) {
	        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	        freeMap->Clear((int) dataSectors[i]);
	    }
	    
	    int dataSectors2[NumIndex2];
	    synchDisk->ReadSector(dataSectors[NumIndex1], (char *)dataSectors2);
        for (int i = 0; i < numSectors - NumIndex1; i++) {
	        ASSERT(freeMap->Test((int) dataSectors2[i]));  // ought to be marked!
	        freeMap->Clear((int) dataSectors2[i]);
        }
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
    numSectors  = divRoundUp(numBytes, SectorSize);
    sectorNo = sector;
    bHdrChange = false;
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    DEBUG('f', "Writing back file header, sector %d.\n", sector);
    synchDisk->WriteSector(sector, (char *)this); 
    sectorNo = sector;
    bHdrChange = false;
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    int sector = offset / SectorSize;

    if(sector < NumIndex1) // index in 1st stage
        return dataSectors[sector];
    else { // index in 2nd stage
	    int dataSectors2[NumIndex2];
	    synchDisk->ReadSector(dataSectors[NumIndex1], (char *)dataSectors2);
        return dataSectors2[sector - NumIndex1];
    }
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

const char *delNewline(char *s)  // For ctime() or asctime() returned string ONLY!
{
  static char buf[50];
  char *p;

  strcpy(buf, s);
  p = strchr(buf, '\n');
  *p = '\0';
  return buf;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print(bool bPrintTime)
{
    int i, j, k;
    char *data = new char[SectorSize];
    int dataSectors2[NumIndex2];

	if(numSectors > NumIndex1)
	    synchDisk->ReadSector(dataSectors[NumIndex1], (char *)dataSectors2);

    if(bPrintTime)
        printf("FileHeader contents.  File size: %d.  File modification time: %s.  File blocks:\n", \
        numBytes, delNewline(ctime((time_t *)&lastModTime)));
    else
        printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < numSectors; i++) {
        if(i < NumIndex1)
	        printf("%d ", dataSectors[i]);
	    else {
	        if(i == NumIndex1)
	            printf(" Index2: ");
	        printf("%d ", dataSectors2[i - NumIndex1]);
	    }
    }

    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
        if(i < NumIndex1)
	        synchDisk->ReadSector(dataSectors[i], data);
        else
	        synchDisk->ReadSector(dataSectors2[i - NumIndex1], data);

        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	        if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		        printf("%c", data[j]);
            else
		        printf("\\%x", (unsigned char)data[j]);
	    }
        printf("\n"); 
    }
    delete [] data;
}


time_t FileHeader::getModTime()  // Get last modify time
{
    return (time_t)lastModTime;
}

void FileHeader::setModTime(time_t modTime)  // Set last modify time
{
    lastModTime = (unsigned)modTime;
}

