
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *fp;
    char buffer[1024];

    printf("Content-Type: text/html\n\n");
    printf("<!DOCTYPE html>\n");
    printf("<html lang='ko'>\n");
    printf("<head>\n");
    printf("<meta charset='UTF-8'>\n");
    printf("<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n");
    printf("<title>세련된 CGI 채팅</title>\n");
    printf("<style>\n");
    printf("  body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f0f2f5; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }\n");
    printf("  .chat-container { background-color: #ffffff; border-radius: 10px; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.1); width: 100%; max-width: 500px; padding: 20px; box-sizing: border-box; }\n");
    printf("  h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 1.8em; }\n");
    printf("  #chatbox { border: 1px solid #e0e0e0; height: 350px; overflow-y: auto; padding: 15px; margin-bottom: 20px; background-color: #fbfbfb; border-radius: 5px; }\n");
    printf("  .message-item { margin-bottom: 10px; padding: 8px 12px; border-radius: 15px; background-color: #e6f7ff; color: #333; max-width: 80%; word-wrap: break-word; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }\n");
    printf("  .message-item strong { color: #007bff; font-weight: 600; }\n");
    printf("  form { display: flex; flex-wrap: wrap; gap: 10px; }\n");
    printf("  form input[type='text'] { flex: 1; padding: 12px; border: 1px solid #ccc; border-radius: 5px; font-size: 1em; }\n");
    printf("  form input[name='username'] { flex-basis: 30%%; }\n");
    printf("  form input[name='message'] { flex-basis: 65%%; }\n");
    printf("  form input[type='submit'] { background-color: #007bff; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; font-size: 1em; transition: background-color 0.2s ease; flex-grow: 1; }\n");
    printf("  form input[type='submit']:hover { background-color: #0056b3; }\n");
    printf("</style>\n");
    printf("</head>\n");
    printf("<body>\n");
    printf("<div class='chat-container'>\n");
    printf("<h1>세련된 CGI 채팅</h1>\n");
    printf("<div id='chatbox'>\n");

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

    printf("</div>\n");
    printf("<form action='/cgi-bin/write.cgi' method='GET'>\n");
    printf("  <input type='text' name='username' placeholder='닉네임' required>\n");
    printf("  <input type='text' name='message' placeholder='메시지 입력' required>\n");
    printf("  <input type='submit' value='보내기'>\n");
    printf("</form>\n");
    printf("</div>\n");
    printf("</body>\n");
    printf("</html>\n");

    return 0;
}
