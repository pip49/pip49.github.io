
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// URL 디코딩 함수 선언
void url_decode(char *dest, const char *src);

int main(void) {
    char *query_string = getenv("QUERY_STRING");
    FILE *fp;

    // 리디렉션할 URL을 저장할 변수
    char redirect_url[1024] = "";
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
    
    // 닉네임이 있으면 닉네임이 포함된 URL로, 없으면 기본 URL로 리디렉션
    if (strlen(username) > 0) {
        snprintf(redirect_url, sizeof(redirect_url), "http://125.251.58.88:49494/chat.html?username=%s", username);
    } else {
        snprintf(redirect_url, sizeof(redirect_url), "http://125.251.58.88:49494/chat.html");
    }

    // HTTP 헤더: Content-Type과 동적으로 생성된 Location 헤더를 출력
    printf("Content-Type: text/html\n");
    printf("Location: %s\n\n", redirect_url);

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
