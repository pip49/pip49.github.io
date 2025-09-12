
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// 쉘 스크립트의 경로를 정의합니다.
// 스크립트 파일은 실행 권한(chmod +x)이 있어야 합니다.
#define SCRIPT_PATH "/var/www/html/scripts/donation.sh"

int main(void) {
    // CGI 환경에 필수적인 HTTP 헤더를 출력합니다.
    printf("Content-Type: text/html\n\n");
    printf("<!DOCTYPE html>\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("    <meta charset=\"UTF-8\">\n");
    printf("    <title>후원금 스크립트 실행</title>\n");
    printf("</head>\n");
    printf("<body>\n");

    // 쉘 스크립트를 실행하고 그 결과를 pre 태그로 감싸서 출력합니다.
    // 이렇게 하면 JavaScript에서 결과를 쉽게 파싱할 수 있습니다.
    FILE *fp;
    char output[256];

    printf("<pre>\n");

    fp = popen(SCRIPT_PATH, "r");
    if (fp == NULL) {
        printf("오류: 스크립트를 실행할 수 없습니다.\n");
        return 1;
    }

    while (fgets(output, sizeof(output), fp) != NULL) {
        printf("%s", output);
    }
    printf("</pre>\n");

    pclose(fp);

    printf("</body>\n");
    printf("</html>\n");
    
    return 0;
}
