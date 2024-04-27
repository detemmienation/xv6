#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[]) {
  
   test_free();
   malloc(1024); 

  exit(0);
}
