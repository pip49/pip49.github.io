
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep 함수를 위해 추가

// URL 디코딩 함수 선언
void url_decode(char *dest, const char *src);

int main(void) {
    char *query_string = getenv("QUERY_STRING");
    FILE *fp;

    char username[256] = "";
    char message[256] = "";
    
    // 쿼리 문자열이 있을 경우 처리
    if (query_string != NULL) {
        char decoded_query[512] = "";
        
        // 쿼리 문자열을 디코딩
        url_decode(decoded_query, query_string);

        // 디코딩된 문자열에서 닉네임과 메시지를 파싱
        sscanf(decoded_query, "username=%[^&]&message=%[^\n]", username, message);

        // chat.log 파일에 메시지 추가 (a: append 모드)
        fp = fopen("/var/www/html/chat.log", "a");
        if (fp != NULL) {
            fprintf(fp, "%s: %s\n", username, message);
            fclose(fp);
        }
    }
    
    // HTTP 헤더: Content-Type과 캐시 방지 헤더를 출력
    printf("Content-Type: text/html\n");
    printf("Cache-Control: no-cache, no-store, must-revalidate\n");
    printf("Pragma: no-cache\n");
    printf("Expires: 0\n\n");
    
    // 메시지 처리 후 부모 창을 새로고침하는 HTML과 자바스크립트를 출력
    printf("<!DOCTYPE html>\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("    <title>Message Sent</title>\n");
    printf("    <script>\n");
    printf("        // 부모 창을 새로고침합니다.\n");
    printf("        window.parent.location.reload();\n");
    printf("    </script>\n");
    printf("</head>\n");
    printf("<body>\n");
    printf("</body>\n");
    printf("</html>\n");

    return 0;
}

// URL 디코딩 함수
void url_decode(char *dest, const char *src) {
    int i = 0, j = 0;
    while (src[i] != '\0') {
        if (src[i] == '%') {
            char hex[3];
            hex[0] = src[i+1];
            hex[1] = src[i+2];
            hex[2] = '\0';
            dest[j] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dest[j] = ' ';
            i++;
        } else {
            dest[j] = src[i];
            i++;
        }
        j++;
    }
    dest[j] = '\0';
}
