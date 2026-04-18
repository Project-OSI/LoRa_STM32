#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int expected = 2;
    int actual = 1 + 1;
    if (expected != actual) {
        fprintf(stderr, "FAIL: expected %d got %d\n", expected, actual);
        return 1;
    }
    puts("PASS harness_smoke");
    return 0;
}
