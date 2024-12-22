#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>

#define PORT 12345
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"
#define MAX_USERNAME 32

int client_fd;
pthread_t receive_thread;
char username[MAX_USERNAME];
int waiting_for_username = 0;
int waiting_for_vote = 0;

void handle_signal(int sig) {
    printf("\nİstemci kapatılıyor...\n");
    close(client_fd);
    exit(0);
}

void clear_screen() {
    printf("\033[H\033[J");
}

void clean_story(char *story) {
    char *src = story;
    char *dst = story;
    char *end;
    
    while (*src) {
        if (*src == '(' && *(src+1) == '#') {
            // k.Adı sil 
            end = strchr(src, ')');
            if (end) {
                src = end + 1;
                if (*src == ' ') src++;
                continue;
            }
        }
        *dst = *src;
        dst++;
        src++;
    }
    *dst = '\0';
}

void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    char clean_buffer[BUFFER_SIZE];
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            printf("\nSunucu bağlantısı koptu.\n");
            close(client_fd);
            exit(1);
        }
        
        buffer[bytes_received] = '\0';
        
        if (strncmp(buffer, "USERNAME ", 9) == 0) {
            waiting_for_username = 1;
            clear_screen();
            printf("Hoşgeldiniz! Kullanıcı adınızı girin: ");
            fflush(stdout);
        } else if (strncmp(buffer, "UPDATE ", 7) == 0) {
            clear_screen();
            strcpy(clean_buffer, buffer + 7);
            clean_story(clean_buffer);
            printf("=== HİKAYE ===\n%s\n==================\n", clean_buffer);
            if (!waiting_for_vote) {
                printf("\nKomut (ADD <metin>, YES, NO veya quit): ");
                fflush(stdout);
            }
        } else if (strncmp(buffer, "Hoş geldiniz", 12) == 0) {
            waiting_for_username = 0;
            clear_screen();
            printf("%s", buffer);
            printf("\nKomut (ADD <metin>, YES, NO veya quit): ");
            fflush(stdout);
        } else if (strncmp(buffer, "VOTE ", 5) == 0) {
            clear_screen();
            // Oylama metni
            printf("Oylama! Eklenmek istenen metin:\n%s\n", buffer+5);
            printf("Lütfen YES veya NO giriniz: ");
            fflush(stdout);
            waiting_for_vote = 1;
        } else if (strncmp(buffer, "RESULT", 6) == 0) {
            clear_screen();
            printf("%s\n", buffer);
            waiting_for_vote = 0;
            printf("Komut (ADD <metin>, YES, NO veya quit): ");
            fflush(stdout);
        } else {
            // Diğer mesajlar
            printf("%s", buffer);
            if (!waiting_for_vote && !waiting_for_username) {
                printf("\nKomut (ADD <metin>, YES, NO veya quit): ");
            }
            fflush(stdout);
        }
    }
    
    return NULL;
}

int main() {
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];
    
    signal(SIGINT, handle_signal);
    
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Soket oluşturma hatası");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr) <= 0) {
        perror("Geçersiz adres");
        exit(EXIT_FAILURE);
    }
    
    if (connect(client_fd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Bağlantı hatası");
        exit(EXIT_FAILURE);
    }
    
    if (pthread_create(&receive_thread, NULL, receive_messages, NULL) != 0) {
        perror("Thread oluşturma hatası");
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        if (fgets(buffer, BUFFER_SIZE - 1, stdin) == NULL) {
            break;
        }
        buffer[strcspn(buffer, "\n")] = 0;
        
        if (waiting_for_username) {
            if (strlen(buffer) > 0) {
                strncpy(username, buffer, MAX_USERNAME - 1);
                username[MAX_USERNAME - 1] = '\0';
                send(client_fd, username, strlen(username), 0);
            }
            continue;
        }

        if (waiting_for_vote) {
            for (int i = 0; buffer[i]; i++) {
                buffer[i] = toupper((unsigned char)buffer[i]);
            }

            if (strncmp(buffer, "YES", 3) == 0 || strncmp(buffer, "NO", 2) == 0) {
                send(client_fd, buffer, strlen(buffer), 0);
                waiting_for_vote = 0;
            } else {
                printf("Lütfen YES veya NO giriniz: ");
                fflush(stdout);
            }
            continue;
        }

        if (strcmp(buffer, "quit") == 0) {
            break;
        }

        if (strncmp(buffer, "ADD ", 4) == 0) {
            if (send(client_fd, buffer, strlen(buffer), 0) == -1) {
                perror("Mesaj gönderme hatası");
                break;
            }
        } else if (strncmp(buffer, "YES", 3) == 0 || strncmp(buffer, "NO", 2) == 0) {
            printf("Şu anda oylama yok! Komut (ADD <metin> veya quit): ");
            fflush(stdout);
        } else if (strlen(buffer) > 0) {
            printf("Geçersiz komut! Kullanım: ADD <metin>, YES, NO veya quit\n");
            printf("\nKomut (ADD <metin>, YES, NO veya quit): ");
            fflush(stdout);
        }
    }
    
    printf("İstemci kapatılıyor...\n");
    close(client_fd);
    return 0;
}
