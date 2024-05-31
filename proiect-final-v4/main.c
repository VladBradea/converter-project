#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "conversii_audio.h"
#include "conversii.h"

#define PORT 8080
#define ADMIN_SOCKET_PATH "/tmp/admin_socket"
#define BUFFER_SIZE 4096

void handle_client(int client_fd);
void process_conversion(int client_fd, const char *input_file, int conversion_option);

void send_conversion_options(int client_fd, const char *extension) {
    char options[BUFFER_SIZE] = {0};

    if (strcmp(extension, "aac") == 0) {
        strcpy(options, "1. AAC to MP3\n2. AAC to WAV\n");
    } else if (strcmp(extension, "mp3") == 0) {
        strcpy(options, "3. MP3 to AAC (IN LUCRU)\n4. MP3 to WAV\n");
    } else if (strcmp(extension, "wav") == 0) {
        strcpy(options, "5. WAV to AAC\n6. WAV to MP3\n");
    } else if (strcmp(extension, "bmp") == 0) {
        strcpy(options, "7. BMP to JPEG\n8. BMP to PNG\n");
    } else if (strcmp(extension, "jpeg") == 0 || strcmp(extension, "jpg") == 0) {
        strcpy(options, "9. JPEG to BMP\n10. JPEG to PNG\n");
    } else if (strcmp(extension, "png") == 0) {
        strcpy(options, "11. PNG to BMP\n12. PNG to JPEG\n");
    } else if (strcmp(extension, "odt") == 0) {
        strcpy(options, "13. ODT to PDF\n14. ODT to TXT\n");
    } else if (strcmp(extension, "txt") == 0) {
        strcpy(options, "15. TXT to PDF\n16. TXT to ODT\n");
    } else if (strcmp(extension, "pdf") == 0) {
        strcpy(options, "17. PDF to ODT\n");
    } else {
        strcpy(options, "Unsupported file extension.\n");
    }

    write(client_fd, options, strlen(options));
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    char extension[BUFFER_SIZE] = {0};
    int conversion_option;

    // Read the file extension from the client
    if (read(client_fd, extension, sizeof(extension)) <= 0) {
        perror("Failed to read file extension");
        close(client_fd);
        return;
    }

    // Send the conversion options based on the extension
    send_conversion_options(client_fd, extension);

    // Read the conversion option from the client
    if (read(client_fd, buffer, sizeof(buffer)) <= 0) {
        perror("Failed to read conversion option");
        close(client_fd);
        return;
    }
    conversion_option = atoi(buffer);

    // Read the file size from the client
    size_t file_size;
    if (read(client_fd, &file_size, sizeof(file_size)) <= 0) {
        perror("Failed to read file size");
        close(client_fd);
        return;
    }

    // Read file from client
    char input_file_template[BUFFER_SIZE] = "/tmp/input_file_XXXXXX";
    int input_fd = mkstemp(input_file_template);
    if (input_fd == -1) {
        perror("Failed to create temporary input file");
        close(client_fd);
        return;
    }

    ssize_t bytes_received;
    size_t total_bytes_received = 0;

    while (total_bytes_received < file_size && (bytes_received = read(client_fd, buffer, BUFFER_SIZE)) > 0) {
        write(input_fd, buffer, bytes_received);
        total_bytes_received += bytes_received;
    }
    close(input_fd);

    // Rename the temporary file to include the original extension
    char input_file_with_extension[BUFFER_SIZE];
    snprintf(input_file_with_extension, sizeof(input_file_with_extension), "%s.%s", input_file_template, extension);
    rename(input_file_template, input_file_with_extension);

    process_conversion(client_fd, input_file_with_extension, conversion_option);

    // Delete the temporary input file
    unlink(input_file_with_extension);

    close(client_fd);
}


void send_file_to_client(int client_fd, const char *file_path, const char *extension) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open file");
        return;
    }
    // Send the file extension first
    write(client_fd, extension, strlen(extension) + 1);
    sleep(0.2);
    // Send the file size next
    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0) {
        perror("Failed to get file size");
        close(fd);
        return;
    }
    size_t file_size = file_stat.st_size;
    write(client_fd, &file_size, sizeof(file_size));

    // Send the file content
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if (write(client_fd, buffer, bytes_read) != bytes_read) {
            perror("Failed to send file");
            close(fd);
            return;
        }
    }

    close(fd);
}

void process_conversion(int client_fd, const char *input_file, int conversion_option) {
    char output_file_template[BUFFER_SIZE] = "/tmp/output_file_XXXXXX";
    int output_fd = mkstemp(output_file_template);
    if (output_fd == -1) {
        perror("Failed to create temporary output file");
        return;
    }
    close(output_fd); // Close the file descriptor, we will use the filename

    char output_file[BUFFER_SIZE];
    const char *extension;
    switch (conversion_option) {
        case 1:
            extension = ".mp3";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_aac_to_mp3(input_file, output_file);
            break;
        case 2:
            extension = ".wav";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_aac_to_wav(input_file, output_file);
            break;
        case 3:
            // extension = ".aac";
            // snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            // convert_mp3_to_aac(input_file, output_file);
            return; // Conversion not implemented
        case 4:
            extension = ".wav";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_mp3_to_wav(input_file, output_file);
            break;
        case 5:
            extension = ".aac";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_wav_to_aac(input_file, output_file);
            break;
        case 6:
            extension = ".mp3";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_wav_to_mp3(input_file, output_file);
            break;
        case 7:
            extension = ".jpeg";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_bmp_to_jpeg(input_file, output_file);
            break;
        case 8:
            extension = ".png";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_bmp_to_png(input_file, output_file);
            break;
        case 9:
            extension = ".bmp";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_jpeg_to_bmp(input_file, output_file);
            break;
        case 10:
            extension = ".png";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_jpeg_to_png(input_file, output_file);
            break;
        case 11:
            extension = ".bmp";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_png_to_bmp(input_file, output_file);
            break;
        case 12:
            extension = ".jpg";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_png_to_jpeg(input_file, output_file);
            break;
        case 13:
            extension = ".pdf";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_odt_to_pdf(input_file, output_file);
            break;
        case 14:
            extension = ".txt";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_odt_to_txt(input_file, output_file);
            break;
        case 15:
            extension = ".pdf";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_txt_to_pdf(input_file, output_file);
            break;
        case 16:
            extension = ".odt";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_txt_to_odt(input_file, output_file);
            break;
        case 17:
            extension = ".odt";
            snprintf(output_file, sizeof(output_file), "%s%s", output_file_template, extension);
            convert_pdf_to_odt(input_file, output_file);
            break;
        default:
            write(client_fd, "Invalid conversion option.\n", 27);
            return;
    }

    send_file_to_client(client_fd, output_file, extension);

    // Delete the temporary output file after sending
    unlink(output_file);
}

void *handle_admin_client(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_un address;

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, ADMIN_SOCKET_PATH, sizeof(address.sun_path) - 1);
    unlink(ADMIN_SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if ((client_fd = accept(server_fd, NULL, NULL)) < 0) {
        perror("accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    handle_client(client_fd);

    close(client_fd);
    close(server_fd);
    return NULL;
}

void *handle_simple_clients(void *arg) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        handle_client(new_socket);
        close(new_socket);
    }

    close(server_fd);
    return NULL;
}

int main() {
    pthread_t admin_thread, clients_thread;

    pthread_create(&admin_thread, NULL, handle_admin_client, NULL);
    pthread_create(&clients_thread, NULL, handle_simple_clients, NULL);

    pthread_join(admin_thread, NULL);
    pthread_join(clients_thread, NULL);

    return 0;
}
