#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


#define ALIGN4(s)         (((((s) - 1) >> 2) << 2) + 4)
#define BLOCK_DATA(b)      ((b) + 1)
#define BLOCK_HEADER(ptr)   ((struct block *)(ptr) - 1)


static int atexit_registered = 0;
static int num_mallocs       = 0;
static int num_frees         = 0;
static int num_reuses        = 0;
static int num_grows         = 0;
static int num_splits        = 0;
static int num_coalesces     = 0;
static int num_blocks        = 0;
static int num_requested     = 0;
static long max_heap         = 0;
//static int max_heap          = 0;

/*
 *  \brief printStatistics
 *
 *  \param none
 *
 *  Prints the heap statistics upon process exit.  Registered
 *  via atexit()
 *
 *  \return none
 */
void printStatistics( void )
{
  printf("\nheap management statistics\n");
  printf("mallocs:\t%d\n", num_mallocs );
  printf("frees:\t\t%d\n", num_frees );
  printf("reuses:\t\t%d\n", num_reuses );
  printf("grows:\t\t%d\n", num_grows );
  printf("splits:\t\t%d\n", num_splits );
  printf("coalesces:\t%d\n", num_coalesces );
  printf("blocks:\t\t%d\n", num_blocks );
  printf("requested:\t%d\n", num_requested );
  printf("max heap:\t%ld\n", max_heap );
  //printf("max heap:\t%d\n", max_heap );
}

struct block 
{
   size_t      size;  /* Size of the allocated block of memory in bytes */
   struct block *next;  /* Pointer to the next block of allcated memory   */
   bool        free;  /* Is this block free?                     */
};


struct block *FreeList = NULL; /* Free list to track the blocks available */
struct block *nextFit = NULL;  /* Pointer to track where next fit left off */
/*
 * \brief findFreeBlock
 *
 * \param last pointer to the linked list of free blocks
 * \param size size of the block needed in bytes 
 *
 * \return a block that fits the request or NULL if no free block matches
 */
struct block *findFreeBlock(struct block **last, size_t size) 
{
   struct block *curr = FreeList;

#if defined FIT && FIT == 0
   /* First fit */
   while (curr && !(curr->free && curr->size >= size)) 
   {
      *last = curr;
      curr  = curr->next;
   }
   num_reuses++;
#endif

#if defined BEST && BEST == 0
   /* Best Fit */
   /* Use First Fit to find initial size differences between first free
      block and size */
   while (curr && !(curr->free && curr->size >= size))
   {
      *last = curr;
      curr = curr->next;
   }
   if (!curr)
   {
      return curr;
   }
   /* Variable to keep track of the size differences */
   size_t size_diff = curr->size - size;
   /* Move curr to the next link */
   *last = curr;
   curr = curr->next;
   /* Iterate through the rest of the linked list to find the smallest
      size differences of free block */
   while ( curr != NULL)
   {
      if (curr->free && curr->size >= size && (curr->size - size) < size_diff)
      {
         *last = curr;
         size_diff = curr->size - size;
      }
      curr = curr->next;
   }
   num_reuses++;
   return *last;
#endif

#if defined WORST && WORST == 0
   /* Worst Fit */
   /* Use First Fit to find initial size differences between first free
      block and size */
   while (curr && !(curr->free && curr->size >= size))
   {
      *last = curr;
      curr = curr->next;
   }
   /* Return curr if it's empty */
   if (!curr)
   {
      return curr;
   }
   /* Variable to keep track of the size differences */
   size_t size_diff = curr->size - size;
   /* Move curr to the next link */
   *last = curr;
   curr = curr->next;
   /* Iterate through the rest of the linked list to find the smallest
      size differences of free block */
   while ( curr != NULL)
   {
      if (curr->free && curr->size >= size && (curr->size - size) > size_diff)
      {
         *last = curr;
         size_diff = curr->size - size;
      }
      curr = curr->next;
   }
   num_reuses++;
   return *last;
#endif

#if defined NEXT && NEXT == 0
   /* variable to keep track of original starting spot
      and also a stopping point when looping through the block list
       from the beginning */
   struct block *original = nextFit;
   curr = nextFit;
   /* When curr starts with NULL */
   if (curr == NULL)
   {
      curr = FreeList;
      while (curr != NULL && !(curr->free && curr->size >= size))
      {
         *last = curr;
         curr = curr->next;
      }
   }
   /* When curr starts somewhere in the middle */
   else
   {
      /* Find free block until end of list */
      while (curr && !(curr->free && curr->size >= size)) 
      {
         *last = curr;
         curr  = curr->next;
      }
      /* Start from the beginning all the way to original check point */
      if (curr == NULL)
      {
         curr = FreeList;
         while (curr && !(curr->free && curr->size >= size)) 
         {
            *last = curr;
            curr = curr->next;
         }
         if (curr == original)
         {
            curr = NULL;
         }
      }
   }
   nextFit = curr;
   num_reuses++; 

#endif
   return curr;
}

