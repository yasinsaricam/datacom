#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <ctype.h>

#define PORT 12345
#define BACKLOG 5
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define INITIAL_STORY_SIZE 1024
#define MAX_USERNAME 32

int server_fd;
char *story = NULL;
size_t story_size = 0;
int client_count = 0;

pthread_mutex_t story_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t vote_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int socket_fd;
    int id;
    char username[MAX_USERNAME];
} client_t;

client_t *clients[MAX_CLIENTS];

// Oylama yapısı
typedef struct {
    int active;                 
    char *pending_text;         
    int requesting_client_id;   
    int votes_yes;
    int votes_no;
    int votes_cast;
    int total_voters;
} VoteSession;

VoteSession current_vote;

void add_client(client_t *client) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = client;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void remove_client(client_t *client) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->id == client->id) {
            clients[i] = NULL;
            break;
        }
    }
    client_count--;
    pthread_mutex_unlock(&client_mutex);
}

void broadcast_message(const char *message) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL) {
            send(clients[i]->socket_fd, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void broadcast_message_except(const char *message, int except_id) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->id != except_id) {
            send(clients[i]->socket_fd, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

void do_update_story(const char *formatted_text) {
    pthread_mutex_lock(&story_mutex);
    
    if (story == NULL) {
        story_size = INITIAL_STORY_SIZE;
        story = (char *)malloc(story_size);
        story[0] = '\0';
    }

    size_t needed_size = strlen(story) + strlen(formatted_text) + 2;
    if (needed_size > story_size) {
        story_size = needed_size * 2;
        story = (char *)realloc(story, story_size);
    }

    if (strlen(story) > 0) {
        strcat(story, " ");
    }
    strcat(story, formatted_text);
    
    char update_message[BUFFER_SIZE + strlen(story)];
    snprintf(update_message, sizeof(update_message), "UPDATE %s", story);
    
    printf("\n=== GÜNCEL HİKAYE ===\n%s\n==================\n", story);

    pthread_mutex_unlock(&story_mutex);

    broadcast_message(update_message);
}

void finalize_vote() {
    pthread_mutex_lock(&vote_mutex);
    if (!current_vote.active) {
        pthread_mutex_unlock(&vote_mutex);
        return;
    }

    int yes = current_vote.votes_yes;
    int no = current_vote.votes_no;
    int req_id = current_vote.requesting_client_id;
    char *pending_text = current_vote.pending_text;

    char *temp_text = strdup(pending_text);

    free(current_vote.pending_text);
    current_vote.pending_text = NULL;
    current_vote.active = 0;
    current_vote.votes_yes = 0;
    current_vote.votes_no = 0;
    current_vote.votes_cast = 0;
    current_vote.requesting_client_id = 0;
    current_vote.total_voters = 0;

    pthread_mutex_unlock(&vote_mutex);

    if (yes >= no) {

        pthread_mutex_lock(&client_mutex);
        client_t *req_client = NULL;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != NULL && clients[i]->id == req_id) {
                req_client = clients[i];
                break;
            }
        }
        pthread_mutex_unlock(&client_mutex);

        char formatted_with_user[BUFFER_SIZE + strlen(temp_text) + MAX_USERNAME + 5];
        snprintf(formatted_with_user, sizeof(formatted_with_user), "%s (#%s)", temp_text, 
                 req_client ? req_client->username : "Bilinmeyen");

        do_update_story(formatted_with_user);
    } else {
        // Reddedildi
        broadcast_message("RESULT Reddedildi\n");
        
        pthread_mutex_lock(&client_mutex);
        client_t *req_client = NULL;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != NULL && clients[i]->id == req_id) {
                req_client = clients[i];
                break;
            }
        }
        pthread_mutex_unlock(&client_mutex);
        
        if (req_client) {
            const char *kick_msg = "Eklemeniz reddedildi, sunucudan çıkarılıyorsunuz.\n";
            send(req_client->socket_fd, kick_msg, strlen(kick_msg), 0);
            close(req_client->socket_fd);
            remove_client(req_client);
            free(req_client);
            
            char notify_msg[BUFFER_SIZE];
            snprintf(notify_msg, sizeof(notify_msg), 
                     "İstemci #%d oylamada başarısız oldu ve sunucudan çıkarıldı.\n", req_id);
            broadcast_message(notify_msg);
        }
        
        pthread_mutex_lock(&story_mutex);
        if (story != NULL && strlen(story) > 0) {
            char current_story_msg[BUFFER_SIZE + strlen(story)];
            snprintf(current_story_msg, sizeof(current_story_msg), "UPDATE %s", story);
            pthread_mutex_unlock(&story_mutex);
            broadcast_message(current_story_msg);
        } else {
            pthread_mutex_unlock(&story_mutex);
        }
    }

    free(temp_text);
}

