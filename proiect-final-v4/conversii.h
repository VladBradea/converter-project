#ifndef CONVERSII_H
#define CONVERSII_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t b;
    uint8_t g;
    uint8_t r;
} RGB;

typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} BMPFileHeader1;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    int32_t xPelsPerMeter;
    int32_t yPelsPerMeter;
    uint32_t clrUsed;
    uint32_t clrImportant;
} BMPInfoHeader1;
#pragma pack(pop)

// Function prototypes
int read_BMP_file(const char *filename, unsigned char **data, int *width, int *height);
void write_JPEG_file(const char *filename, unsigned char *img_data, int width, int height, int quality);
int write_PNG_file(const char *filename, unsigned char *image, int width, int height);
int read_JPEG_file(const char *filename, unsigned char **image_buffer, int *width, int *height);
int read_PNG_file(const char *filename, unsigned char **image, int *width, int *height);
void write_BMP_file(const char *filename, unsigned char *image_buffer, int width, int height);

void convert_bmp_to_jpeg(const char *input_file, const char *output_file);
void convert_bmp_to_png(const char *input_file, const char *output_file);
void convert_jpeg_to_bmp(const char *input_file, const char *output_file);
void convert_jpeg_to_png(const char *input_file, const char *output_file);
void convert_png_to_bmp(const char *input_file, const char *output_file);
void convert_png_to_jpeg(const char *input_file, const char *output_file);

void convert_pdf_to_odt(const char *input_path, const char *output_path);
void convert_odt_to_pdf(const char *input_path, const char *output_path);
void convert_odt_to_txt(const char *input_path, const char *output_path);
void convert_txt_to_odt(const char *input_path, const char *output_path);
void convert_txt_to_pdf(const char *input_path, const char *output_path);

#endif // CONVERSII_H