/*
 * \brief growheap
 *
 * Given a requested size of memory, use sbrk() to dynamically 
 * increase the data segment of the calling process.  Updates
 * the free list with the newly allocated memory.
 *
 * \param last tail of the free block list
 * \param size size in bytes to request from the OS
 *
 * \return returns the newly allocated block of NULL if failed
 */
struct block *growHeap(struct block *last, size_t size) 
{
   /* Request more space from OS */
   struct block *curr = (struct block *)sbrk(0);
   struct block *prev = (struct block *)sbrk(sizeof(struct block) + size);

   assert(curr == prev);

   /* OS allocation failed */
   if (curr == (struct block *)-1) 
   {
      return NULL;
   }

   /* Update FreeList if not set */
   if (FreeList == NULL) 
   {
      FreeList = curr;
   }

   /* Attach new block to prev block */
   if (last) 
   {
      last->next = curr;
   }

   /* Update block metadata */
   curr->size = size;
   curr->next = NULL;
   curr->free = false;
   num_grows++;
   num_blocks++;
   max_heap += size;
   return curr;
}

/*
 * \brief malloc
 *
 * finds a free block of heap memory for the calling process.
 * if there is no free block that satisfies the request then grows the 
 * heap and returns a new block
 *
 * \param size size of the requested memory in bytes
 *
 * \return returns the requested memory allocation to the calling process 
 * or NULL if failed
 */
void *malloc(size_t size) 
{
   num_requested += size;
   if( atexit_registered == 0 )
   {
      atexit_registered = 1;
      atexit( printStatistics );
   }

   /* Align to multiple of 4 */
   size = ALIGN4(size);

   /* Handle 0 size */
   if (size == 0) 
   {
      return NULL;
   }

   /* Look for free block */
   struct block *last = FreeList;
   struct block *next = findFreeBlock(&last, size);
   /* Check if there's at least 1 free block to use
      to manage any used block with extra free space */
   if (next != NULL && next->size > size)
   {
      struct block *new = FreeList;
      
      while (new != NULL )
      {
         if (new->free == true && new != next)
         {
            new->size += (next->size - size);
            next->size = size;
            num_splits++;
            break;
         }
         new = new->next;
      }       
      if (new == NULL)
      {
         struct block *freeBlock = growHeap(last, next->size - size);
         next->size = size;
         freeBlock->free = true;
         num_splits++;
      }
   }
   /* Could not find free block, so grow heap */
   if (next == NULL) 
   {
      next = growHeap(last, size);
   }

   /* Could not find free block or grow heap, so just return NULL */
   if (next == NULL) 
   {
      return NULL;
   }
   
   /* Mark block as in use */
   next->free = false;

   /* Return data address associated with block */
   num_mallocs++;
   return BLOCK_DATA(next);
}
void *calloc(size_t nmemb, size_t size)
{
  //calloc allocates multiple block of memory
  //malloc allocate one contiguous block
  //so we don't know if you should combine them into total?
  
  //calculate total size need to allocate 
  size_t total_size = nmemb * size;
	//allocate the space for request size
  void *next = malloc(total_size);
	
	if (!next) return NULL;
	
	return memset(next, 0, total_size);
}

void *realloc(void *ptr, size_t size)
{
   num_requested += size;
   if( atexit_registered == 0 )
   {
      atexit_registered = 1;
      atexit( printStatistics );
   }

   /* Align to multiple of 4 */
   size = ALIGN4(size);

   /* Handle 0 size */
   if (size == 0) 
   {
      return NULL;
   }
   /* expand or reduce block */
   //struct block *last = FreeList;
   struct block *next = ptr;
    //if request size smaller than the current ptr size
    //Question: what should we do with the data that is left out? truncate? free?
    if (next != NULL && next->size > size)
    {
      next->size = size;
    }
    //if request size larger than the current ptr size
    if (next != NULL && next->size < size)
    {
      /* Look for free block */
      struct block *last = FreeList;
      struct block *newblock = findFreeBlock(&last, size);
      newblock = ptr;
      newblock = next->next;
      next = newblock;
    }
   /* Could not find free block or grow heap, so just return NULL */
   if (next == NULL) 
   {
      return NULL;
   }

   /* Return data address associated with block */

   return BLOCK_DATA(next);
}
/*
 * \brief free
 *
 * frees the memory block pointed to by pointer. if the block is adjacent
 * to another block then coalesces (combines) them
 *
 * \param ptr the heap memory to free
 *
 * \return none
 */
void free(void *ptr) 
{
   if (ptr == NULL) 
   {
      return;
   }

   /* Make block as free */
   struct block *curr = BLOCK_HEADER(ptr);
   assert(curr->free == 0);
   curr->free = true;
   num_frees++;
   /* Traverse through all block and coalesce any block needed */
   struct block *join = FreeList;
   while (join != NULL)
   {
      if (join->free == true && join->next != NULL && join->next->free == true)
      { 
         join->size += join->next->size;
         if (join->next->next == NULL)
         {
            join->next = NULL;
         }
         else
         {
            join->next = join->next->next;
         }
         num_coalesces++;
      }
      join = join->next;
   }
}