void check_vote_status() {
    pthread_mutex_lock(&vote_mutex);
    if (!current_vote.active) {
        pthread_mutex_unlock(&vote_mutex);
        return;
    }
    int yes = current_vote.votes_yes;
    int no = current_vote.votes_no;
    int total = current_vote.total_voters;
    int cast = current_vote.votes_cast;
    pthread_mutex_unlock(&vote_mutex);

    if (yes > total/2 || no > total/2 || cast == total) {
        finalize_vote();
    }
}

void start_vote(const char *text, int requesting_id) {
    pthread_mutex_lock(&vote_mutex);
    current_vote.active = 1;
    current_vote.pending_text = strdup(text);
    current_vote.requesting_client_id = requesting_id;
    current_vote.votes_yes = 0;
    current_vote.votes_no = 0;
    current_vote.votes_cast = 0;

    pthread_mutex_lock(&client_mutex);
    current_vote.total_voters = client_count - 1;
    pthread_mutex_unlock(&client_mutex);

    pthread_mutex_unlock(&vote_mutex);

    char vote_message[BUFFER_SIZE + strlen(text) + 50];
    snprintf(vote_message, sizeof(vote_message), "VOTE %s\nEklensin mi? (YES/NO): ", text);
    broadcast_message_except(vote_message, requesting_id);
}

void process_vote_response(client_t *client, const char *response) {
    char vote[BUFFER_SIZE];
    strncpy(vote, response, BUFFER_SIZE-1);
    vote[BUFFER_SIZE-1] = '\0';
    for (int i = 0; vote[i]; i++) {
        vote[i] = toupper((unsigned char)vote[i]);
    }

    pthread_mutex_lock(&vote_mutex);
    if (!current_vote.active) {
        pthread_mutex_unlock(&vote_mutex);
        return;
    }

    if (client->id == current_vote.requesting_client_id) {
        pthread_mutex_unlock(&vote_mutex);
        return;
    }

    if (strncmp(vote, "YES", 3) == 0) {
        current_vote.votes_yes++;
        current_vote.votes_cast++;
    } else if (strncmp(vote, "NO", 2) == 0) {
        current_vote.votes_no++;
        current_vote.votes_cast++;
    } else {
        pthread_mutex_unlock(&vote_mutex);
        return;
    }

    pthread_mutex_unlock(&vote_mutex);
    check_vote_status();
}

