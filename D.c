
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <hiredis/hiredis.h> // ★ 1. hiredis 라이브러리 추가

// =======================================================
// ★ 2. Redis 동시 접속 제한을 위한 설정 정의
// =======================================================
#define REDIS_HOST "127.0.0.1"
#define REDIS_PORT 6379
#define REDIS_KEY "cgi:concurrent_users_D" // ★ 서버별로 고유한 키를 사용하세요
#define MAX_USERS 1                      // 허용할 최대 동시 접속 인원 (현재 명으로 설정)
#define EXPIRE_TIME 5                   // ★ 비정상 종료 시 Redis 키의 만료 시간 (초)
#define REDIRECT_URL "/limit_notice.html" // 초과 시 리디렉션할 주소
// #define MAX_WAIT_TIME 10             // CGI 환경 문제 해결을 위해 주석 처리됨 (sleep 미사용)

// =======================================================
// 기존 11.c 파일 설정
// =======================================================
#define COUNTER_FILE "/var/www/html/counter.dat"
#define UNIQUE_VISITORS_FILE "/var/www/html/unique_visitors.txt"
#define ANNOUNCEMENT_FILE "/var/www/html/announcement.txt"
#define UPLOADS_DIR "/var/www/html/uploads/"
#define MAX_CONTENT_SIZE 1024 * 1024


// =======================================================
// Redis 리디렉션 함수 (추가)
// =======================================================
void redirect_to_limit() {
    // 302 리디렉션 HTTP 헤더 출력
    printf("Status: 302 Found\r\n");
    printf("Location: %s\r\n\r\n", REDIRECT_URL);
}


// =======================================================
// 기존 11.c 파일 함수들
// =======================================================

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

void print_announcement_from_file() {
    FILE *fp;
    char *buffer;
    long file_size;

    fp = fopen(ANNOUNCEMENT_FILE, "r");
    if (fp == NULL) {
        printf("<p>아직 공지사항이 없습니다.</p>\n");
        return;
    }

    flock(fileno(fp), LOCK_SH);

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);

    buffer = (char*)malloc(file_size + 1);
    if (buffer == NULL) {
        printf("<p style=\"color: red;\">메모리 할당 오류</p>\n");
        fclose(fp);
        flock(fileno(fp), LOCK_UN);
        return;
    }

    if (fread(buffer, 1, file_size, fp) != file_size) {
        printf("<p style=\"color: red;\">파일 읽기 오류</p>\n");
        free(buffer);
        fclose(fp);
        flock(fileno(fp), LOCK_UN);
        return;
    }
    buffer[file_size] = '\0';

    printf("%s", buffer);

    free(buffer);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
}

const char* get_file_ext(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) return "";
    return dot + 1;
}

void print_uploaded_files() {
    DIR *dir;
    struct dirent *ent;

    dir = opendir(UPLOADS_DIR);
    if (dir == NULL) {
        printf("<p>업로드 폴더를 열 수 없습니다. 권한을 확인해 주세요.</p>\n");
        return;
    }

    printf("<h3>업로드된 파일 목록</h3>\n");
    printf("<div class=\"uploaded-files-box\">\n");

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        const char* ext = get_file_ext(ent->d_name);

        if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 || strcasecmp(ext, "png") == 0 || strcasecmp(ext, "gif") == 0) {
            printf("<p><img src=\"/uploads/%s\" style=\"max-width:100%%;height:auto;\" alt=\"%s\"></p>\n", ent->d_name, ent->d_name);
        } else if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "webm") == 0) {
            printf("<p><video controls style=\"max-width:100%%;height:auto;\"><source src=\"/uploads/%s\" type=\"video/%s\"></video></p>\n", ent->d_name, ext);
        } else {
            printf("<p><a href=\"/uploads/%s\" class=\"download-link\" download=\"%s\" target=\"_blank\">%s</a></p>\n", ent->d_name, ent->d_name, ent->d_name);
        }
    }

    printf("</div>\n");
    closedir(dir);
}


