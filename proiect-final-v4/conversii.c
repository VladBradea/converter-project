#include "conversii.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <png.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#pragma pack(push, 1)


// JPEG error handler
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr *my_error_ptr;

METHODDEF(void) my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

// Helper function to convert BGR to RGB
void convert_BGR_to_RGB(unsigned char *data, int size) {
    for (int i = 0; i < size; i += 3) {
        uint8_t temp = data[i];
        data[i] = data[i + 2];
        data[i + 2] = temp;
    }
}

// Read BMP file
int read_BMP_file(const char *filename, unsigned char **data, int *width, int *height) {
    BMPFileHeader1 fileHeader;
    BMPInfoHeader1 infoHeader;
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open file '%s'\n", filename);
        return 0;
    }

    if (fread(&fileHeader, sizeof(BMPFileHeader1), 1, file) != 1 ||
        fread(&infoHeader, sizeof(BMPInfoHeader1), 1, file) != 1) {
        fprintf(stderr, "Failed to read BMP headers\n");
        fclose(file);
        return 0;
    }

    if (infoHeader.bitCount != 24) {
        fprintf(stderr, "Unsupported bit depth: %d\n", infoHeader.bitCount);
        fclose(file);
        return 0;
    }

    fseek(file, fileHeader.offset, SEEK_SET);
    *width = infoHeader.width;
    *height = infoHeader.height;
    int row_padded = (*width * 3 + 3) & (~3);
    int dataSize = row_padded * *height;
    *data = malloc(dataSize);

    if (!*data) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return 0;
    }

    if (fread(*data, dataSize, 1, file) != 1) {
        fprintf(stderr, "Failed to read BMP data\n");
        free(*data);
        fclose(file);
        return 0;
    }

    fclose(file);
    convert_BGR_to_RGB(*data, dataSize);
    return 1;
}

// Write JPEG file
void write_JPEG_file(const char *filename, unsigned char *img_data, int width, int height, int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *outfile;
    JSAMPROW row_pointer[1];

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    if ((outfile = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "Can't open %s for writing\n", filename);
        exit(1);
    }

    jpeg_stdio_dest(&cinfo, outfile);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    row_pointer[0] = img_data;
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &img_data[cinfo.next_scanline * width * 3];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    fclose(outfile);
    jpeg_destroy_compress(&cinfo);
}

// Write PNG file
int write_PNG_file(const char *filename, unsigned char *image, int width, int height) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Cannot open file %s\n", filename);
        return 0;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return 0;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 0;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    png_bytep row_pointers[height];
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte *)&image[y * width * 3];
    }
    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return 1;
}

// Read JPEG file
int read_JPEG_file(const char *filename, unsigned char **image_buffer, int *width, int *height) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    FILE *infile;
    JSAMPARRAY buffer;
    int row_stride;

    if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "Can't open %s\n", filename);
        return 0;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return 0;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;
    *image_buffer = (unsigned char *)malloc(cinfo.output_height * row_stride);
    *width = cinfo.output_width;
    *height = cinfo.output_height;

    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy(*image_buffer + (cinfo.output_scanline - 1) * row_stride, buffer[0], row_stride);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    return 1;
}

// Read PNG file
int read_PNG_file(const char *filename, unsigned char **image, int *width, int *height) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open file %s\n", filename);
        return 0;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return 0;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 0;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    *width = png_get_image_width(png, info);
    *height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int rowbytes = png_get_rowbytes(png, info);
    *image = (unsigned char *)malloc(rowbytes * *height);

    png_bytep row_pointers[*height];
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = *image + y * rowbytes;
    }

    png_read_image(png, row_pointers);

    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
    return 1;
}

// Write BMP file
void write_BMP_file(const char *filename, unsigned char *image_buffer, int width, int height) {
    FILE *outfile = fopen(filename, "wb");
    if (!outfile) {
        fprintf(stderr, "Failed to open file for writing\n");
        return;
    }

    BMPFileHeader1 fileHeader;
    BMPInfoHeader1 infoHeader;

    int rowPadding = (4 - (width * 3 % 4)) % 4;
    int rowSize = width * 3 + rowPadding;
    int imageSize = rowSize * height;

    // Setup BMP File Header
    fileHeader.type = 0x4D42; // 'BM'
    fileHeader.size = sizeof(BMPFileHeader1) + sizeof(BMPInfoHeader1) + imageSize;
    fileHeader.reserved1 = 0;
    fileHeader.reserved2 = 0;
    fileHeader.offset = sizeof(BMPFileHeader1) + sizeof(BMPInfoHeader1);

    // Setup BMP Info Header
    infoHeader.size = sizeof(BMPInfoHeader1);
    infoHeader.width = width;
    infoHeader.height = height;
    infoHeader.planes = 1;
    infoHeader.bitCount = 24;
    infoHeader.compression = 0; // BI_RGB
    infoHeader.sizeImage = imageSize;
    infoHeader.xPelsPerMeter = 0;
    infoHeader.yPelsPerMeter = 0;
    infoHeader.clrUsed = 0;
    infoHeader.clrImportant = 0;

    // Write headers
    fwrite(&fileHeader, sizeof(BMPFileHeader1), 1, outfile);
    fwrite(&infoHeader, sizeof(BMPInfoHeader1), 1, outfile);

    // Write pixel data
    for (int y = height - 1; y >= 0; y--) { // BMP images are stored bottom-to-top
        fwrite(image_buffer + y * width * 3, 3, width, outfile);
        // Pad each row to a multiple of 4 bytes
        if (rowPadding > 0) {
            uint8_t padding[3] = {0};
            fwrite(padding, 1, rowPadding, outfile);
        }
    }

    fclose(outfile);
}