void handle_signal(int sig) {
    printf("\nSunucu kapatılıyor...\n");
    
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL) {
            close(clients[i]->socket_fd);
            free(clients[i]);
            clients[i] = NULL;
        }
    }
    pthread_mutex_unlock(&client_mutex);
    
    pthread_mutex_lock(&story_mutex);
    free(story);
    pthread_mutex_unlock(&story_mutex);

    // Oylama yapısı temizliği
    pthread_mutex_lock(&vote_mutex);
    if (current_vote.pending_text) {
        free(current_vote.pending_text);
    }
    pthread_mutex_unlock(&vote_mutex);

    close(server_fd);
    exit(0);
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char welcome_message[BUFFER_SIZE];
    
    pthread_detach(pthread_self());
    
    send(client->socket_fd, "USERNAME ", 9, 0);
    
    bytes_received = recv(client->socket_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        strncpy(client->username, buffer, MAX_USERNAME - 1);
        client->username[MAX_USERNAME - 1] = '\0';
    } else {
        strcpy(client->username, "Anonim");
    }
    
    snprintf(welcome_message, sizeof(welcome_message), 
             "Hoş geldiniz %s! Siz #%d numaralı istemcisiniz.\n"
             "Hikayeye katkıda bulunmak için 'ADD <metin>' komutunu kullanın.\n", 
             client->username, client->id);
    send(client->socket_fd, welcome_message, strlen(welcome_message), 0);
    
    pthread_mutex_lock(&story_mutex);
    if (story != NULL && strlen(story) > 0) {
        char current_story[BUFFER_SIZE + strlen(story)];
        snprintf(current_story, sizeof(current_story), "UPDATE %s", story);
        send(client->socket_fd, current_story, strlen(current_story), 0);
    }
    pthread_mutex_unlock(&story_mutex);
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(client->socket_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // ADD komutu
        if (strncmp(buffer, "ADD ", 4) == 0) {
            pthread_mutex_lock(&vote_mutex);
            if (current_vote.active) {
                pthread_mutex_unlock(&vote_mutex);
                send(client->socket_fd, "Şu anda bir oylama sürüyor, lütfen bekleyin.\n", 47, 0);
                continue;
            }
            pthread_mutex_unlock(&vote_mutex);
            start_vote(buffer+4, client->id);
        }
        else if (strncmp(buffer, "YES", 3) == 0 || strncmp(buffer, "NO", 2) == 0) {
            process_vote_response(client, buffer);
        }
        else if (strcmp(buffer, "quit") == 0) {
            break;
        } else {
            char *msg = "Geçersiz komut! Kullanım: ADD <metin>, YES, NO veya quit\n";
            send(client->socket_fd, msg, strlen(msg), 0);
        }
    }
    
    printf("İstemci %s (#%d) bağlantısı kapandı.\n", client->username, client->id);
    remove_client(client);
    close(client->socket_fd);
    free(client);
    return NULL;
}

int main() {
    struct sockaddr_in address;
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    int opt = 1;
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    memset(clients, 0, sizeof(clients));
    memset(&current_vote, 0, sizeof(current_vote));
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Soket oluşturma hatası");
        exit(EXIT_FAILURE);
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt hatası");
        exit(EXIT_FAILURE);
    }
    
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Bind hatası");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, BACKLOG) == -1) {
        perror("Listen hatası");
        exit(EXIT_FAILURE);
    }
    
    printf("Hikaye paylaşım sunucusu %d portu üzerinden dinliyor...\n", PORT);
    printf("Güncel hikaye burada görüntülenecek.\n");
    printf("=== GÜNCEL HİKAYE ===\n(henüz hikaye yok)\n==================\n");
    
    while (1) {
        client_t *client = malloc(sizeof(client_t));
        client->socket_fd = accept(server_fd, (struct sockaddr *)&client_address, 
                                 &client_addr_len);
        
        if (client->socket_fd == -1) {
            free(client);
            continue;
        }
        
        pthread_mutex_lock(&client_mutex);
        if (client_count >= MAX_CLIENTS) {
            char *msg = "Sunucu dolu. Lütfen daha sonra tekrar deneyin.\n";
            send(client->socket_fd, msg, strlen(msg), 0);
            close(client->socket_fd);
            free(client);
            pthread_mutex_unlock(&client_mutex);
            continue;
        }
        
        client->id = ++client_count;
        pthread_mutex_unlock(&client_mutex);
        
        add_client(client);
        printf("Yeni istemci bağlandı (#%d)\n", client->id);
        
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, client) != 0) {
            perror("Thread oluşturma hatası");
            remove_client(client);
            close(client->socket_fd);
            free(client);
            continue;
        }
    }
    
    return 0;
}
