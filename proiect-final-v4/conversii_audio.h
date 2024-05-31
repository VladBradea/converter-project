#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdint.h>

#ifndef PROIECT_FINAL_CONVERSII_AUDIO_H
#define PROIECT_FINAL_CONVERSII_AUDIO_H

// AUDIO
void convert_aac_to_mp3(const char *input_path, const char *output_path);
void convert_aac_to_wav(const char *input_path, const char *output_path);
void convert_mp3_to_wav(const char *input_path, const char *output_path);
void convert_wav_to_aac(const char *input_path, const char *output_path);
void convert_wav_to_mp3(const char *input_path, const char *output_path);

#endif //PROIECT_FINAL_CONVERSII_AUDIO_H
