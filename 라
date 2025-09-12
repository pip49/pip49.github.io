
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void url_decode(char *dest, const char *src);

int main(void) {
    char *query_string = getenv("QUERY_STRING");
    FILE *fp;

    char username[256] = "";
    char message[256] = "";
    
    if (query_string != NULL) {
        char decoded_query[512] = "";
        
        url_decode(decoded_query, query_string);

        sscanf(decoded_query, "username=%[^&]&message=%[^\n]", username, message);

        fp = fopen("/var/www/html/chat.log", "a");
        if (fp != NULL) {
            fprintf(fp, "%s: %s\n", username, message);
            fclose(fp);
        }
    }
    
    printf("Content-Type: text/html\n");
    printf("Cache-Control: no-cache, no-store, must-revalidate\n");
    printf("Pragma: no-cache\n");
    printf("Expires: 0\n\n");
    
    printf("<!DOCTYPE html>\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("    <title>Message Sent</title>\n");
    printf("    <script>\n");
    printf("        window.parent.location.reload();\n");
    printf("    </script>\n");
    printf("</head>\n");
    printf("<body>\n");
    printf("</body>\n");
    printf("</html>\n");

    return 0;
}

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
