#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_BUFFER 4096
#define MAX_PATH 1024
#define MAX_COMMAND 256

typedef enum {
    FTP_DISCONNECTED,
    FTP_CONNECTED,
    FTP_LOGGED_IN
} FTPState;

typedef struct {
    char server_hostname[256];
    char username[64];
    char password[64];
    char current_remote_dir[MAX_PATH];
    char current_local_dir[MAX_PATH];
    
    int control_socket;
    int data_socket;
    int server_port;
    int data_port;
    
    char server_ip[16];
    FTPState state;
} FTPClient;

// Function prototypes
int ftp_connect(FTPClient *client, const char *hostname);
int ftp_login(FTPClient *client, const char *username, const char *password);
int ftp_enter_passive_mode(FTPClient *client);
int ftp_list_remote_files(FTPClient *client);
int ftp_change_remote_directory(FTPClient *client, const char *path);
int ftp_make_remote_directory(FTPClient *client, const char *path);
int ftp_remove_remote_file(FTPClient *client, const char *filename);
int ftp_upload_file(FTPClient *client, const char *local_file, const char *remote_file);
int ftp_download_file(FTPClient *client, const char *remote_file, const char *local_file);
int ftp_rename_remote_file(FTPClient *client, const char *old_name, const char *new_name);
void ftp_close_connection(FTPClient *client);

// Utility functions
void trim_whitespace(char *str);
int send_ftp_command(FTPClient *client, const char *command);
int recv_ftp_response(FTPClient *client, char *response, size_t max_len);
void print_error(const char *message);

// Utility function to trim whitespace
void trim_whitespace(char *str) {
    char *start = str;
    char *end = str + strlen(str) - 1;

    // Trim leading whitespace
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start++;
    }

    // Trim trailing whitespace
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        end--;
    }

    // Move trimmed string to beginning
    memmove(str, start, end - start + 1);
    str[end - start + 1] = '\0';
}

// Send FTP command and check for basic error
int send_ftp_command(FTPClient *client, const char *command) {
    char full_command[MAX_COMMAND];
    snprintf(full_command, sizeof(full_command), "%s\r\n", command);
    
    int sent_bytes = send(client->control_socket, full_command, strlen(full_command), 0);
    if (sent_bytes < 0) {
        print_error("Failed to send command");
        return -1;
    }
    
    return 0;
}

// Receive FTP response
int recv_ftp_response(FTPClient *client, char *response, size_t max_len) {
    int received_bytes = recv(client->control_socket, response, max_len - 1, 0);
    if (received_bytes < 0) {
        print_error("Failed to receive server response");
        return -1;
    }
    
    response[received_bytes] = '\0';
    trim_whitespace(response);
    
    return atoi(response);
}

// Print error with system error description
void print_error(const char *message) {
    fprintf(stderr, "%s: %s\n", message, strerror(errno));
}

// Connect to FTP server
int ftp_connect(FTPClient *client, const char *hostname) {
    struct hostent *host_info;
    struct sockaddr_in server_addr;
    char response[MAX_BUFFER];

    // Resolve hostname
    host_info = gethostbyname(hostname);
    if (!host_info) {
        herror("gethostbyname");
        return -1;
    }

    // Create socket
    client->control_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->control_socket < 0) {
        print_error("Socket creation failed");
        return -1;
    }

    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; //ipv4
    server_addr.sin_port = htons(21);  // Standard FTP control port, convert in bigendian
    memcpy(&server_addr.sin_addr, host_info->h_addr, host_info->h_length);

    // Connect to server
    if (connect(client->control_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        print_error("Connection failed");
        close(client->control_socket);
        return -1;
    }

    // Store server details
    strcpy(client->server_hostname, hostname);
    strcpy(client->server_ip, inet_ntoa(server_addr.sin_addr));
    
    // Receive welcome message
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 220) {
        fprintf(stderr, "Server connection failed: %s\n", response);
        close(client->control_socket);
        return -1;
    }

    client->state = FTP_CONNECTED;
    return 0;
}

// Login to FTP server
int ftp_login(FTPClient *client, const char *username, const char *password) {
    char command[MAX_COMMAND];
    char response[MAX_BUFFER];
    
    // Send username
    snprintf(command, sizeof(command), "USER %s", username);
    if (send_ftp_command(client, command) < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 331) {
        fprintf(stderr, "Username error: %s\n", response);
        return -1;
    }
    
    // Send password
    snprintf(command, sizeof(command), "PASS %s", password);
    if (send_ftp_command(client, command) < 0) return -1;
    
    response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 230) {
        fprintf(stderr, "Login failed: %s\n", response);
        return -1;
    }
    
    // Store login details
    strcpy(client->username, username);
    strcpy(client->password, password);
    
    client->state = FTP_LOGGED_IN;
    return 0;
}

