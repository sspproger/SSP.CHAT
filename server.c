// Компиляция: gcc server.c -o server
// Запуск: ./server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 2048
#define PORT 8888
#define USERS_FILE "users.txt"
#define ENCRYPTION_KEY 0xAA

typedef struct {
    int socket;
    char username[50];
    int authenticated;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
int server_socket;
volatile sig_atomic_t running = 1;

// Функция расшифровки (для чтения КОМАНД, но не для хранения паролей!)
void e2e_decrypt(char *data, int len) {
    for(int i = 0; i < len; i++) {
        data[i] ^= ENCRYPTION_KEY;
    }
}

// Функция шифрования (для отправки ответов)
void e2e_encrypt(char *data, int len) {
    for(int i = 0; i < len; i++) {
        data[i] ^= ENCRYPTION_KEY;
    }
}

// ========== НОВЫЕ ФУНКЦИИ ДЛЯ РАБОТЫ С HEX ==========

// Сохранение пользователя с HEX паролем
int register_user_hex(const char *username, const char *hex_password) {
    FILE *file = fopen(USERS_FILE, "a+");
    if (!file) return 0;
    
    // Проверяем, существует ли уже пользователь
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char existing_user[50];
        sscanf(line, "%[^:]:", existing_user);
        if (strcmp(existing_user, username) == 0) {
            fclose(file);
            return 0;
        }
    }
    
    // Сохраняем username и HEX пароль
    fprintf(file, "%s:%s\n", username, hex_password);
    fclose(file);
    printf("<<< Зарегистрирован пользователь: %s >>>\n", username);
    return 1;
}

// Проверка логина с HEX паролем
int login_user_hex(const char *username, const char *hex_password) {
    FILE *file = fopen(USERS_FILE, "r");
    if (!file) return 0;
    
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char stored_user[50];
        char stored_hex[256];
        sscanf(line, "%[^:]:%s", stored_user, stored_hex);
        
        if (strcmp(stored_user, username) == 0) {
            fclose(file);
            // Сравниваем HEX строки
            return (strcmp(stored_hex, hex_password) == 0);
        }
    }
    
    fclose(file);
    return 0;
}

// Обновление пароля в HEX формате
int update_password_hex(const char *username, const char *new_hex_password) {
    FILE *file = fopen(USERS_FILE, "r");
    if (!file) return 0;
    
    FILE *temp = fopen("users_temp.txt", "w");
    if (!temp) {
        fclose(file);
        return 0;
    }
    
    char line[512];
    int updated = 0;
    
    while (fgets(line, sizeof(line), file)) {
        char stored_user[50];
        char stored_hex[256];
        sscanf(line, "%[^:]:%s", stored_user, stored_hex);
        
        if (strcmp(stored_user, username) == 0) {
            fprintf(temp, "%s:%s\n", username, new_hex_password);
            updated = 1;
        } else {
            fprintf(temp, "%s", line);
        }
    }
    
    fclose(file);
    fclose(temp);
    
    if (updated) {
        remove(USERS_FILE);
        rename("users_temp.txt", USERS_FILE);
        return 1;
    } else {
        remove("users_temp.txt");
        return 0;
    }
}

// Обновить имя пользователя в файле
int update_username(const char *old_username, const char *new_username) {
    FILE *file = fopen(USERS_FILE, "r");
    if (!file) return 0;
    
    // Временный файл
    FILE *temp = fopen("users_temp.txt", "w");
    if (!temp) {
        fclose(file);
        return 0;
    }
    
    char line[512];
    int updated = 0;
    
    while (fgets(line, sizeof(line), file)) {
        char stored_user[50];
        char stored_hex[256];
        sscanf(line, "%[^:]:%s", stored_user, stored_hex);
        
        if (strcmp(stored_user, old_username) == 0) {
            // Сохраняем с новым именем, пароль оставляем тот же
            fprintf(temp, "%s:%s\n", new_username, stored_hex);
            updated = 1;
        } else {
            // Копируем остальных пользователей без изменений
            fprintf(temp, "%s", line);
        }
    }
    
    fclose(file);
    fclose(temp);
    
    if (updated) {
        // Заменяем старый файл новым
        remove(USERS_FILE);
        rename("users_temp.txt", USERS_FILE);
        return 1;
    } else {
        remove("users_temp.txt");
        return 0;
    }
}

// Найти клиента по сокету
Client* find_client(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == socket) {
            return &clients[i];
        }
    }
    return NULL;
}

// Отправить зашифрованное сообщение клиенту
void send_to_client(int socket, const char *message) {
    char encrypted[BUFFER_SIZE];
    strcpy(encrypted, message);
    e2e_encrypt(encrypted, strlen(message));
    send(socket, encrypted, strlen(message), 0);
}

