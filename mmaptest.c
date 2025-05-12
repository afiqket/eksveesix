#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define PGSIZE 4096
#define MAP_FAILED ((void *) -1)

#define MAP_PROT_READ  0x00000001
#define MAP_PROT_WRITE 0x00000002

int
main(void)
{
  char *filename = "alice.txt"; // Change your file here
  int length = 4096;
  int offset = 0;

  // Open file
  printf(1, "1. Try to open file\n");
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    printf(1, "open failed\n");
    exit();
  }
  else
  {
    printf(1, "open success\n\n");
  }

  // mmap
  printf(1, "2. Try to mmap the file %s with offset %d and length %d\n", filename, offset, length);
  char *mapped = (char *)mmap(fd, offset, length, MAP_PROT_WRITE);
  if (mapped == MAP_FAILED) {
    printf(1, "mmap failed\n");
    close(fd);
    exit();
  }
  else
  {
    printf(1, "mmap succesful. mmap address: 0x%x\n", (int)mapped);
  }

  // Read mmap contents and print
  printf(1, "\n3. Try to read: mapped[0] to mapped[69]:\n");
  for (int i = 0; i < 70; i++)
  {
    write(1, &(mapped[i]), 1);
  }

  // Write to mmap
  printf(1, "\n4. Try to write: mapped[0] to mapped[4] = \"Bobby\"\n");
  mapped[0] = 'B';
  mapped[1] = 'o';
  mapped[2] = 'b';
  mapped[3] = 'b';
  mapped[4] = 'y';
  printf(1, "Writes success\n");

  // Read
  printf(1, "\n5. Try to read after write: mapped[0] to mapped[69]\n");
  for (int i = 0; i < 70; i++)
  {
    write(1, &(mapped[i]), 1);
  }

  // Unmap
  printf(1, "\n6. Try to unmap\n");
  if (munmap((void*)mapped, length) == 0)
    printf(1, "unmap success\n");
  else
    printf(1, "unmap failure\n");

  close(fd);
  exit();
}