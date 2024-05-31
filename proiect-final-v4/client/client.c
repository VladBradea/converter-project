#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 8080
#define ADMIN_SOCKET_PATH "/tmp/admin_socket"
#define BUFFER_SIZE 4095

void send_file(int socket_fd, const char *file_path);
void receive_file(int socket_fd, const char *input_path);
void generate_output_path(const char *input_path, const char *new_extension, char *output_path);
void communicate_with_server(int socket_fd);
void connect_to_admin_server();
void connect_to_simple_server();

void send_file(int socket_fd, const char *file_path) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open file");
        return;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0) {
        perror("Failed to get file size");
        close(fd);
        return;
    }
    size_t file_size = file_stat.st_size;
    write(socket_fd, &file_size, sizeof(file_size));

    printf("Size of the file being sent: %zu bytes\n", file_size);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if (write(socket_fd, buffer, bytes_read) != bytes_read) {
            perror("Failed to send file");
            close(fd);
            return;
        }
    }

    close(fd);
}

void receive_file(int socket_fd, const char *input_path) {
    char buffer[BUFFER_SIZE];

    // Read the new file extension
    if (read(socket_fd, buffer, sizeof(buffer)) <= 0) {
        perror("Failed to read new file extension");
        return;
    }
    const char *new_extension = buffer;

    // Generate the full output path with the new extension
    char output_file_path[BUFFER_SIZE];
    generate_output_path(input_path, new_extension, output_file_path);

    int fd = open(output_file_path, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        perror("Failed to open file for writing");
        return;
    }

    size_t file_size;
    if (read(socket_fd, &file_size, sizeof(file_size)) <= 0) {
        perror("Failed to read file size");
        close(fd);
        return;
    }

    printf("Size of the received file: %zu bytes\n", file_size);

    ssize_t bytes_received;
    size_t total_bytes_received = 0;

    while (total_bytes_received < file_size && (bytes_received = read(socket_fd, buffer, BUFFER_SIZE)) > 0) {
        if (write(fd, buffer, bytes_received) != bytes_received) {
            perror("Failed to write to file");
            close(fd);
            return;
        }
        total_bytes_received += bytes_received;
    }

    close(fd);

    if (total_bytes_received == file_size) {
        printf("File received successfully\n");
    } else {
        printf("File reception incomplete: received %zu bytes out of %zu bytes\n", total_bytes_received, file_size);
    }

    printf("Converted file saved to: %s\n", output_file_path);
}

void generate_output_path(const char *input_path, const char *new_extension, char *output_path) {
    const char *dot = strrchr(input_path, '.');
    if (!dot || dot == input_path) {
        snprintf(output_path, BUFFER_SIZE, "%s_modified%s", input_path, new_extension);
    } else {
        size_t base_len = dot - input_path;
        snprintf(output_path, base_len + strlen(new_extension) + 10, "%.*s_modified%s", (int)base_len, input_path, new_extension);
    }
}

void communicate_with_server(int socket_fd) {
    char buffer[BUFFER_SIZE] = {0};
    int option;

    while (1) {
        char input_path[BUFFER_SIZE], output_path[BUFFER_SIZE];
        printf("Enter input file path: ");
        scanf("%s", input_path);

        // Determine file extension
        const char *dot = strrchr(input_path, '.');
        if (!dot || dot == input_path) {
            printf("Invalid file extension.\n");
            continue;
        }
        const char *extension = dot + 1;

        // Send file extension to the server
        write(socket_fd, extension, strlen(extension) + 1);

        // Receive and display conversion options from the server
        if (read(socket_fd, buffer, sizeof(buffer)) <= 0) {
            perror("Failed to read conversion options");
            close(socket_fd);
            return;
        }
        printf("Conversion options:\n%s", buffer);

        // Get user's choice for conversion
        printf("Choose an option:\n");
        scanf("%d", &option);
        sprintf(buffer, "%d", option);
        write(socket_fd, buffer, strlen(buffer) + 1);

        // Send the input file to the server
        send_file(socket_fd, input_path);

        // Receive the converted file from the server
        receive_file(socket_fd, input_path);
    }
}

void connect_to_admin_server() {
    int socket_fd;
    struct sockaddr_un address;

    if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, ADMIN_SOCKET_PATH, sizeof(address.sun_path) - 1);

    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    communicate_with_server(socket_fd);
}

void connect_to_simple_server() {
    int socket_fd;
    struct sockaddr_in address;

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Connection Failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    communicate_with_server(socket_fd);
}

int main() {
    int choice;
    printf("Choose server to connect to:\n");
    printf("1. Admin Server\n");
    printf("2. Simple Server\n");
    scanf("%d", &choice);

    if (choice == 1) {
        connect_to_admin_server();
    } else if (choice == 2) {
        connect_to_simple_server();
    } else {
        printf("Invalid choice.\n");
    }

    return 0;
}