// Enter passive mode
int ftp_enter_passive_mode(FTPClient *client) {
    char response[MAX_BUFFER];
    int h1, h2, h3, h4, p1, p2;
    
    if (send_ftp_command(client, "PASV") < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 227) {
        fprintf(stderr, "Passive mode failed: %s\n", response);
        return -1;
    }
    
    // Parse passive mode response
    if (sscanf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", 
               &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        fprintf(stderr, "Failed to parse passive mode response\n");
        return -1;
    }
    
    // Construct IP and port
    snprintf(client->server_ip, sizeof(client->server_ip), 
             "%d.%d.%d.%d", h1, h2, h3, h4);
    client->data_port = p1 * 256 + p2;
    
    // Create data socket
    client->data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->data_socket < 0) {
        print_error("Data socket creation failed");
        return -1;
    }
    
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(client->data_port);
    inet_aton(client->server_ip, &data_addr.sin_addr);
    
    // Connect data socket
    if (connect(client->data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
        print_error("Data connection failed");
        close(client->data_socket);
        return -1;
    }
    
    return 0;
}

// List remote files
int ftp_list_remote_files(FTPClient *client) {
    char response[MAX_BUFFER];
    char data_buffer[MAX_BUFFER];
    
    // Enter passive mode
    if (ftp_enter_passive_mode(client) < 0) return -1;
    
    // Send LIST command
    if (send_ftp_command(client, "LIST") < 0) return -1;
    
    // Get initial response
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 150) {
        fprintf(stderr, "LIST command failed: %s\n", response);
        close(client->data_socket);
        return -1;
    }
    
    // Read data
    printf("Remote Files:\n");
    int bytes_read;
    while ((bytes_read = recv(client->data_socket, data_buffer, sizeof(data_buffer) - 1, 0)) > 0) {
        data_buffer[bytes_read] = '\0';
        printf("%s", data_buffer);
    }
    
    // Close data connection
    close(client->data_socket);
    
    // Get final response
    response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 226) {
        fprintf(stderr, "File listing incomplete: %s\n", response);
        return -1;
    }
    
    return 0;
}

// Change remote directory
int ftp_change_remote_directory(FTPClient *client, const char *path) {
    char command[MAX_COMMAND];
    char response[MAX_BUFFER];
    
    snprintf(command, sizeof(command), "CWD %s", path);
    if (send_ftp_command(client, command) < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 250) {
        fprintf(stderr, "Change directory failed: %s\n", response);
        return -1;
    }
    
    strncpy(client->current_remote_dir, path, sizeof(client->current_remote_dir) - 1);
    return 0;
}

// Download file
int ftp_download_file(FTPClient *client, const char *remote_file, const char *local_file) {
    char response[MAX_BUFFER];
    char command[MAX_COMMAND];
    FILE *local_fp;
    
    // Enter passive mode
    if (ftp_enter_passive_mode(client) < 0) return -1;
    
    // Open local file
    local_fp = fopen(local_file, "wb");
    if (!local_fp) {
        print_error("Failed to open local file");
        return -1;
    }
    
    // Send RETR command
    snprintf(command, sizeof(command), "RETR %s", remote_file);
    if (send_ftp_command(client, command) < 0) {
        fclose(local_fp);
        return -1;
    }
    
    // Initial response
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 150) {
        fprintf(stderr, "File retrieval failed: %s\n", response);
        fclose(local_fp);
        close(client->data_socket);
        return -1;
    }
    
    // Download file
    char data_buffer[MAX_BUFFER];
    int bytes_read;
    while ((bytes_read = recv(client->data_socket, data_buffer, sizeof(data_buffer), 0)) > 0) {
        fwrite(data_buffer, 1, bytes_read, local_fp);
    }
    
    // Close file and data connection
    fclose(local_fp);
    close(client->data_socket);
    
    // Final response
    response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 226) {
        fprintf(stderr, "File download incomplete: %s\n", response);
        return -1;
    }
    
    printf("File downloaded successfully: %s\n", local_file);
    return 0;
}

