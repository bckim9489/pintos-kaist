/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "vm/inspect.h"

struct list frame_table;
struct list_elem *start;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
    list_init(&frame_table);
	start = list_begin(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		
        struct page *p = (struct page *)malloc(sizeof(struct page));

        //bool (*page_initializer)(struct page *, enum vm_type, void *);

		typedef bool(*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        switch (VM_TYPE(type))
        {
        case VM_ANON:
            initializer = anon_initializer;
            break;
        case VM_FILE:
            initializer = file_backed_initializer;
            break;
        }
		
        uninit_new(p, upage, init, type, aux, initializer);
		
        p->writable = writable;
		
		/* TODO: Insert the page into the spt. */
        return spt_insert_page(spt, p);
    }
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/*
	struct page *dummy_page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *dummy_elem;
	dummy_page->va = pg_round_down(va);
	dummy_elem = hash_find(&spt->spt_hash, &dummy_page->hash_elem);
	
	free(dummy_page);
	
	if(!dummy_elem)
		return NULL; 

	return hash_entry(dummy_elem, struct page, hash_elem);
	*/
    struct page* page = (struct page*)malloc(sizeof(struct page)); // dummy page 생성
    struct hash_elem *e;

    page->va = pg_round_down(va); 
    e = hash_find(&spt->spt_hash, &page->hash_elem); 

    free(page);
    return e != NULL ? hash_entry(e,struct page,hash_elem) : NULL;
}

bool insert_page(struct hash *pages, struct page *p){
    if(!hash_insert(pages, &p->hash_elem))
        return true;
    else
        return false;
}
bool delete_page(struct hash *pages, struct page *p){
    if(!hash_delete(pages, &p->hash_elem))
        return true;
    else
        return false;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	/* TODO: Fill this function. */
	
	//return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL ? true : false;
	return insert_page(&spt->spt_hash, page);
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
    hash_delete(&spt->spt_hash, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct thread *current_thread = thread_current();
	struct list_elem *search_elem = start;
	
	for (start = search_elem; start != list_end(&frame_table); start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		
		if (pml4_is_accessed(current_thread->pml4, victim->page->va)) {
			pml4_set_accessed(current_thread->pml4, victim->page->va, 0);
		} else {
			return victim;
		}
	}
	
	for (start = list_begin(&frame_table); start != search_elem; start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		
		if (pml4_is_accessed(current_thread->pml4, victim->page->va)) {
			pml4_set_accessed(current_thread->pml4, victim->page->va, 0);
		} else {
			return victim;
		}
	}
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
    swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
    /* TODO: Fill this function. */
    frame->kva = palloc_get_page(PAL_USER);                    
    if (frame->kva == NULL){
        frame = vm_evict_frame();
		frame->page = NULL;
		return frame;
    }
    list_push_back (&frame_table, &frame->frame_elem);
	frame->page = NULL;

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr UNUSED) {
    if (vm_alloc_page(VM_MARKER_0 | VM_ANON, addr, true)) {
		thread_current()->stack_bottom -= PGSIZE;
		return true;
	}

	return false;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    if (addr == NULL)
        return false;

    if (!addr || is_kernel_vaddr(addr) || !not_present)
	{
		return false;
	}

    page = spt_find_page(spt, addr);
    if (!page) {
		if (addr >= USER_STACK - (1 << 20) && USER_STACK > addr && addr >= f->rsp - 8 && addr < thread_current()->stack_bottom) {
			void *fpage = thread_current()->stack_bottom - PGSIZE;
			if (vm_stack_growth(fpage)) {
				page = spt_find_page(spt, fpage);
				return true;
			}
			else {
				return false;
			}
		} else {
			return false;
		}
	}
	return false;
	//return vm_do_claim_page(page);

}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
    
    page = spt_find_page(&thread_current()->spt, va);
    if (page == NULL)
        return false;
    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	/*
    if (!page || !is_user_vaddr(page->va)) {
		return false;
	}
    struct frame *frame = vm_get_frame ();
    frame->page = page;
    page->frame = frame;

   	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
		return false;
	}
	return swap_in(page, frame->kva);
	*/
	struct thread *t = thread_current ();
    struct frame *frame = vm_get_frame ();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    if(pml4_get_page (t->pml4, page->va) == NULL&& pml4_set_page (t->pml4, page->va, frame->kva, page->writable)){
        return swap_in(page, frame->kva);
    }
    return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

bool 
page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return a->va < b->va;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {

	struct hash_iterator i;
    hash_first(&i, &src->spt_hash);
    while (hash_next(&i))
    {
        struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
        enum vm_type type = src_page->operations->type;
        void *upage = src_page->va;
        bool writable = src_page->writable;


        if (type == VM_UNINIT){
            vm_initializer *init = src_page->uninit.init;
            void *aux = src_page->uninit.aux;
            vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
            continue;
        }

        if (type == VM_FILE)
        {
            struct container *file_aux = malloc(sizeof(struct container));
            file_aux->file = src_page->file.file;
            file_aux->ofs = src_page->file.ofs;
            file_aux->read_bytes = src_page->file.read_bytes;
            file_aux->zero_bytes = src_page->file.zero_bytes;
            if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
                return false;
            struct page *file_page = spt_find_page(dst, upage);
            file_backed_initializer(file_page, type, NULL);
            file_page->frame = src_page->frame;
            pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva, src_page->writable);
            continue;
        }

        if (!vm_alloc_page(type, upage, writable))
            return false;

        if (!vm_claim_page(upage))
            return false;

        struct page *dst_page = spt_find_page(dst, upage);
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }

    return true;
} 

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;

    hash_first (&i, &spt->spt_hash);
    while (hash_next (&i)) {
        struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);

        if (page->operations->type == VM_FILE) {
            do_munmap(page->va);
        }
    }
	//hash_clear(&spt->spt_hash, spt_destructor);
}

void spt_destructor(struct hash_elem *e, void *aux)
{
    struct page *page = hash_entry(e, struct page, hash_elem);
    vm_dealloc_page(page);
}