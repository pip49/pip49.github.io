
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 쉘 명령어의 출력을 HTML로 출력하는 함수
void execute_shell_and_print(const char* command) {
    FILE *fp;
    char path[1024];
    fp = popen(command, "r");
    if (fp == NULL) {
        printf("<p style=\"color: red;\">쉘 명령어를 실행할 수 없습니다.</p>\n");
        return;
    }
    printf("<pre>\n");
    while (fgets(path, sizeof(path) - 1, fp) != NULL) {
        printf("%s", path);
    }
    printf("</pre>\n");
    pclose(fp);
}

int main(void) {
    // Content-Type 헤더는 필수
    printf("Content-Type: text/html\n\n");

    // 원하는 쉘 스크립트 실행
    execute_shell_and_print("sh /var/www/html/scripts/new.sh");

    return 0;
}