// Upload file
int ftp_upload_file(FTPClient *client, const char *local_file, const char *remote_file) {
    char response[MAX_BUFFER];
    char command[MAX_COMMAND];
    FILE *local_fp;
    
    // Enter passive mode
    if (ftp_enter_passive_mode(client) < 0) return -1;
    
    // Open local file
    local_fp = fopen(local_file, "rb");
    if (!local_fp) {
        print_error("Failed to open local file");
        return -1;
    }
    
    // Send STOR command
    snprintf(command, sizeof(command), "STOR %s", remote_file);
    if (send_ftp_command(client, command) < 0) {
        fclose(local_fp);
        return -1;
    }
    
    // Initial response
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 150) {
        fprintf(stderr, "File upload failed: %s\n", response);
        fclose(local_fp);
        close(client->data_socket);
        return -1;
    }
    
    // Upload file
    char data_buffer[MAX_BUFFER];
    size_t bytes_read;
    while ((bytes_read = fread(data_buffer, 1, sizeof(data_buffer), local_fp)) > 0) {
        send(client->data_socket, data_buffer, bytes_read, 0);
    }
    
    // Close file and data connection
    fclose(local_fp);
    close(client->data_socket);

    // Final response
    response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 226) {
        fprintf(stderr, "File upload incomplete: %s\n",response);
        return -1;
    }
    
    printf("File uploaded successfully: %s\n", local_file);
    return 0;
}

// Create remote directory
int ftp_make_remote_directory(FTPClient *client, const char *path) {
    char command[MAX_COMMAND];
    char response[MAX_BUFFER];
    
    snprintf(command, sizeof(command), "MKD %s", path);
    if (send_ftp_command(client, command) < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 257) {
        fprintf(stderr, "Create directory failed: %s\n", response);
        return -1;
    }
    
    printf("Directory created: %s\n", path);
    return 0;
}

// Remove remote file
int ftp_remove_remote_file(FTPClient *client, const char *filename) {
    char command[MAX_COMMAND];
    char response[MAX_BUFFER];
    
    snprintf(command, sizeof(command), "DELE %s", filename);
    if (send_ftp_command(client, command) < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 250) {
        fprintf(stderr, "File deletion failed: %s\n", response);
        return -1;
    }
    
    printf("File deleted: %s\n", filename);
    return 0;
}

// Rename remote file
int ftp_rename_remote_file(FTPClient *client, const char *old_name, const char *new_name) {
    char command[MAX_COMMAND];
    char response[MAX_BUFFER];
    
    // Send RNFR (Rename From) command
    snprintf(command, sizeof(command), "RNFR %s", old_name);
    if (send_ftp_command(client, command) < 0) return -1;
    
    int response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 350) {
        fprintf(stderr, "Rename preparation failed: %s\n", response);
        return -1;
    }
    
    // Send RNTO (Rename To) command
    snprintf(command, sizeof(command), "RNTO %s", new_name);
    if (send_ftp_command(client, command) < 0) return -1;
    
    response_code = recv_ftp_response(client, response, sizeof(response));
    if (response_code != 250) {
        fprintf(stderr, "File rename failed: %s\n", response);
        return -1;
    }
    
    printf("File renamed from %s to %s\n", old_name, new_name);
    return 0;
}

// Close FTP connection
void ftp_close_connection(FTPClient *client) {
    if (client->state == FTP_LOGGED_IN || client->state == FTP_CONNECTED) {
        send_ftp_command(client, "QUIT");
        recv_ftp_response(client, NULL, 0);
    }
    
    if (client->control_socket > 0) {
        close(client->control_socket);
        client->control_socket = -1;
    }
    
    if (client->data_socket > 0) {
        close(client->data_socket);
        client->data_socket = -1;
    }
    
    client->state = FTP_DISCONNECTED;
    memset(client->username, 0, sizeof(client->username));
    memset(client->password, 0, sizeof(client->password));
}