// =======================================================
// ★ 3. main 함수 수정 (Redis 게이트 로직 재정비)
// =======================================================
int main(void) {
    redisContext *c = NULL;
    redisReply *reply = NULL;
    int current_users = 0;
    int redirected = 0; // 리디렉션 여부 플래그

    // ----------------------------------------------------
    // ★ [NEW] Redis 동시 접속 제한 로직 (게이트)
    // ----------------------------------------------------
    // 1. Redis 서버 접속
    c = redisConnect(REDIS_HOST, REDIS_PORT);

    if (c != NULL && !c->err) {
        // 2. 접속 인원 증가 (INCR)
        reply = redisCommand(c, "INCR %s", REDIS_KEY);

        if (reply != NULL && reply->type == REDIS_REPLY_INTEGER) {
            current_users = reply->integer;
            freeReplyObject(reply);
            reply = NULL; // reply 포인터 초기화

            // 3. 인원 제한 확인
            if (current_users > MAX_USERS) {
                // 초과: DECR 후 리디렉션
                redisCommand(c, "DECR %s", REDIS_KEY);
                redirect_to_limit();
                redirected = 1; // 리디렉션 플래그 설정
                goto CLEANUP_AND_EXIT; // 정리 코드로 이동
            }

            // 성공: EXPIRE 설정 (CGI 프로세스가 갑자기 종료되어도 30초 후 자동 해제)
            redisCommand(c, "EXPIRE %s %d", REDIS_KEY, EXPIRE_TIME);

        } else {
            // INCR 실패 시 (Redis 오류) - 안전을 위해 리디렉션
            if (reply) freeReplyObject(reply);
            redirect_to_limit();
            redirected = 1;
            goto CLEANUP_AND_EXIT; // 정리 코드로 이동
        }
    } else {
        // Redis 연결 실패 시 - 안전을 위해 리디렉션
        redirect_to_limit();
        redirected = 1;
        goto CLEANUP_AND_EXIT; // 정리 코드로 이동
    }

    // ----------------------------------------------------
    // [기존 11.c 코드 시작] Redis 게이트를 통과한 경우에만 실행
    // ----------------------------------------------------
    // 만약 리디렉션되지 않았다면 HTML 콘텐츠를 출력합니다.
    if (redirected == 0) {
        printf("Content-Type: text/html\n\n");

        FILE *fp_count;
        FILE *fp_unique;
        int count = 0;
        const char *remote_addr = getenv("REMOTE_ADDR");
        int new_visitor = 1;

        struct stat st;
        time_t now = time(NULL);
        struct tm *today = localtime(&now);

        // 일별 방문자 초기화 로직
        if (stat(UNIQUE_VISITORS_FILE, &st) == 0) {
            struct tm *file_date = localtime(&st.st_mtime);
            if (today->tm_year != file_date->tm_year || today->tm_mon != file_date->tm_mon || today->tm_mday != file_date->tm_mday) {
                remove(UNIQUE_VISITORS_FILE);
                remove(COUNTER_FILE);
            }
        }

        // 중복 방문자 확인 및 카운터 증가 로직
        fp_unique = fopen(UNIQUE_VISITORS_FILE, "r+");
        if (fp_unique == NULL) {
            fp_unique = fopen(UNIQUE_VISITORS_FILE, "w+");
            if (fp_unique == NULL) {
                printf("<h1>Error: Could not open unique visitors file.</h1>\n");
                goto CLEANUP_AND_EXIT;
            }
        }

        if (flock(fileno(fp_unique), LOCK_EX) == -1) {
            fclose(fp_unique);
            printf("<h1>Error: Failed to lock file.</h1>\n");
            goto CLEANUP_AND_EXIT;
        }

        char line[50];
        while (fgets(line, sizeof(line), fp_unique) != NULL) {
            line[strcspn(line, "\n")] = 0;
            if (remote_addr != NULL && strcmp(line, remote_addr) == 0) {
                new_visitor = 0;
                break;
            }
        }

        if (new_visitor) {
            fp_count = fopen(COUNTER_FILE, "r+");
            if (fp_count == NULL) {
                fp_count = fopen(COUNTER_FILE, "w+");
                if (fp_count == NULL) {
                    flock(fileno(fp_unique), LOCK_UN);
                    fclose(fp_unique);
                    printf("<h1>Error: Could not open counter file.</h1>\n");
                    goto CLEANUP_AND_EXIT;
                }
            }
            fscanf(fp_count, "%d", &count);
            count++;
            rewind(fp_count);
            fprintf(fp_count, "%d", count);
            fclose(fp_count);
            fprintf(fp_unique, "%s\n", remote_addr);
        } else {
            fp_count = fopen(COUNTER_FILE, "r");
            if (fp_count != NULL) {
                fscanf(fp_count, "%d", &count);
                fclose(fp_count);
            }
        }

        flock(fileno(fp_unique), LOCK_UN);
        fclose(fp_unique);

        // ******************************************************
        // HTML 출력 시작
        // ******************************************************
        printf("<!DOCTYPE html>\n");
        printf("<html lang=\"ko\">\n");
        printf("<head>\n");
        printf("    <meta charset=\"UTF-8\">\n");
        printf("    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
        printf("    <title>아날로그 사운드의 소울</title>\n");
        // ... (중략: Style 및 HTML 코드) ...

        printf("    <style>\n");
// ★★★ [수정] Jua 폰트 URL 추가 ★★★
printf("        @import url('https://fonts.googleapis.com/css2?family=Noto+Sans+KR:wght@300;400;700&family=Jua&display=swap');\n");
printf("        body { font-family: 'Noto Sans KR', sans-serif; background-color: #f0f0f0; color: #333; line-height: 1.6; margin: 0; padding: 40px 20px; display: flex; flex-direction: column; min-height: 10vh; }\n");
printf("        .header-section { display: flex; align-items: center; justify-content: center; gap: 20px; margin-bottom: 20px; }\n");
printf("        .header-image-main { width: 150px; height: 150px; border-radius: 50%%; object-fit: cover; cursor: pointer; border: 4px solid #00796b; box-shadow: 0 4px 10px rgba(0, 0, 0, 0.2); transition: transform 0.3s ease; }\n");
printf("        .header-image-main:hover { transform: scale(1.05); }\n");
printf("        .header-image-sub { width: 100px; height: 100px; border-radius: 50%%; object-fit: cover; cursor: pointer; border: 3px solid #e65100; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2); transition: transform 0.3s ease; }\n");
printf("        .header-image-sub:hover { transform: scale(1.05); }\n");
printf("        .container { max-width: 800px; padding: 30px; background-color: #ffffff; border-radius: 12px; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1); border: 1px solid #ddd; margin-bottom: 20px; }\n");
printf("        h1 { color: #00796b; font-weight: 700; font-size: 2.5em; margin-bottom: 20px; }\n");
printf("        p { font-size: 1.1em; font-weight: 300; margin-bottom: 20px; }\n");
printf("        .highlight { color: #e65100; font-weight: 700; }\n");
printf("        .list-container { margin-top: 30px; display: flex; justify-content: space-around; flex-wrap: wrap; gap: 20px; }\n");
printf("        .list-item { background-color: #fafafa; padding: 15px; border-radius: 8px; width: 45%%; box-sizing: border-box; border-left: 3px solid #00796b; text-align: left; }\n");
printf("        .list-item h3 { color: #333; margin: 0 0 10px; font-weight: 400; font-size: 1.2em; }\n");
printf("        .list-item ul { list-style: none; padding: 0; margin: 0; }\n");
printf("        .list-item li { margin-bottom: 5px; font-size: 0.95em; }\n");
printf("        .banner-container { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; }\n");
printf("        .banner-container img { width: 150px; height: auto; border-radius: 8px; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1); cursor: pointer; transition: transform 0.3s ease; }\n");
printf("        .banner-container img:hover { transform: scale(1.03); }\n");
printf("        .footer-info { display: flex; justify-content: space-between; align-items: flex-start; margin-top: auto; padding: 20px; background-color: #f0f0f0; border-top: 1px solid #ddd; color: #666; font-size: 0.9em; }\n");
printf("        .footer-info p { margin: 5px 0; }\n");
printf("        .counter-container { background-color: #e3f2fd; /* [수정] 배경색을 밝은 파란색 계열로 변경 */ border-radius: 8px; padding: 15px; margin-top: 20px; margin-bottom: 20px; border: 2px solid #2196f3; /* [수정] 테두리색을 표준 파란색으로 변경 */ font-size: 1.1em; }\n");
printf("        .counter-container strong { color: #1976d2; /* [수정] 방문자 수 숫자의 색상을 진한 파란색으로 변경 */ }\n");
printf("        pre { background-color: #333; color: #f0f0f0; padding: 10px; border-radius: 5px; overflow-x: auto; text-align: center; }\n");
printf("        .stream-container { display: flex; flex-direction: column; align-items: center; gap: 20px; margin-top: 20px; }\n");
printf("        .image-stream-container {\n");
printf("            border-radius: 12px;\n");
printf("            overflow: hidden;\n");
printf("            width: 90%%; /* 중앙 정렬을 위해 너비 설정 */\n");
printf("            max-width: 600px; /* 최대 너비 설정 */\n");
printf("            height: auto; \n");
printf("            background-color: #333;\n");
printf("            display: flex;\n");
printf("            flex-direction: column;\n");
printf("            justify-content: center;\n");
printf("            align-items: center;\n");
printf("            min-width: 150px;\n");
printf("            min-height: 100px;\n");
printf("            margin-bottom: 20px; /* 버튼과의 간격 확보 */\n");
printf("        }\n");
printf("        .image-stream-container video {\n");
printf("            max-width: 100%%;\n");
printf("            height: auto; \n");
printf("        }\n");
printf("        .image-stream {\n");
printf("            width: 100%%;\n");
printf("            height: 100%%;\n");
printf("            object-fit: contain;\n");
printf("        }\n");
printf("        .responsive-wrapper { flex-grow: 1; display: flex; flex-direction: column; align-items: center; gap: 30px; max-width: 1200px; margin: 0 auto; }\n");
printf("        .main-content { flex-grow: 1; max-width: 800px; text-align: center; }\n");
printf("        .chat-and-new-container { display: flex; flex-direction: column; align-items: center; width: 100%%; }\n");
printf("        .link-row { display: flex; justify-content: center; gap: 10px; margin-bottom: 20px; }\n");
printf("        .link-row a { text-decoration: none; color: #00796b; font-weight: 700; padding: 10px 15px; border: 2px solid #00796b; border-radius: 8px; transition: background-color 0.3s ease, color 0.3s ease; }\n");
printf("        .link-row a:hover { background-color: #00796b; color: #fff; }\n");
printf("        #donation-result, #location-result, #shell-container { \n");
printf("            margin-top: 15px; padding: 15px; \n");
printf("            background-color: #f5f5f5; border: 1px solid #ddd; \n");
printf("            border-radius: 8px; \n");
printf("            box-sizing: border-box; \n");
printf("            text-align: center;\n");
printf("        }\n");
printf("        .refresh-banner { text-align: center; font-size: 0.9em; background-color: #f0f0f0; padding: 5px; border-bottom: 1px solid #ddd; }\n");
printf("        @media (min-width: 768px) {\n");
printf("            .responsive-wrapper { flex-direction: row; justify-content: center; align-items: flex-start; }\n");
printf("            .footer-info { text-align: left; }\n");
printf("            .chat-and-new-container { width: auto; }\n");
printf("        }\n");
printf("        .new-container { \n");
printf("            width: 100%%; \n");
printf("            max-width: 350px; \n");
printf("            background-color: #f8f9fa; \n");
printf("            border-radius: 8px; \n");
printf("            padding: 15px; \n");
printf("            text-align: center; \n");
printf("            margin-top: 20px; \n");
printf("        }\n");
printf("        .announcement-box { border: 1px solid black; padding: 10px; height: 150px; overflow-y: scroll; margin-top: 15px; }\n");
printf("        .shell-output-box { border: 1px solid black; padding: 10px; height: 150px; overflow-y: scroll; }\n");
printf("        .announcement-box img, .announcement-box video, .announcement-box audio { max-width: 100%%; height: auto; }\n");
printf("        .uploaded-files-box { border: 1px solid #ccc; padding: 10px; margin-top: 10px; height: 220px; overflow-y: scroll; }\n");
printf("        .uploaded-files-box img, .uploaded-files-box video, .uploaded-files-box audio { max-width: 100%%; height: auto; }\n");
printf("        .video-stream-container, .audio-stream-container { \n");
printf("            max-width: 800px; \n");
printf("            padding: 20px; \n");
printf("            background-color: #ffffff; \n");
printf("            border-radius: 12px; \n");
printf("            border: 1px solid #ddd; \n");
printf("            margin: 0 auto 20px auto; \n");
printf("            display: flex; /* 추가: Flexbox 사용 */\n");
printf("            flex-direction: column; /* 추가: 세로 방향으로 정렬 */\n");
printf("            align-items: center; /* 추가: 가로 중앙 정렬 */\n");
printf("            text-align: center;\n");
printf("        }\n");
printf("        .video-stream-container h3, .audio-stream-container h3 { \n");
printf("            color: #00796b; \n");
printf("            margin-top: 0; \n");
printf("        }\n");
printf("        .video-frame { \n");
printf("            width: 100%%; \n");
printf("            height: auto; \n");
printf("            border-radius: 8px; \n");
printf("            overflow: hidden; \n");
printf("            border: 2px solid #333;\n");
printf("        }\n");
printf("        .stream-image, .stream-audio { \n");
printf("            max-width: 100%%; \n");
printf("            height: auto; \n");
printf("        }\n");
printf("        .styled-link { \n");
printf("            text-decoration: none; \n");
printf("            color: #333; /* [수정] 글자색 변경: 검정색 */\n");
printf("            background-color: #e65100; /* [수정] 배경색 변경: 주황색 */\n");
printf("            padding: 5px 10px; /* [수정] 크기 줄임 (10px 20px에서 변경) */\n");
printf("            border-radius: 6px; /* [수정] 모서리도 약간 줄임 */\n");
printf("            font-weight: 700; \n");
printf("            transition: background-color 0.3s ease; \n");
printf("            font-size: 0.9em; /* [추가] 글자 크기 살짝 줄임 */\n");
printf("        }\n");
printf("        .styled-link:hover { \n");
printf("            background-color: #ff8f00; /* [수정] 호버 색상도 주황색 계열로 변경 */\n");
printf("            color: #fff; /* [추가] 호버 시 글자색을 흰색으로 변경 */\n");
printf("        }\n");
printf("        .video-link { \n");
printf("            display: inline-block; \n");
printf("            margin: 10px 0; \n");
printf("            padding: 8px 15px; \n");
printf("            background-color: #333; /* [수정] 배경색을 검정색(#333)으로 변경 */ \n");
printf("            color: #fff; \n");
printf("            text-decoration: none; \n");
printf("            border-radius: 5px; \n");
printf("            font-weight: bold; \n");
printf("            transition: background-color 0.3s ease; \n");
printf("        }\n");
printf("        .video-link:hover { \n");
printf("            background-color: #000; /* [수정] 호버 배경색을 완전 검정색으로 변경 */ \n");
printf("        }\n");
/* --- [채팅 링크 스타일 추가] --- */
printf("        .chat-link-style { \n");
printf("            display: inline-block; \n");
printf("            margin: 10px 0; \n");
printf("            padding: 8px 15px; \n");
printf("            background-color: #ffc107; /* 진한 노랑색 */\n");
printf("            color: #333; /* 검정색 글자 */\n");
printf("            text-decoration: none; \n");
printf("            border-radius: 5px; \n");
printf("            font-weight: bold; \n");
printf("            transition: background-color 0.3s ease; \n");
printf("        }\n");
printf("        .chat-link-style:hover { \n");
printf("            background-color: #ff9800; /* 호버 시 더 진한 노랑/주황색 */\n");
printf("        }\n");
/* ---------------------------------- */
printf("        .top-banner-container { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; margin-bottom: 10px; }\n");
printf("        .top-banner-container p { margin: 0; }\n");
printf("        .top-banner-container img { width: 40px; height: auto; margin-left: 10px; }\n");
// ★★★ [수정] Jua 폰트 스타일 색상을 빨간색(#d32f2f)으로 변경 ★★★
printf("        .jua-font-link { font-family: 'Jua', cursive; font-size: 1.2em; color: #d32f2f; font-weight: 700; }\n");

/* --- [새로 추가된 CSS] --- */
printf("        #hidden-content { \n");
printf("            display: none; \n");
printf("            width: 100%%; \n");
printf("            max-width: 350px; \n");
printf("        }\n");
/* --- [채팅창 숨김 처리 및 너비 설정] --- */
printf("        #chat-wrapper { \n");
printf("            display: none; \n"); /* 초기 숨김 */
printf("            width: 350px; \n"); /* iframe 너비에 맞춤 */
printf("            margin-bottom: 20px; \n");
printf("            border-radius: 8px; \n");
printf("            background-color: #fff; \n");
printf("        }\n");
    printf("    </style>\n");
    printf("    <script src=\"https://cdn.jsdelivr.net/npm/hls.js@latest\"></script>\n");
    printf("</head>\n");
    printf("<body>\n");


    printf("    <div class=\"top-banner-container\">\n");
// ★★★ [수정] '오버튼' 링크에 jua-font-link 클래스 적용 ★★★
printf("        <p>새는 <a href=\"#\" onclick=\"window.location.reload(); return false;\" class=\"jua-font-link\">오버튼</a>을다</p>\n");
    printf("        <div class=\"top-images\">\n");
    printf("            <a href=\"http://pip49.com\"><img src=\"/1.jpg\" alt=\"쇼핑\"></a>\n");
    printf("            <a href=\"http://band49.com\"><img src=\"/2.jpg\" alt=\"게시판\"></a>\n");
    printf("        </div>\n");
    printf("    </div>\n");
    printf("    <div class=\"responsive-wrapper\">\n");
    printf("        <div class=\"main-content\">\n");

printf("            <div class=\"video-stream-container\">\n");
/* --- [스트림 1: 웹캠 Live 스트림 컨테이너] --- */
printf("                <div id=\"live-stream-container\" class=\"image-stream-container\" style=\"display:none;\">\n");
printf("                    <h3>Live Stream (웹캠)</h3>\n");
printf("                    <video id=\"livePlayer\" controls disablepictureinpicture controlslist=\"nodownload nofullscreen\"></video>\n");
printf("                </div>\n");

/* --- [스트림 2: MP4 파일 푸시 스트림 컨테이너] --- */
printf("                <div id=\"file-stream-container\" class=\"image-stream-container\" style=\"display:none;\">\n");
printf("                    <h3>File Stream (MP4)</h3>\n");
printf("                    <video id=\"filePlayer\" controls disablepictureinpicture controlslist=\"nodownload nofullscreen\"></video>\n");
printf("                </div>\n");

/* --- [버튼: 스트림 별로 함수 호출하도록 분리] --- */
printf("                <a href=\"#\" onclick=\"toggleLiveStream();\" class=\"video-link\">Live 스트리밍 보기 (웹캠)</a>\n");
printf("                <a href=\"#\" onclick=\"toggleFileStream();\" class=\"video-link\">File 스트리밍 보기 (MP4)</a>\n");
printf("            </div>\n");

    printf("            <div style=\"margin-top: 20px; text-align: center;\">\n");
    printf("                <a href=\"#\" id=\"location-link\" class=\"styled-link\">장소/시간</a>\n");
    printf("            </div>\n");
    printf("            <div id=\"location-result\"></div>\n");

    // 서버 정보보기 섹션이 이 부분으로 이동했습니다.
    printf("            <div style=\"text-align: center; margin-top: 10px;\">\n");
    printf("                <button id=\"showShellResultBtn\"> 소개글</button>\n");
    printf("            </div>\n");
    printf("            <div id=\"shell-container\"></div>\n"); // class="new-container" 제거

    printf("            <div id=\"donation-result\"></div>\n");
    printf("            <div class=\"link-row\">\n");
    printf("                <a href=\"#\" id=\"donation-link\">후원금을 받습니다</a>\n");
    printf("            </div>\n");
    printf("        </div>\n");

    /* --- [채팅 링크 추가] --- */
    printf("        <div style=\"text-align: center; width: 100%%; max-width: 350px; margin-top: 20px;\">\n");
    printf("            <a href=\"#\" id=\"chat-link\" class=\"chat-link-style\">채팅</a>\n"); // <-- class를 chat-link-style로 변경
    printf("        </div>\n");

    /* --- [채팅 컨테이너 (사이드바 위치에 독립적으로 토글)] --- */
    printf("        <div id=\"chat-wrapper\">\n"); /* style="display: none;"은 CSS에 정의됨 */
    printf("            <iframe src=\"/chat.html\" frameborder=\"0\" style=\"width: 350px; height: 500px;\"></iframe>\n");
    printf("        </div>\n");

    /* --- [공지/업로드 컨테이너] --- */
    printf("        <div class=\"chat-and-new-container\">\n");
    printf("            <div style=\"text-align: center; margin-top: 20px;\">\n");
    printf("                <a href=\"#\" id=\"refresh-link\" class=\"video-link\">다운로드/파일</a>\n"); // 이 링크는 항상 보입니다.
    printf("            </div>\n");

    printf("            <div id=\"hidden-content\">\n"); // 이 div가 클릭 시 나타납니다.
   printf("                    <h3>(공지)파일재생</h3>\n");
    printf("                <div class=\"announcement-box\">\n");
    printf("                    <h5>(공지)</h5>\n");
    print_announcement_from_file();
    printf("                </div>\n");
    printf("                <div class=\"uploads-box\">\n");
    print_uploaded_files();
    printf("                </div>\n");

    printf("            </div>\n"); // hidden-content 닫힘
    printf("        </div>\n");
    printf("    </div>\n");
    printf("    <div class=\"footer-info\">\n");
    printf("        <div>\n");
    printf("            <p> P앤I PUNCH</p>\n");
    printf("            <p> 함병철</p>\n");
    printf("            <p> 서울시 강동구 성내로15길65</p>\n");
    printf("            <p> Tel) 02) 479-3246</p>\n");
    printf("            <p> Email) hambyeongcheol44@gmail.com</p>\n");
    printf("        </div>\n");
    printf("        <div class=\"counter-container\">\n");
    printf("            <p>오늘 방문자 수: <strong>%d</strong></p>\n", count);
    printf("        </div>\n");
    printf("    </div>\n");
    printf("    <script>\n");
    printf("        document.getElementById('donation-link').addEventListener('click', function(event) {\n");
    printf("            event.preventDefault();\n");
    printf("            var resultDiv = document.getElementById('donation-result');\n");
    printf("            if (resultDiv.innerHTML.trim() !== '') {\n");
    printf("                resultDiv.innerHTML = '';\n");
    printf("            } else {\n");
    printf("                resultDiv.innerHTML = '스크립트를 실행 중입니다...';\n");
    printf("                fetch('/cgi-bin/donate.cgi')\n");
    printf("                    .then(response => {\n");
    printf("                        if (!response.ok) {\n");
    printf("                            throw new Error('네트워크 응답이 올바르지 않습니다.');\n");
    printf("                        }\n");
    printf("                        return response.text();\n");
    printf("                    })\n");
    printf("                    .then(data => {\n");
    printf("                        var parser = new DOMParser();\n");
    printf("                        var doc = parser.parseFromString(data, 'text/html');\n");
    printf("                        var preContent = doc.querySelector('pre');\n");
    printf("                        if (preContent) {\n");
    printf("                            resultDiv.innerHTML = preContent.innerHTML;\n");
    printf("                        } else {\n");
    printf("                            resultDiv.innerHTML = '스크립트 실행 결과가 없습니다.';\n");
    printf("                        }\n");
    printf("                    })\n");
    printf("                    .catch(error => {\n");
    printf("                        resultDiv.innerHTML = '<p style=\"color: red;\">오류: ' + error.message + '</p>';\n");
    printf("                        console.error('Fetch error:', error);\n");
    printf("                    });\n");
    printf("            }\n");
    printf("        });\n");
    printf("\n");
    printf("        document.getElementById('location-link').addEventListener('click', function(event) {\n");
    printf("            event.preventDefault();\n");
    printf("            var resultDiv = document.getElementById('location-result');\n");
    printf("            if (resultDiv.innerHTML.trim() !== '') {\n");
    printf("                resultDiv.innerHTML = '';\n");
    printf("            } else {\n");
    printf("                resultDiv.innerHTML = '장소 정보를 실행 중입니다...';\n");
    printf("                fetch('/cgi-bin/place.cgi')\n");
    printf("                    .then(response => {\n");
    printf("                        if (!response.ok) {\n");
    printf("                            throw new Error('네트워크 응답이 올바르지 않습니다.');\n");
    printf("                        }\n");
    printf("                        return response.text();\n");
    printf("                    })\n");
    printf("                    .then(data => {\n");
    printf("                        resultDiv.innerHTML = '<pre>' + data + '</pre>';\n");
    printf("                    })\n");
    printf("                    .catch(error => {\n");
    printf("                        resultDiv.innerHTML = '<p style=\"color: red;\">오류: ' + error.message + '</p>';\n");
    printf("                        console.error('Fetch error:', error);\n");
    printf("                    });\n");
    printf("            }\n");
    printf("        });\n");
    printf("\n");
    printf("        var audio1 = document.getElementById('audio1');\n");
    printf("        var audio2 = document.getElementById('audio2');\n");
    printf("        var audio3 = document.getElementById('audio3');\n");
    printf("        var currentAudio = null;\n");
    printf("\n");
    printf("        function playAudio(audioId) {\n");
    printf("            var audio = document.getElementById(audioId);\n");
    printf("            if (currentAudio && currentAudio !== audio) {\n");
    printf("                currentAudio.pause();\n");
    printf("                currentAudio.currentTime = 0;\n");
    printf("            }\n");
    printf("            if (audio.paused) {\n");
    printf("                audio.play();\n");
    printf("                currentAudio = audio;\n");
    printf("            } else {\n");
    printf("                audio.pause();\n");
    printf("            }\n");
    printf("        }\n");
    printf("\n");

/* --- [ Live Stream (웹캠) 토글 함수 - HLS.js 사용 ] --- */
printf("            function toggleLiveStream() {\n");
printf("                var liveContainer = document.getElementById('live-stream-container');\n");
printf("                var liveVideo = document.getElementById('livePlayer');\n");
printf("                var liveUrl = 'http://125.251.58.88:8080/live/my_stream1.m3u8'; // 웹캠 Live 스트림 주소\n");
printf("                \n");
printf("                // 다른 스트림 닫기\n");
printf("                document.getElementById('file-stream-container').style.display = 'none';\n");
printf("                document.getElementById('filePlayer').pause();\n");
printf("                \n");
printf("                if (liveContainer.style.display === 'block') {\n");
printf("                    // 닫기 (숨김 및 정지)\n");
printf("                    liveContainer.style.display = 'none';\n");
printf("                    liveContainer.style.width = '';\n"); // 너비 초기화
printf("                    liveVideo.pause();\n");
printf("                    if (liveVideo.hlsInstance) {\n");
printf("                        liveVideo.hlsInstance.destroy();\n");
printf("                        liveVideo.hlsInstance = null;\n");
printf("                    }\n");
printf("                } else {\n");
printf("                    // 열기 및 HLS 재생\n");
printf("                    liveContainer.style.display = 'block';\n");
printf("                    // liveContainer.style.width = '50%%'; // 👈 이 줄을 제거하여 CSS 중앙 정렬을 따르게 함\n");
printf("                    \n");
printf("                    if (liveVideo.hlsInstance) { liveVideo.hlsInstance.destroy(); liveVideo.hlsInstance = null; }\n");
printf("                    \n");
printf("                    if (Hls.isSupported()) {\n");
printf("                        var hls = new Hls();\n");
printf("                        hls.loadSource(liveUrl);\n");
printf("                        hls.attachMedia(liveVideo);\n");
printf("                        liveVideo.hlsInstance = hls;\n");
printf("                    } else if (liveVideo.canPlayType('application/vnd.apple.mpegurl')) {\n");
printf("                        liveVideo.src = liveUrl;\n");
printf("                    }\n");
printf("                    liveVideo.play();\n");
printf("                }\n");
printf("            }\n");

/* --- [ File Stream (MP4) 토글 함수 - 네이티브 로드 사용 ] --- */
printf("            function toggleFileStream() {\n");
printf("                var fileContainer = document.getElementById('file-stream-container');\n");
printf("                var fileVideo = document.getElementById('filePlayer');\n");
printf("                var fileUrl = 'http://125.251.58.88:8080/live/my_stream2.m3u8'; // MP4 파일 푸시 스트림 주소 (HLS 변환)\n");
printf("                \n");
printf("                // 다른 스트림 닫기\n");
printf("                document.getElementById('live-stream-container').style.display = 'none';\n");
printf("                document.getElementById('livePlayer').pause();\n");
printf("                if (document.getElementById('livePlayer').hlsInstance) { document.getElementById('livePlayer').hlsInstance.destroy(); document.getElementById('livePlayer').hlsInstance = null; }\n");
printf("                \n");
printf("                if (fileContainer.style.display === 'block') {\n");
printf("                    // 닫기\n");
printf("                    fileContainer.style.display = 'none';\n");
printf("                    fileVideo.pause();\n");
printf("                    fileVideo.src = ''; // src 비우기\n");
printf("                } else {\n");
printf("                    // 열기 및 재생\n");
printf("                    fileContainer.style.display = 'block';\n");
printf("                    \n");
printf("                    // FFmpeg 푸시 스트림을 HLS로 간주하고 HLS.js를 사용하여 로드합니다.\n");
printf("                    if (Hls.isSupported()) {\n");
printf("                        var hls = new Hls();\n");
printf("                        hls.loadSource(fileUrl);\n");
printf("                        hls.attachMedia(fileVideo);\n");
printf("                        fileVideo.hlsInstance = hls;\n");
printf("                    }\n");
printf("                    liveVideo.play();\n");
printf("                }\n");
printf("            }\n");
    printf("\n");
    printf("\n");
    printf("        // 다운로드 확인 및 재생/열기 기능을 포함한 자바스크립트 코드\n");
    printf("        document.addEventListener('DOMContentLoaded', function() {\n");
    printf("            var downloadLinks = document.querySelectorAll('.download-link');\n");
    printf("            downloadLinks.forEach(function(link) {\n");
    printf("                link.addEventListener('click', function(event) {\n");
    printf("                    var fileName = this.getAttribute('download');\n");
    printf("                    var confirmDownload = confirm('\"' + fileName + '\" 파일을 다운로드하시겠습니까?');\n");
    printf("                    \n");
    printf("                    if (confirmDownload) {\n");
    printf("                        // 사용자가 '확인'을 누르면 다운로드를 허용\n");
    printf("                        return true; \n");
    printf("                    } else {\n");
    printf("                        // 사용자가 '취소'를 누르면 다운로드를 막고\n");
    printf("                        event.preventDefault();\n");
    printf("                        // 새 탭에서 파일을 엽니다 (브라우저가 재생/열기 시도)\n");
    printf("                        window.open(this.href, '_blank');\n");
    printf("                    }\n");
    printf("                });\n");
    printf("            });\n");
    printf("        });\n");
    printf("\n");
    printf("        document.getElementById('showShellResultBtn').addEventListener('click', function() {\n");
    printf("            var shellContainer = document.getElementById('shell-container');\n");
    printf("            if (shellContainer.innerHTML.trim() !== '') {\n");
    printf("                shellContainer.innerHTML = '';\n");
    printf("                return;\n");
    printf("            }\n");
    printf("            shellContainer.innerHTML = '로딩 중...';\n");
    printf("            fetch('/cgi-bin/shell_result.cgi')\n");
    printf("                .then(response => response.text())\n");
    printf("                .then(data => {\n");
    printf("                    shellContainer.innerHTML = data;\n");
    printf("                })\n");
    printf("                .catch(error => {\n");
    printf("                        shellContainer.innerHTML = '<p style=\"color: red;\">데이터를 불러오는 데 실패했습니다.</p>';\n");
    printf("                        console.error('Error:', error);\n");
    printf("                    });\n");
    printf("        });\n");
    printf("        \n");
    /* --- [새로고침(공지/업로드) 토글 함수] --- */
    printf("        document.getElementById('refresh-link').addEventListener('click', function(event) {\n");
printf("            event.preventDefault(); // 페이지 새로고침 방지\n");
printf("            var hiddenContent = document.getElementById('hidden-content');\n");
printf("            \n");
printf("            // 현재 display 상태를 확인하여 토글합니다.\n");
printf("            if (hiddenContent.style.display === 'block') {\n");
printf("                // 현재 보이고 있다면 숨깁니다.\n");
printf("                hiddenContent.style.display = 'none';\n");
printf("            } else {\n");
printf("                // 현재 숨겨져 있다면 보이게 합니다.\n");
printf("                hiddenContent.style.display = 'block';\n");
printf("            }\n");
printf("        });\n");
    /* --- [채팅창 토글 함수 (새로 추가됨)] --- */
    printf("        document.getElementById('chat-link').addEventListener('click', function(event) {\n");
    printf("            event.preventDefault(); \n");
    printf("            var chatWrapper = document.getElementById('chat-wrapper');\n");
    printf("            \n");
    printf("            // 현재 display 상태를 확인하여 토글합니다.\n");
    printf("            if (chatWrapper.style.display === 'block') {\n");
    printf("                // 현재 보이고 있다면 숨깁니다.\n");
    printf("                chatWrapper.style.display = 'none';\n");
    printf("            } else {\n");
    printf("                // 현재 숨겨져 있다면 보이게 합니다.\n");
    printf("                chatWrapper.style.display = 'block';\n");
    printf("            }\n");
    printf("        });\n");
    printf("    </script>\n");
    printf("</body>\n");
    printf("</html>\n");

    // ******************************************************
    // ★ 5. 후처리 및 정리 (스크립트 종료 전 카운터 감소)
    // ******************************************************
CLEANUP_AND_EXIT:
    // Redis 연결이 성공했을 때만 연결을 닫습니다.
    if (c != NULL) {
        // 이미 리디렉션된 경우 (current_users > MAX_USERS) DECR은 위에서 이미 실행됨
        // 정상 접속한 경우 (redirected == 0)는 EXPIRE로 락을 유지하고 있으므로 DECR은 생략
        redisFree(c);
    }

    return 0;
}
}
