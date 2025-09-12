
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("Content-type: text/plain\n\n");
    system("/var/www/html/scripts/place.sh");
    return 0;
}