// Interactive menu for FTP client
void ftp_client_menu(FTPClient *client) {
    int choice;
    char hostname[256], username[64], password[64];
    char remote_path[MAX_PATH], local_path[MAX_PATH];
    
    while (1) {
        printf("\n--- FTP Client Menu ---\n");
        printf("1. Connect to Server\n");
        printf("2. Login\n");
        printf("3. List Remote Files\n");
        printf("4. Change Remote Directory\n");
        printf("5. Download File\n");
        printf("6. Upload File\n");
        printf("7. Create Remote Directory\n");
        printf("8. Delete Remote File\n");
        printf("9. Rename Remote File\n");
        printf("0. Exit\n");
        printf("Enter your choice: ");
        
        scanf("%d", &choice);
        getchar(); // Consume newline
        
        switch (choice) {
            case 1:
                printf("Enter hostname: ");
                fgets(hostname, sizeof(hostname), stdin);
                hostname[strcspn(hostname, "\n")] = 0;
                ftp_connect(client, hostname);
                break;
            
            case 2:
                printf("Enter username: ");
                fgets(username, sizeof(username), stdin);
                username[strcspn(username, "\n")] = 0;
                
                printf("Enter password: ");
                fgets(password, sizeof(password), stdin);
                password[strcspn(password, "\n")] = 0;
                
                ftp_login(client, username, password);
                break;
            
            case 3:
                ftp_list_remote_files(client);
                break;
            
            case 4:
                printf("Enter remote directory path: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                ftp_change_remote_directory(client, remote_path);
                break;
            
            case 5:
                printf("Enter remote file to download: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                
                printf("Enter local file path: ");
                fgets(local_path, sizeof(local_path), stdin);
                local_path[strcspn(local_path, "\n")] = 0;
                
                ftp_download_file(client, remote_path, local_path);
                break;
            
            case 6:
                printf("Enter local file to upload: ");
                fgets(local_path, sizeof(local_path), stdin);
                local_path[strcspn(local_path, "\n")] = 0;
                
                printf("Enter remote file path: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                
                ftp_upload_file(client, local_path, remote_path);
                break;
            
            case 7:
                printf("Enter new remote directory path: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                ftp_make_remote_directory(client, remote_path);
                break;
            
            case 8:
                printf("Enter remote file to delete: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                ftp_remove_remote_file(client, remote_path);
                break;
            
            case 9:
                printf("Enter old filename: ");
                fgets(remote_path, sizeof(remote_path), stdin);
                remote_path[strcspn(remote_path, "\n")] = 0;
                
                printf("Enter new filename: ");
                fgets(local_path, sizeof(local_path), stdin);
                local_path[strcspn(local_path, "\n")] = 0;
                
                ftp_rename_remote_file(client, remote_path, local_path);
                break;
            
            case 0:
                ftp_close_connection(client);
                printf("Disconnected from server.\n");
                return;
            
            default:
                printf("Invalid choice. Try again.\n");
                break;
        }
    }
}

/*int main() {
    FTPClient client = {0};
    
    // Initialize client state
    client.control_socket = -1;
    client.data_socket = -1;
    client.state = FTP_DISCONNECTED;
    
    // Start interactive menu
    ftp_client_menu(&client);
    
    return 0;
}*/

int main() {
    FTPClient client;
    const char *hostname = "ftp.up.pt"; // Substitua pelo hostname do servidor
    const char *username = "anonymous"; // Nome de usuário anônimo
    const char *password = "";          // Senha vazia para acesso anônimo

    // Inicializar a estrutura FTPClient
    memset(&client, 0, sizeof(client));

    // Conectar ao servidor
    if (ftp_connect(&client, hostname) < 0) {
        fprintf(stderr, "Erro: Falha ao conectar ao servidor %s.\n", hostname);
        return 1;
    }
    printf("Conectado ao servidor %s\n", hostname);

    // Fazer login no servidor
    if (ftp_login(&client, username, password) < 0) {
        fprintf(stderr, "Erro: Falha ao fazer login com as credenciais fornecidas.\n");
        ftp_close_connection(&client);
        return 1;
    }
    printf("Login realizado com sucesso.\n");

    // Listar arquivos no diretório remoto
    printf("Listando arquivos no diretório remoto:\n");
    if (ftp_list_remote_files(&client) < 0) {
        fprintf(stderr, "Erro: Não foi possível listar os arquivos remotos.\n");
    }

    // Fazer o download de um arquivo
    const char *remote_file = "/pub/gnu/emacs/elisp-manual-21-2.8.tar.gz"; // Substitua com o caminho real
    const char *local_file = "elisp-manual-21-2.8.tar.gz";
    if (ftp_download_file(&client, remote_file, local_file) == 0) {
        printf("Arquivo '%s' baixado com sucesso como '%s'.\n", remote_file, local_file);
    } else {
        fprintf(stderr, "Erro: Falha ao baixar o arquivo '%s'.\n", remote_file);
    }

    // Fechar conexão
    ftp_close_connection(&client);
    printf("Conexão encerrada com o servidor.\n");

    return 0;
}
