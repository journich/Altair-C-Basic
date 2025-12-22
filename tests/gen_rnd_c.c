/*
 * Generate RND values from C implementation for comparison
 */
#include <stdio.h>
#include <stdlib.h>
#include "basic/basic.h"
#include "basic/mbf.h"

int main(int argc, char *argv[]) {
    int count = 100;
    if (argc > 1) {
        count = atoi(argv[1]);
    }

    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);

    for (int i = 0; i < count; i++) {
        mbf_t value = rnd_next(&state, one);
        double d = mbf_to_double(value);
        printf("%.6g\n", d);
    }

    return 0;
}