// Conversion functions
void convert_bmp_to_jpeg(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_BMP_file(input_file, &image_data, &width, &height)) {
        write_JPEG_file(output_file, image_data, width, height, 75);  // Using a default quality of 75
        free(image_data);
    }
}

void convert_bmp_to_png(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_BMP_file(input_file, &image_data, &width, &height)) {
        write_PNG_file(output_file, image_data, width, height);
        free(image_data);
    }
}

void convert_jpeg_to_bmp(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_JPEG_file(input_file, &image_data, &width, &height)) {
        write_BMP_file(output_file, image_data, width, height);
        free(image_data);
    }
}

void convert_jpeg_to_png(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_JPEG_file(input_file, &image_data, &width, &height)) {
        write_PNG_file(output_file, image_data, width, height);
        free(image_data);
    }
}

void convert_png_to_bmp(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_PNG_file(input_file, &image_data, &width, &height)) {
        write_BMP_file(output_file, image_data, width, height);
        free(image_data);
    }
}

void convert_png_to_jpeg(const char *input_file, const char *output_file) {
    unsigned char *image_data;
    int width, height;
    if (read_PNG_file(input_file, &image_data, &width, &height)) {
        write_JPEG_file(output_file, image_data, width, height, 75);  // Using a default quality of 75
        free(image_data);
    }
}


void convert_pdf_to_odt(const char *input_path, const char *output_path) {
    printf("Converting PDF to ODT: %s to %s\n", input_path, output_path);

    char *ext = strrchr(input_path, '.');
    if (ext == NULL || strcmp(ext, ".pdf") != 0) {
        fprintf(stderr, "Error: Input file %s is not .pdf\n", input_path);
        return;
    }
    if (access(input_path, F_OK) != 0) {
        fprintf(stderr, "Error: Input file %s doesn't exist\n", input_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/libreoffice", "libreoffice", "--headless", "--convert-to", "odt", input_path, "--outdir", output_path, NULL);
        fprintf(stderr, "Error: execl failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully converted PDF to ODT.\n");
        } else {
            fprintf(stderr, "Error: Conversion failed.\n");
        }
    }
}

void convert_odt_to_pdf(const char *input_path, const char *output_path) {
    printf("Converting ODT to PDF: %s to %s\n", input_path, output_path);

    char *ext = strrchr(input_path, '.');
    if (ext == NULL || strcmp(ext, ".odt") != 0) {
        fprintf(stderr, "Error: Input file %s is not .odt\n", input_path);
        return;
    }
    if (access(input_path, F_OK) != 0) {
        fprintf(stderr, "Error: Input file %s doesn't exist\n", input_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/libreoffice", "libreoffice", "--headless", "--convert-to", "pdf", input_path, "--outdir", output_path, NULL);
        fprintf(stderr, "Error: execl failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully converted ODT to PDF.\n");
        } else {
            fprintf(stderr, "Error: Conversion failed.\n");
        }
    }
}

void convert_odt_to_txt(const char *input_path, const char *output_path) {
    printf("Converting ODT to TXT: %s to %s\n", input_path, output_path);

    char *ext = strrchr(input_path, '.');
    if (ext == NULL || strcmp(ext, ".odt") != 0) {
        fprintf(stderr, "Error: Input file %s is not .odt\n", input_path);
        return;
    }
    if (access(input_path, F_OK) != 0) {
        fprintf(stderr, "Error: Input file %s doesn't exist\n", input_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/libreoffice", "libreoffice", "--headless", "--convert-to", "txt", input_path, "--outdir", output_path, NULL);
        fprintf(stderr, "Error: execl failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully converted ODT to TXT.\n");
        } else {
            fprintf(stderr, "Error: Conversion failed.\n");
        }
    }
}

void convert_txt_to_odt(const char *input_path, const char *output_path) {
    printf("Converting TXT to ODT: %s to %s\n", input_path, output_path);

    char *ext = strrchr(input_path, '.');
    if (ext == NULL || strcmp(ext, ".txt") != 0) {
        fprintf(stderr, "Error: Input file %s is not .txt\n", input_path);
        return;
    }
    if (access(input_path, F_OK) != 0) {
        fprintf(stderr, "Error: Input file %s doesn't exist\n", input_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/libreoffice", "libreoffice", "--headless", "--convert-to", "odt", input_path, "--outdir", output_path, NULL);
        fprintf(stderr, "Error: execl failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully converted TXT to ODT.\n");
        } else {
            fprintf(stderr, "Error: Conversion failed.\n");
        }
    }
}

void convert_txt_to_pdf(const char *input_path, const char *output_path) {
    printf("Converting TXT to PDF: %s to %s\n", input_path, output_path);

    char *ext = strrchr(input_path, '.');
    if (ext == NULL || strcmp(ext, ".txt") != 0) {
        fprintf(stderr, "Error: Input file %s is not .txt\n", input_path);
        return;
    }
    if (access(input_path, F_OK) != 0) {
        fprintf(stderr, "Error: Input file %s doesn't exist\n", input_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/libreoffice", "libreoffice", "--headless", "--convert-to", "pdf:writer_pdf_Export", input_path, "--outdir", output_path, NULL);
        fprintf(stderr, "Error: execl failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        fprintf(stderr, "Error: fork failed: %s\n", strerror(errno));
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Successfully converted TXT to PDF.\n");
        } else {
            fprintf(stderr, "Error: Conversion failed.\n");
        }
    }
}