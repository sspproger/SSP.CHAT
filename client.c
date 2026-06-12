#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define PORT 8888
#define DEFAULT_IP "127.0.0.1"
#define ENCRYPTION_KEY 0xAA

// ANSI цветовые коды
#define RESET   "\033[0m"
#define BLACK   "\033[30m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

// Очистка экрана и перемещение курсора
#define CLEAR_SCREEN "\033[2J\033[H"
#define HIDE_CURSOR  "\033[?25l"
#define SHOW_CURSOR  "\033[?25h"

int sock;
volatile sig_atomic_t running = 1;
int authenticated = 0;
char my_username[50];
int need_redraw_prompt = 0;  // Флаг для перерисовки приглашения

// Функция сквозного шифрования
void e2e_encrypt(char *data, int len) {
    for(int i = 0; i < len; i++) {
        data[i] ^= ENCRYPTION_KEY;
    }
}

// Получить текущее время
void get_timestamp(char *buffer, int size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(buffer, size, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

// Нарисовать заголовок
void draw_header() {
    printf(CLEAR_SCREEN);
    printf(HIDE_CURSOR);
    
    printf(CYAN BOLD);
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                    ║\n");
    printf("║" CYAN "     ███████╗███████╗██████╗     ██████╗██╗  ██╗ █████╗ ████████╗   " CYAN "║\n");
    printf("║" CYAN "     ██╔════╝██╔════╝██╔══██╗   ██╔════╝██║  ██║██╔══██╗╚══██╔══╝   " CYAN "║\n");
    printf("║" CYAN "     ███████╗███████╗██████╔╝   ██║     ███████║███████║   ██║      " CYAN "║\n");
    printf("║" CYAN "     ╚════██║╚════██║██╔═══╝    ██║     ██╔══██║██╔══██║   ██║      " CYAN "║\n");
    printf("║" CYAN "     ███████║███████║██║     ██╗╚██████╗██║  ██║██║  ██║   ██║      " CYAN "║\n");
    printf("║" CYAN "     ╚══════╝╚══════╝╚═╝     ╚═╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝      " CYAN "║\n");
    printf("║                                                                    ║\n");
    printf("║" YELLOW BOLD "                         SSP.chat                               " CYAN "    ║\n");
    printf("║                                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf(RESET "\n");
}

// Нарисовать статус-бар
void draw_status_bar() {
    char timestamp[10];
    get_timestamp(timestamp, sizeof(timestamp));
    
    if(authenticated && strlen(my_username) > 0) {
        printf(DIM "[%s] username: [%s]" RESET "\n", timestamp, my_username);
    } else {
        printf(DIM "[%s] " YELLOW "Введите команду логин и пароль" RESET "\n", timestamp);
    }
}

// Нарисовать меню команд
void draw_commands_menu() {
    printf("\n" DIM);
    printf("┌────────────────────────────────────────────────────────────────────┐\n");
    printf("│ " WHITE "                       ДОСТУПНЫЕ КОМАНДЫ" DIM "                           │\n");
    printf("├────────────────────────────────────────────────────────────────────┤\n");
    printf("│ " WHITE "/reg" DIM "      username password  - регистрация нового пользователя     │\n");
    printf("│ " WHITE "/login" DIM "    username password  - вход в систему                      │\n");
    printf("│ " WHITE "/exit" DIM "                        - выход из чата                       │\n");
    if(authenticated) {
        printf("│ " WHITE "/changename" DIM " new_name         - сменить имя пользователя            │\n");
        printf("│ " WHITE "/changepass" DIM " old new          - сменить пароль                      │\n");
        printf("│ " WHITE "/users" DIM "                       - список активных пользователей       │\n");
        printf("│ " WHITE "/clear" DIM "                       - очистить экран                      │\n");
    }
    printf("└────────────────────────────────────────────────────────────────────┘\n");
    printf(RESET);
}

// Вывести приглашение для ввода
void print_prompt() {
    char time_buf[10];
    get_timestamp(time_buf, sizeof(time_buf));
    
    if(authenticated && strlen(my_username) > 0) {
        printf(DIM "[%s]" RESET " " CYAN "[" GREEN "%s" CYAN "] " RESET, time_buf, my_username);
    } else {
        printf(DIM "[%s]" RESET " " GREEN "[Вход]>>> " RESET, time_buf);
    }
    fflush(stdout);
}

// Форматирование и вывод сообщения
void print_message(const char *msg) {
    char buffer[BUFFER_SIZE];
    strcpy(buffer, msg);
    
    // Убираем символ новой строки в конце
    int len = strlen(buffer);
    if(len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
        buffer[len-1] = '\0';
    }
    if(len > 1 && buffer[len-2] == '\r') {
        buffer[len-2] = '\0';
    }
    
    char time_buf[10];
    get_timestamp(time_buf, sizeof(time_buf));
    
    // Проверяем успешный вход в систему
    if(strstr(buffer, "Добро пожаловать")) {
        authenticated = 1;
        draw_header();
        draw_status_bar();
        draw_commands_menu();
        print_prompt();
        return;
    }

    // Если выход из системы
    if(strstr(buffer, "До свидания")) {
        authenticated = 0;
        my_username[0] = '\0';
        draw_header();
        draw_status_bar();
        draw_commands_menu();
        print_prompt();
        return;
    }
    
    // Перед выводом сообщения нужно перейти на новую строку
    // и очистить текущую строку с приглашением
    printf("\r\033[K");  // Возврат в начало строки и очистка
    
    // Раскрашиваем разные типы сообщений
    if(strstr(buffer, "присоединился") || strstr(buffer, "присоединился к") || 
       strstr(buffer, "joined") || strstr(buffer, "вошёл") ||
       strstr(buffer, "покинул") || strstr(buffer, "left") || strstr(buffer, "вышел")) {
        printf(DIM "[%s]" RESET " " YELLOW "➤ %s" RESET "\n", time_buf, buffer);
    }
    else if(strstr(buffer, "Регистрация успешна")) {
        printf(DIM "[%s]" RESET " " GREEN "✓ %s" RESET "\n", time_buf, buffer);
    }
    else if(strstr(buffer, "Неверное") || strstr(buffer, "существует") || 
            strstr(buffer, "Ошибка") || strstr(buffer, "ошибка") ||
            strstr(buffer, "переполнен")) {
        printf(DIM "[%s]" RESET " " RED "✗ %s" RESET "\n", time_buf, buffer);
    }
    else if(strstr(buffer, ":")) {
        char *colon = strstr(buffer, ":");
        if(colon) {
            int username_len = colon - buffer;
            char username[50];
            strncpy(username, buffer, username_len);
            username[username_len] = '\0';
            
            // Не выводим свои сообщения (они уже выведены при отправке)
            if(authenticated && strcmp(username, my_username) == 0) {
                // Свои сообщения не выводим, просто перерисовываем приглашение
                print_prompt();
                return;
            } else {
                printf(DIM "[%s]" RESET " " CYAN BOLD "%s" RESET ":" WHITE "%s" RESET "\n", 
                       time_buf, username, colon + 1);
            }
        } else {
            printf(DIM "[%s]" RESET " %s\n", time_buf, buffer);
        }
    }
    else {
        // Убираем <<< и >>> если есть
        char *clean_buffer = buffer;
        if(strstr(buffer, "<<< ") == buffer) {
            clean_buffer = buffer + 4;
        }
        char *end = strstr(clean_buffer, " >>>");
        if(end) {
            *end = '\0';
        }
        printf(DIM "[%s]" RESET " %s\n", time_buf, clean_buffer);
    }
    
    // После вывода сообщения снова показываем приглашение
    print_prompt();
}

void *receive_messages(void *arg){
    char buffer[BUFFER_SIZE];
    int bytes;

    while(running){
        bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0){
            if(running) {
                printf("\r\033[K" RED "<<< Соединение с сервером потеряно. >>>\n" RESET);
            }
            break;
        }
        buffer[bytes] = '\0';
        e2e_encrypt(buffer, bytes);
        
        print_message(buffer);
    }
    return NULL;
}

// Функция для преобразования байт в HEX строку
void bytes_to_hex(const unsigned char *data, int len, char *hex) {
    for(int i = 0; i < len; i++) {
        sprintf(hex + i*2, "%02X", data[i]);
    }
    hex[len*2] = '\0';
}

// Обработчик Ctrl+C
void handle_sigint(int sig) {
    printf(SHOW_CURSOR);
    printf("\r\033[K\n" RED "\n┌──────────────────────────────────────────────────────────────────────┐\n");
    printf("│                       👋 ДО СВИДАНИЯ! 👋                                 │\n");
    printf("└──────────────────────────────────────────────────────────────────────┘\n" RESET);
    running = 0;
    if(sock > 0) {
        if(authenticated) {
            send(sock, "/exit", 5, 0);
        }
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
    exit(0);
}

int main(int argc, char *argv[]){
    struct sockaddr_in server_addr;
    pthread_t receive_thread;
    char buffer[BUFFER_SIZE];
    char *server_ip;
    
    signal(SIGINT, handle_sigint);

    if(argc > 1){
        server_ip = argv[1];
    } else {
        server_ip = DEFAULT_IP;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        perror("<<< Ошибка создания сокета! >>>");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    draw_header();
    
    printf(BLUE "<<< Подключение к " CYAN "%s" BLUE ":%d... >>>\n" RESET, server_ip, PORT);

    if(connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        printf(RED "<<< Ошибка подключения к серверу! >>>\n" RESET);
        printf(YELLOW "<<< Убедитесь, что сервер запущен на %s:%d >>>\n" RESET, server_ip, PORT);
        exit(1);
    }

    printf(GREEN "<<< Подключение установлено! >>>\n" RESET);
    
    draw_status_bar();
    draw_commands_menu();
    
    printf(SHOW_CURSOR);
    
    pthread_create(&receive_thread, NULL, receive_messages, NULL);

    while(running){
        print_prompt();

        if(fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        // Убираем символ новой строки
        buffer[strcspn(buffer, "\n")] = '\0';
    
        // Если пустое сообщение - пропускаем
        if(strlen(buffer) == 0) {
            continue;
        }
    
        // Обработка локальных команд (не отправляются на сервер)
        if(strcmp(buffer, "/clear") == 0 && authenticated) {
            draw_header();
            draw_status_bar();
            draw_commands_menu();
            continue;
        }
    
        if(strcmp(buffer, "/exit") == 0){
            printf(YELLOW "\n<<< Выход из чата... >>>\n" RESET);
            break;
        }
    
        // Сохраняем оригинальное сообщение для вывода (до шифрования)
        char original_msg[BUFFER_SIZE];
        strcpy(original_msg, buffer);
    
        // Проверка на успешную авторизацию
        if(strncmp(buffer, "/login ", 7) == 0) {
            char username[50], password[50];
            sscanf(buffer + 7, "%s %s", username, password);
            strcpy(my_username, username);
        }
    
        // Проверка на смену имени
        if(strncmp(buffer, "/changename ", 12) == 0 && authenticated) {
            char newname[50];
            sscanf(buffer + 12, "%s", newname);
            strcpy(my_username, newname);
        }

        // Обработка команд с шифрованием
        if(strncmp(buffer, "/reg ", 4) == 0) {
            char username[50], password[50];
            sscanf(buffer + 4, "%s %s", username, password);
    
            char encrypted_password[256];
            strcpy(encrypted_password, password);
            e2e_encrypt(encrypted_password, strlen(password));
    
            char hex_password[512];
            bytes_to_hex((unsigned char*)encrypted_password, strlen(password), hex_password);
    
            snprintf(buffer, BUFFER_SIZE, "/reg %s %s", username, hex_password);
        }
        else if(strncmp(buffer, "/login ", 7) == 0) {
            char username[50], password[50];
            sscanf(buffer + 7, "%s %s", username, password);
    
            char encrypted_password[256];
            strcpy(encrypted_password, password);
            e2e_encrypt(encrypted_password, strlen(password));
    
            char hex_password[512];
            bytes_to_hex((unsigned char*)encrypted_password, strlen(password), hex_password);
    
            snprintf(buffer, BUFFER_SIZE, "/login %s %s", username, hex_password);
        }
        else if(strncmp(buffer, "/changepass ", 12) == 0 && authenticated) {
            char old_pass[100], new_pass[100];
            sscanf(buffer + 12, "%s %s", old_pass, new_pass);
    
            char encrypted_old[256], encrypted_new[256];
            char hex_old[512], hex_new[512];
    
            strcpy(encrypted_old, old_pass);
            e2e_encrypt(encrypted_old, strlen(old_pass));
            bytes_to_hex((unsigned char*)encrypted_old, strlen(old_pass), hex_old);
    
            strcpy(encrypted_new, new_pass);
            e2e_encrypt(encrypted_new, strlen(new_pass));
            bytes_to_hex((unsigned char*)encrypted_new, strlen(new_pass), hex_new);
    
            snprintf(buffer, BUFFER_SIZE, "/changepass %s %s", hex_old, hex_new);
        }
    
        // Вывод своего сообщения (только если это не служебная команда и пользователь авторизован)
        if(authenticated && strncmp(original_msg, "/", 1) != 0) {
            char time_buf2[10];
            get_timestamp(time_buf2, sizeof(time_buf2));
            // Стираем строку с приглашением и выводим свое сообщение
            printf("\r\033[K");  // Очищаем текущую строку
            printf(DIM "[%s]" RESET " " CYAN "[" GREEN "%s" CYAN "]" RESET " %s\n", 
                   time_buf2, my_username, original_msg);
        }
    
        if(strlen(buffer) > 0){
            char encrypted[BUFFER_SIZE];
            strcpy(encrypted, buffer);
            e2e_encrypt(encrypted, strlen(buffer));
            if(send(sock, encrypted, strlen(buffer), 0) < 0){
                printf(RED "<<< Ошибка отправки >>>\n" RESET);
                break;
            }
        }
    }

    printf(SHOW_CURSOR);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    pthread_cancel(receive_thread);
    pthread_join(receive_thread, NULL);
    
    printf(YELLOW "\n<<< До встречи в SSP.CHAT >>>\n" RESET);
    return 0;
}