#include "CheckedMemory.h"
#include "StringView.h"
#include "libutils\Defs.h"
#include <stddef.h>
#include <stdio.h>
#include "libutils\BitTwiddling.h"
#include "libutils\IntrusiveHeap.h"

/*this makes our hashtables unchecked*/
#define UNCHECKED

typedef struct {
   QueueNode node;
   StringView file;
   u32 line;
   u32 allocCount;   
} FileEntry;

FileEntry *_fileAllocCompareFunc(FileEntry *n1, FileEntry *n2){
   return n1->allocCount > n2->allocCount ? n1 : n2;
}

static PriorityQueue *getAllocPQ(){
   static PriorityQueue *out = NULL;
   if (!out){
      out = priorityQueueCreateUnchecked(offsetof(FileEntry, node), (PQCompareFunc)&_fileAllocCompareFunc);
   }

   return out;
}

static void freeAllocPQ(){
   priorityQueueDestroyUnchecked(getAllocPQ());
}

#define HashTableT FileEntry
#include "libutils\HashTable_Create.h"

static i32 _fileEntryCompare(FileEntry *e1, FileEntry *e2){
   return e1->file == e1->file && e1->line == e2->line;
}

static u32 _fileEntryHash(FileEntry *file){
   u32 out = 5031;
   out += (out << 5) + file->line;
   out += (out << 5) + hashPtr((void*)file->file);
   return  (out << 5) + out;
}

static ht(FileEntry) *getFileTable(){
   static ht(FileEntry) *out = NULL;
   if (!out){
      out = htCreate(FileEntry)(&_fileEntryCompare, &_fileEntryHash, NULL);
   }
   return out;
}

static void freeFileTable(){
   htDestroy(FileEntry)(getFileTable());
}

typedef struct {
   StringView file, func;
   u32 line, bytes;
}AllocData;

typedef struct {
   void *key;
   AllocData value;
} adEntry;

#define HashTableT adEntry
#include "libutils\HashTable_Create.h"

static i32 _adEntryCompare(adEntry *e1, adEntry *e2){
   return e1->key == e2->key;
}

static u32 _adEntryHash(adEntry *ad){
   return hashPtr(ad->key);
}

static ht(adEntry) *getMemTable(){
   static ht(adEntry) *out = NULL;
   if (!out){
      out = htCreate(adEntry)(&_adEntryCompare, &_adEntryHash, NULL);
   }
   return out;
}

static void freeMemTable(){
   htDestroy(adEntry)(getMemTable());
}

static void addAlloc(StringView file, u32 line){
   ht(FileEntry)* rpt = getFileTable();
   FileEntry filerpt = { 0 };
   FileEntry *found;
   
   filerpt.allocCount = 0;
   filerpt.file = file;
   filerpt.line = line;
   
   found = htFind(FileEntry)(rpt, &filerpt);
   if (!found){
      ++filerpt.allocCount;
      htInsert(FileEntry)(rpt, &filerpt);
   }
   else{
      found->allocCount += 1;
   }
}

void* checkedMallocImpl(u32 sz, char *func, char* file, u32 line){
   StringView str = stringIntern(file);
   adEntry newEntry = { malloc(sz), { str, stringIntern(func), line, sz } };
   htInsert(adEntry)(getMemTable(), &newEntry);
   addAlloc(str, line);
   return newEntry.key;
}
void* checkedCallocImpl(u32 count, u32 sz, char *func, char* file, u32 line){
   StringView str = stringIntern(file);
   adEntry newEntry = { calloc(count, sz), { str, stringIntern(func), line, count*sz } };
   htInsert(adEntry)(getMemTable(), &newEntry);
   addAlloc(str, line);
   return newEntry.key;
}
void checkedFreeImpl(void* mem){
   adEntry e = { mem, { 0 } };
   
   if (!mem){ return; }
   htErase(adEntry)(getMemTable(), htFind(adEntry)(getMemTable(), &e));
   free(mem);
}

void printMemoryLeaks(){
#ifdef _DEBUG
   i32 leaks = 0;
   FILE *output = NULL;
   PriorityQueue *allocPQ = getAllocPQ();

   htForEach(FileEntry, e, getFileTable(), {
      priorityQueuePush(allocPQ, e);
   });

   output = fopen("allocReport.csv", "wt");
   if (output){
      fprintf(output, "File,Line,Alloc Count\n");

      while (!priorityQueueIsEmpty(allocPQ)){
         FileEntry *entry = priorityQueuePop(allocPQ);
         fprintf(output, "%s,%i,%i\n", entry->file, entry->line, entry->allocCount);
      }
      fclose(output);
   }   

   output = fopen("memleak.txt", "wt");
   if (output){
      fprintf(output, "MEMORY LEAKS\n");
      fprintf(output, "-------------START---------------\n");
      htForEach(adEntry, e, getMemTable(), {
         AllocData *data = &e->value;
         fprintf(output, "%i bytes in %s(%s:%i)\n", data->bytes, data->func, data->file, data->line);
         ++leaks;
      });
      fprintf(output, "-------------END-----------------\n");

      fclose(output);
   }

   ASSERT(!leaks);
   
   freeMemTable();
   freeFileTable();
   freeAllocPQ();

   
   
#endif
}