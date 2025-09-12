
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *fp;
    char buffer[1024];

    // HTTP 헤더를 정확히 출력해야 합니다.
    printf("Content-Type: text/html\n\n");

    fp = fopen("/var/www/html/chat.log", "r");
    if (fp != NULL) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            char *colon = strchr(buffer, ':');
            if (colon) {
                *colon = '\0';
                printf("  <div class='message-item'><strong>%s</strong>%s</div>\n", buffer, colon + 1);
                *colon = ':';
            } else {
                printf("  <div class='message-item'>%s</div>\n", buffer);
            }
        }
        fclose(fp);
    } else {
        printf("  <div class='message-item'>메시지가 없습니다.</div>\n");
    }

    return 0;
}
