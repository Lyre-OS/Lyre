#include <stdio.h>
#include <stdint.h>

int main() {
    FILE *f = fopen("/dev/nvme0n1", "r");
    char start[7];
    fread(start, 6, 1, f);
    printf("%s\n", start);
    fclose(f);
    return 0;
}
