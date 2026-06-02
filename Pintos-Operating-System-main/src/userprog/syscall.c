#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void sys_exit (int status);

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


/* Terminate the current process with status STATUS. */
static void
terminate_process (int status) 
{
  struct thread *cur = thread_current ();
  cur->thread_exit_status = status;
  printf ("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}

/* To make sure all memory addresses from buffer are valid user addresses. */
static void validate_user_memory(const void *usr_buffer, unsigned length) {
  const uint8_t *buf_ptr = usr_buffer;

  unsigned offset = 0;
  while (offset < length) {
    const void *curr_addr = (const void *)(buf_ptr + offset);

    if (!is_user_vaddr(curr_addr) || 
        pagedir_get_page(thread_current()->pagedir, curr_addr) == NULL) {
      terminate_process(-1);
    }

    offset++;
  }
}


/* Ensure that the given null-terminated string resides entirely in user-accessible memory space. */
static void verify__user_string(const char *s) {
    const char *cursor = s;

    /* Continue as long as the address is valid*/
    while (is_user_vaddr(cursor) && 
           pagedir_get_page(thread_current()->pagedir, cursor) != NULL) {
        /* Stop when null terminating byte reached */
        if (*cursor == '\0') {
            return;
        }
        cursor++;
    }

    /* If we exit the loop, an invalid address is encountered */
    terminate_process(-1);
}


/* Verify that the address points to a valid user-space memory */
static void assert_user_pointer(const void *pointer) {
    struct thread *t = thread_current();
    
    /* Check that pointer is within user address range */
    if (!is_user_vaddr(pointer)) {
        terminate_process(-1);
    }

    /* Ensure the pointer maps */
    if (pagedir_get_page(t->pagedir, pointer) == NULL) {
        terminate_process(-1);
    }
}


/* Extract a 4 byte integer argument from the user stack */
static int retrieve_integer_argument(struct intr_frame *frame, int byte_offset) {

    char *arg_addr = (char *)frame->esp + byte_offset;
    
    /* Validate each byte of the 4-byte integer */
    for (size_t i = 0; i < sizeof(int); i++) {
        assert_user_pointer(arg_addr + i);
    }


    int value;
    memcpy(&value, arg_addr, sizeof(value));
    return value;
}


/* Retrieve a pointer argument from the user stack at the particular offset. */
static void *load_user_pointer(struct intr_frame *frame, int byte_offset) {

    char *ptr_addr = (char *)frame->esp + byte_offset;

    /* Validate each byte of the pointer itself */
    for (size_t i = 0; i < sizeof(void *); i++) {
        assert_user_pointer(ptr_addr + i);
    }

    /* copy the pointer value */
    void *result;
    memcpy(&result, ptr_addr, sizeof(result));
    return result;
}

static void syscall_handler(struct intr_frame *frame) {
  assert_user_pointer(frame->esp);
  int call_number = retrieve_integer_argument(frame, 0);

  switch (call_number) {

    /*shut the machine off immediately*/
    case SYS_HALT:
      shutdown_power_off();
      break;

    /*terminate this process with status, and tell the parent.*/
    case SYS_EXIT: {
      int exit_code = retrieve_integer_argument(frame, 4);
      terminate_process(exit_code);
      break;
    }

    /*start new process running the given command line*/
    case SYS_EXEC: {
      char *command = load_user_pointer(frame, 4);
      verify__user_string(command);
      frame->eax = process_execute(command);
      break;
    }

    /*wait for a child process to terminate and return thee exit status*/
    case SYS_WAIT: {
      tid_t tid = retrieve_integer_argument(frame, 4);
      frame->eax = process_wait(tid);
      break;
    }

    /*create a new file*/
    case SYS_CREATE: {
      char *file_name = load_user_pointer(frame, 4);
      verify__user_string(file_name);
      unsigned size_ = retrieve_integer_argument(frame, 8);

      lock_acquire(&fs_access_lock);

      frame->eax = filesys_create(file_name, size_);
      lock_release(&fs_access_lock);
      break;
    }

    /*delete the file from system*/
    case SYS_REMOVE: {

      char *file_name = load_user_pointer(frame, 4);
      verify__user_string(file_name);
      
      lock_acquire(&fs_access_lock);

      frame->eax = filesys_remove(file_name);
      lock_release(&fs_access_lock);
      break;
    }

    /*open the file and return a new file descriptor*/
    case SYS_OPEN: {
      char *file_path = load_user_pointer(frame, 4);
      verify__user_string(file_path);

      lock_acquire(&fs_access_lock);

      struct file *opened_file = filesys_open(file_path);

      if (opened_file != NULL) {
        struct thread *t = thread_current();

        if (t->executable_file != NULL && 
        opened_file == t->executable_file) {
          file_close(opened_file);
          frame->eax = -1;
        } 
        else {
          frame->eax = allocate_fd(opened_file);
        }
      } 
      else {
        frame->eax = -1;
      }
      lock_release(&fs_access_lock);
      break;
    }

    /*report the open filesize*/
    case SYS_FILESIZE: {
      int fd = retrieve_integer_argument(frame, 4);

      lock_acquire(&fs_access_lock);

      struct file *fptr = fetch_file_by_fd(fd);
      frame->eax = (fptr == NULL ? -1 : file_length(fptr));

      lock_release(&fs_access_lock);
      break;
    }

    /*readbytes from file_desc*/
    case SYS_READ: {
      int fd = retrieve_integer_argument(frame, 4);
      void *buf = load_user_pointer(frame, 8);
      unsigned size = retrieve_integer_argument(frame, 12);

      assert_user_pointer(buf);
      if (size > 0)
        validate_user_memory(buf, size);

      if (fd == STDIN_FILENO) {
        for (unsigned i = 0; i < size; i++)
          ((char *)buf)[i] = input_getc();
        frame->eax = size;
      } 
        else {

        lock_acquire(&fs_access_lock);

        struct file *fptr = fetch_file_by_fd(fd);
        frame->eax = (fptr == NULL ? -1 : file_read(fptr, buf, size));

        lock_release(&fs_access_lock);
      }
      break;
    }

    /*write bytes to a file descriptor*/
    case SYS_WRITE: {

    int file_desc = retrieve_integer_argument(frame, 4);
    void *buf = load_user_pointer(frame, 8);
    unsigned size = retrieve_integer_argument(frame, 12);

    /* Validate buffer region byte-by-byte */
    for (unsigned i = 0; i < size; i++) {
        assert_user_pointer((char *)buf + i);
    }

    if (file_desc == STDOUT_FILENO) {
        putbuf(buf, size);
        frame->eax = size;
    } else {
        lock_acquire(&fs_access_lock);

        struct file *fptr = fetch_file_by_fd(file_desc);
        int result = -1;
        if (fptr) {
            result = file_write(fptr, buf, size);
        }
        lock_release(&fs_access_lock);

        frame->eax = result;
    }
    break;
}

    /*change the next byte to read/write in an open file*/
    case SYS_SEEK: {
      int file_desc = retrieve_integer_argument(frame, 4);
      unsigned pos = retrieve_integer_argument(frame, 8);

      lock_acquire(&fs_access_lock);

      struct file *fptr = fetch_file_by_fd(file_desc);
      if (fptr != NULL) file_seek(fptr, pos);

      lock_release(&fs_access_lock);

      break;
    }

    case SYS_TELL: {//report current position
      int file_desc = retrieve_integer_argument(frame, 4);

      lock_acquire(&fs_access_lock);

      struct file *fptr = fetch_file_by_fd(file_desc);
      frame->eax = (fptr == NULL ? -1 : file_tell(fptr));

      lock_release(&fs_access_lock);

      break;
    }

    /* close an open file descriptor.*/
    case SYS_CLOSE: {
      int file_desc = retrieve_integer_argument(frame, 4);

      lock_acquire(&fs_access_lock);
      close_fd(file_desc);
      
      lock_release(&fs_access_lock);
      break;
    }

    default:
      /*Unknown syscall then terminate the process*/
      terminate_process(-1);
  }
}

static void
sys_exit (int status) 
{
  struct thread *curr_thread = thread_current ();
  curr_thread->thread_exit_status = status;
  printf ("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}
