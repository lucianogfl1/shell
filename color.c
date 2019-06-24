#include <stdio.h>
#include "color.h"

void set_color(const char* color) {
    printf("%s", color);
    fflush(stdout);
}
