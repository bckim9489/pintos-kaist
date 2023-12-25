#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/off_t.h"


tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
struct container{
    struct file *file;
    off_t ofs;
    size_t read_bytes;
    size_t zero_bytes;
};
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
struct file *process_get_file(int fd);
int process_add_file(struct file *file);
bool lazy_load_segment (struct page *page, void *aux);
#define MAX_FDT 128
#endif /* userprog/process.h */
