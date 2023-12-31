/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);


/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

    struct container *container = (struct container *)page->uninit.aux;
    file_page->file = container->file;
    file_page->ofs = container->ofs;
    file_page->read_bytes = container->read_bytes;
    file_page->zero_bytes = container->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	if (page == NULL) {
		return false;
	}
	
	struct container *aux = (struct container *)page->uninit.aux;
	
	struct file *file = aux->file;
	off_t offset = aux->ofs;
	size_t page_read_bytes = aux->read_bytes;
	size_t page_zero_bytes = aux->zero_bytes;
	
	file_seek(file, offset);
	
	if (file_read(file, kva, page_read_bytes) != (int)page_read_bytes) {
		return false;
	}
	
	memset(kva + page_read_bytes, 0, page_zero_bytes);
	
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	if (page == NULL) {
		return false;
	}

    struct container *aux = (struct container *)page->uninit.aux;

    if (pml4_is_dirty(thread_current()->pml4, page->va)) {
    
		lock_acquire(&filesys_lock);
		file_write_at(aux->file, page->va, aux->read_bytes, aux->ofs);
		lock_release(&filesys_lock);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
    pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    free(page->frame);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
    struct file *f = file_reopen(file);
    void *initial_addr = addr;
	while (length > 0) {
		
		if (spt_find_page(&thread_current()->spt, addr)) {
			undo_mmap(initial_addr, addr);
		}
		
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct container *aux = (struct container *)malloc(sizeof (struct container));
		aux->file = file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) {
			free(aux);
			undo_mmap(initial_addr, addr);
		}
		length -= page_read_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

    return initial_addr;			
}

void *undo_mmap(void *initial_addr, void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;
	
	while (initial_addr < addr) {
		page = spt_find_page(spt, addr);
		spt_remove_page(spt, page);
		initial_addr += PGSIZE;
	}
	
	return NULL;
}


/* Do the munmap */
void
do_munmap (void *addr) {
 	struct thread *curr = thread_current();
	struct page *page;

	if ((page = spt_find_page(&curr->spt, addr)) == NULL) {
		return;
	}

	struct file *file = ((struct container *)page->uninit.aux)->file;

	if (!file) {
		return;
	}

	while (page != NULL && page_get_type(page) == VM_FILE) {
		if (pml4_is_dirty(curr->pml4, page->va))
		{
			struct container *aux = page->uninit.aux;
			
			lock_acquire(&filesys_lock);
			file_write_at(aux->file, addr, aux->read_bytes, aux->ofs);
			lock_release(&filesys_lock);
			pml4_set_dirty(curr->pml4, page->va, false);
			
		}

		pml4_clear_page(&curr->pml4, addr);
		addr += PGSIZE;
		page = spt_find_page(&curr->spt, addr);
	}
}