// Рассылка зашифрованных сообщений всем
void broadcast_message(const char *message, int sender_socket) {
    char encrypted[BUFFER_SIZE];
    strcpy(encrypted, message);
    e2e_encrypt(encrypted, strlen(message));
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0 && 
            clients[i].socket != sender_socket &&
            clients[i].authenticated) {
            send(clients[i].socket, encrypted, strlen(message), 0);
        }
    }
}

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = 0;
        clients[i].username[0] = '\0';
        clients[i].authenticated = 0;
    }
}

void add_client(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == 0) {
            clients[i].socket = socket;
            clients[i].authenticated = 0;
            clients[i].username[0] = '\0';
            client_count++;
            break;
        }
    }
}

void remove_client(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket == socket) {
            if (clients[i].authenticated && clients[i].username[0]) {
                char message[BUFFER_SIZE];
                snprintf(message, sizeof(message), "<<< %s покинул чат >>>\n", clients[i].username);
                broadcast_message(message, socket);
                printf("<<< %s вышел из чата >>>\n", clients[i].username);
            }
            clients[i].socket = 0;
            clients[i].authenticated = 0;
            clients[i].username[0] = '\0';
            client_count--;
            break;
        }
    }
}

void handle_sigint(int sig) {
    printf("\n<<< Сервер останавливается... >>>\n");
    running = 0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket != 0) {
            shutdown(clients[i].socket, SHUT_RDWR);
            close(clients[i].socket);
        }
    }
    
    if (server_socket > 0) {
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
    }
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    signal(SIGINT, handle_sigint);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("<<< Ошибка создания сокета >>>");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("<<< Ошибка привязки порта >>>");
        exit(1);
    }
    
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("<<< Ошибка прослушивания >>>");
        exit(1);
    }
    
    init_clients();
    printf("<<< Сервер запущен на порту %d >>>\n", PORT);
    printf("<<< Ожидание подключений... >>>\n");
    printf("<<< Нажмите Ctrl+C для остановки сервера >>>\n\n");
    
    fd_set read_fds;
    int max_fd;
    
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        max_fd = server_socket;
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket != 0) {
                FD_SET(clients[i].socket, &read_fds);
                if (clients[i].socket > max_fd) {
                    max_fd = clients[i].socket;
                }
            }
        }
        
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        if (select(max_fd + 1, &read_fds, NULL, NULL, &tv) < 0) {
            if (running) perror("<<< Ошибка select >>>");
            continue;
        }
        
        // Новые подключения
        if (FD_ISSET(server_socket, &read_fds)) {
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_socket < 0) {
                if (running) perror("<<< Ошибка принятия >>>");
                continue;
            }
            
            if (client_count >= MAX_CLIENTS) {
                char *msg = "<<< Сервер переполнен >>>\n";
                send(client_socket, msg, strlen(msg), 0);
                close(client_socket);
                continue;
            }
            
            add_client(client_socket);
            printf("<<< Новое подключение >>>\n");
        }
        
        // Обработка сообщений от клиентов
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket != 0 && FD_ISSET(clients[i].socket, &read_fds)) {
                int bytes = recv(clients[i].socket, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes <= 0) {
                    printf("<<< Клиент отключился >>>\n");
                    close(clients[i].socket);
                    remove_client(clients[i].socket);
                } else {
                    buffer[bytes] = '\0';
                    
                    // РАСШИФРОВЫВАЕМ для чтения команды (только для парсинга!)
                    e2e_decrypt(buffer, bytes);
                    
                    Client *client = find_client(clients[i].socket);
                    
                    printf("<<< Получено (расшифровано для команды): %s >>>\n", buffer);
                    
                    // ========== ОБРАБОТКА КОМАНД С HEX ==========
                    
                    // Обработка регистрации (получаем HEX пароль)
                    if (strncmp(buffer, "/reg ", 5) == 0) {
                        char username[50], hex_password[256];
                        sscanf(buffer + 5, "%s %s", username, hex_password);
                        
                        if (register_user_hex(username, hex_password)) {
                            char msg[] = "<<< Регистрация успешна! Теперь войдите: /login username password >>>\n";
                            send_to_client(clients[i].socket, msg);
                            printf("<<< Зарегистрирован пользователь: %s >>>\n", username);
                        } else {
                            char msg[] = "<<< Пользователь уже существует >>>\n";
                            send_to_client(clients[i].socket, msg);
                        }
                    }
                    // Обработка логина (получаем HEX пароль)
                    else if (strncmp(buffer, "/login ", 7) == 0) {
                        char username[50], hex_password[256];
                        sscanf(buffer + 7, "%s %s", username, hex_password);
                        
                        if (login_user_hex(username, hex_password)) {
                            strcpy(client->username, username);
                            client->authenticated = 1;
                            
                            char msg[BUFFER_SIZE];
                            snprintf(msg, sizeof(msg), "<<< Добро пожаловать, %s! >>>\n", username);
                            send_to_client(clients[i].socket, msg);
                            
                            snprintf(msg, sizeof(msg), "<<< Вы вошли в чат. Можете общаться! >>>\n");
                            send_to_client(clients[i].socket, msg);
                            
                            printf("<<< %s вошёл в чат >>>\n", username);
                            
                            char notify[BUFFER_SIZE];
                            snprintf(notify, sizeof(notify), "<<< %s присоединился к чату >>>\n", username);
                            broadcast_message(notify, clients[i].socket);
                        } else {
                            char msg[] = "<<< Неверное имя пользователя или пароль >>>\n";
                            send_to_client(clients[i].socket, msg);
                            printf("<<< Неудачная попытка входа: %s >>>\n", username);
                        }
                    }
                    else if (strcmp(buffer, "/exit") == 0) {
                        if (client->authenticated) {
                            char msg[BUFFER_SIZE];
                            snprintf(msg, sizeof(msg), "<<< %s покинул чат >>>\n", client->username);
                            broadcast_message(msg, clients[i].socket);
                            printf("<<< %s вышел из чата >>>\n", client->username);
                        }
                        char msg[] = "<<< До свидания! >>>\n";
                        send_to_client(clients[i].socket, msg);
                        close(clients[i].socket);
                        remove_client(clients[i].socket);
                    }
                    // Обработка смены имени
                    else if (strncmp(buffer, "/changename ", 12) == 0 && client->authenticated) {
                        char new_username[50];
                        sscanf(buffer + 12, "%s", new_username);
    
                        if (strlen(new_username) < 2) {
                            char msg[] = "<<< Имя должно быть не короче 2 символов >>>\n";
                            send_to_client(clients[i].socket, msg);
                        }
                        else if (login_user_hex(new_username, "")) {
                            char msg[] = "<<< Пользователь с таким именем уже существует >>>\n";
                            send_to_client(clients[i].socket, msg);
                        }
                        else {
                            char old_username[50];
                            strcpy(old_username, client->username);
        
                            if (update_username(old_username, new_username)) {
                                snprintf(client->username, sizeof(client->username), "%s", new_username);
            
                                char msg[BUFFER_SIZE];
                                snprintf(msg, sizeof(msg), "<<< Имя успешно изменено на %s! >>>\n", new_username);
                                send_to_client(clients[i].socket, msg);
            
                                char notify[BUFFER_SIZE];
                                snprintf(notify, sizeof(notify), "<<< %s сменил имя на %s >>>\n", 
                                old_username, new_username);
                                broadcast_message(notify, clients[i].socket);
            
                                printf("<<< %s сменил имя на %s >>>\n", old_username, new_username);
                            } else {
                                char msg[] = "<<< Ошибка при смене имени >>>\n";
                                send_to_client(clients[i].socket, msg);
                            }
                        }
                    }
                    // Обработка смены пароля (получаем HEX пароли)
                    else if (strncmp(buffer, "/changepass ", 12) == 0 && client->authenticated) {
                        char old_hex[256], new_hex[256];
                        sscanf(buffer + 12, "%s %s", old_hex, new_hex);
                        
                        // Проверяем старый пароль (сравниваем HEX строки)
                        if (login_user_hex(client->username, old_hex)) {
                            // Если пароль правильный, сохраняем новый (уже в HEX формате)
                            if (update_password_hex(client->username, new_hex)) {
                                char msg[] = "<<< Пароль успешно изменен! >>>\n";
                                send_to_client(clients[i].socket, msg);
                                printf("<<< %s изменил пароль >>>\n", client->username);
                            } else {
                                char msg[] = "<<< Ошибка при смене пароля >>>\n";
                                send_to_client(clients[i].socket, msg);
                            }
                        } else {
                            char msg[] = "<<< Неверный старый пароль >>>\n";
                            send_to_client(clients[i].socket, msg);
                        }
                    }
                    // Обычные сообщения
                    else if (client->authenticated) {
                        printf("%s: %s\n", client->username, buffer);
                        
                        char formatted[BUFFER_SIZE];
                        snprintf(formatted, sizeof(formatted), "%s: %s\n", client->username, buffer);
                        broadcast_message(formatted, clients[i].socket);
                    }
                    else {
                        char msg[] = "<<< Сначала войдите в систему: /login username password >>>\n";
                        send_to_client(clients[i].socket, msg);
                    }
                }
            }
        }
    }
    
    printf("\n<<< Сервер остановлен >>>\n");
    return 0;
}