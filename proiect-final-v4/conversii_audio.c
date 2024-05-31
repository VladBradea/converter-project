#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdint.h>
#include "conversii_audio.h"

/* Function to convert from AAC format to MP3 format */
void convert_aac_to_mp3(const char *input_path, const char *output_path) {
    /* Pointers declaration for format and codec contexts, streams, packets */
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL;
    AVCodecContext *output_codec_context = NULL;
    AVStream *input_stream = NULL;
    AVStream *output_stream = NULL;
    AVPacket *packet = NULL;
    int ret;

    /* It opens the input file and will exit if it's a problem in opening the file */
    if ((ret = avformat_open_input(&input_format_context, input_path, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", input_path);
        exit(1);
    }

    /* It will retrieve stream info from the input file */
    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        exit(1);
    }

    /* It will allocate the output format context */
    avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_path);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Find the best stream in the input file and will display the error message if it's an error thrown */
    int stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_index < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), input_path);
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* Get the input stream and find the decoder for the stream */
    input_stream = input_format_context->streams[stream_index];
    AVCodec *input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* It will allocate the codec context for the decoder */
    input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It makes a copy for codec parameters from input stream to codec context */
    if ((ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    /* Opens the codec */
    if ((ret = avcodec_open2(input_codec_context, input_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    /* Finds the encoder for the output stream for the MP3 required format */
    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!output_codec) {
        fprintf(stderr, "Necessary encoder not found\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    /* Creates a new stream for the output file */
    output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* It will allocate the codec context for the encoder */
    output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        fprintf(stderr, "Failed to allocate the encoder context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It set the codec parameters for the output stream */
    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->sample_fmt = output_codec->sample_fmts[0];
    output_codec_context->bit_rate = 192000;

    /* Open the output codec */
    if ((ret = avcodec_open2(output_codec_context, output_codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open output codec\n");
        goto end;
    }

    /* It copies stream parameters from codec context to output stream */
    if ((ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context)) < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        goto end;
    }

    /* Set the time base for the output stream */
    output_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

    /* Open the output file */
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", output_path);
            goto end;
        }
    }

    /* Write the header for the output file */
    if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    /* It allocates the AVPacket structure */
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It read frames from the input file */
    while (av_read_frame(input_format_context, packet) >= 0) {
        /* Checks if the packet belongs to the audio stream */
        if (packet->stream_index == stream_index) {
            /* It sends the packet to the decoder */
            ret = avcodec_send_packet(input_codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder\n");
                break;
            }

            /* It will allocate a new frame for the decoded data */
            AVFrame *frame = av_frame_alloc();
            if (!frame) {
                fprintf(stderr, "Could not allocate AVFrame\n");
                ret = AVERROR(ENOMEM);
                break;
            }

            /* Receives decoded frames */
            ret = avcodec_receive_frame(input_codec_context, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                continue;
            } else if (ret < 0) {
                fprintf(stderr, "Error while receiving a frame from the decoder\n");
                av_frame_free(&frame);
                break;
            }

            /* Set the presentation timestamp for the frame */
            frame->pts = frame->best_effort_timestamp;

            /* Send the frame to the encoder */
            ret = avcodec_send_frame(output_codec_context, frame);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a frame to the encoder\n");
                av_frame_free(&frame);
                break;
            }

            /* It will allocate a new packet for the encoded data */
            AVPacket *output_packet = av_packet_alloc();
            if (!output_packet) {
                fprintf(stderr, "Could not allocate AVPacket\n");
                av_frame_free(&frame);
                ret = AVERROR(ENOMEM);
                break;
            }

            /* It receives encoded packets */
            ret = avcodec_receive_packet(output_codec_context, output_packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_packet_free(&output_packet);
                av_frame_free(&frame);
                continue;
            } else if (ret < 0) {
                fprintf(stderr, "Error while receiving a packet from the encoder\n");
                av_packet_free(&output_packet);
                av_frame_free(&frame);
                break;
            }

            /* It will rescale the packet timestamp */
            av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
            output_packet->stream_index = output_stream->index;

            /* It will write the packet to the output file */
            ret = av_interleaved_write_frame(output_format_context, output_packet);
            if (ret < 0) {
                fprintf(stderr, "Error while writing a packet to the output file\n");
                av_packet_free(&output_packet);
                av_frame_free(&frame);
                break;
            }

            av_packet_free(&output_packet);
            av_frame_free(&frame);
        }
        av_packet_unref(packet);
    }

    /* It flushes the encoder */
    ret = avcodec_send_frame(output_codec_context, NULL);
    while (ret >= 0) {
        AVPacket *output_packet = av_packet_alloc();
        if (!output_packet) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        ret = avcodec_receive_packet(output_codec_context, output_packet);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&output_packet);
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a packet from the encoder\n");
            av_packet_free(&output_packet);
            break;
        }

        /* It rescales the packet timestamp */
        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
        output_packet->stream_index = output_stream->index;

        /* It will write the packet to the output file */
        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing a packet to the output file\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_free(&output_packet);
    }

    /* It will write the trailer for the output file */
    av_write_trailer(output_format_context);

    end:
    /* Free the memory */
    av_packet_free(&packet);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    /* Close the input and output file */
    avformat_close_input(&input_format_context);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);

    /* It will print the error message if an error exists */
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
}


/* Function to convert from AAC format to WAV format */
void convert_aac_to_wav(const char *input_path, const char *output_path) {
    /* Pointers declaration for format and codec contexts, streams, packets, frames, and the resampler context */
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL;
    AVCodecContext *output_codec_context = NULL;
    AVStream *input_stream = NULL;
    AVStream *output_stream = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr_ctx = NULL;
    int ret;

    /* It will try to open the source (input) file and
     * in case when it's an error in opening the file it will display this message,
     * then it will exit with an error status */
    if ((ret = avformat_open_input(&input_format_context, input_path, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", input_path);
        exit(1);
    }

    /* This retrieve stream information from the input file and will treat the error case */
    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        goto end;
    }

    /* It will allocate the output format context and will treat the error case */
    avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_path);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Find the best stream in the source (input) file and will treat the error case */
    int stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_index < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), input_path);
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* Get the input stream and find the decoder for the stream */
    input_stream = input_format_context->streams[stream_index];
    AVCodec *input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(EINVAL);
        goto end;
    }

    /* It will allocate the codec context for the decoder */
    input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It will copy codec parameters from input stream to codec context */
    if ((ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    /* Opens the codec */
    if ((ret = avcodec_open2(input_codec_context, input_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    /* It will find the encoder for the output stream  for the required WAV format */
    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!output_codec) {
        fprintf(stderr, "Necessary encoder not found\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    /* It creates a new stream for the output file */
    output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* It will allocate the codec context for the encoder */
    output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        fprintf(stderr, "Failed to allocate the encoder context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Set the codec parameters for the output stream */
    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->sample_fmt = output_codec->sample_fmts[0];
    output_codec_context->bit_rate = 16 * output_codec_context->sample_rate * output_codec_context->channels;

    /* Opens the output codec */
    if ((ret = avcodec_open2(output_codec_context, output_codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open output codec\n");
        goto end;
    }

    /* It copies the stream parameters from codec context to the output stream */
    if ((ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context)) < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        goto end;
    }

    /* It will set the time base for the output stream */
    output_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

    /* Opens the output file */
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", output_path);
            goto end;
        }
    }

    /* Write the header for the output file */
    if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    /* It allocates the AVPacket and AVFrame structures */
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate AVFrame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It will initialize the context for the resampler */
    swr_ctx = swr_alloc_set_opts(NULL,
                                 av_get_default_channel_layout(output_codec_context->channels), output_codec_context->sample_fmt, output_codec_context->sample_rate,
                                 av_get_default_channel_layout(input_codec_context->channels), input_codec_context->sample_fmt, input_codec_context->sample_rate,
                                 0, NULL);
    if (!swr_ctx || swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not allocate or initialize the resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* It will ead frames from the source (input) file */
    while (av_read_frame(input_format_context, packet) >= 0) {
        /* It will check if the packet belongs to the audio stream */
        if (packet->stream_index == stream_index) {
            /* Send the packet to the decoder */
            ret = avcodec_send_packet(input_codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder\n");
                break;
            }

            /* Receive decoded frames */
            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                /* Allocate a new frame for the resampled data */
                AVFrame *resampled_frame = av_frame_alloc();
                if (!resampled_frame) {
                    fprintf(stderr, "Could not allocate resampled AVFrame\n");
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                /* It Sets the parameters for the resampled frame */
                resampled_frame->channel_layout = output_codec_context->channel_layout;
                resampled_frame->format = output_codec_context->sample_fmt;
                resampled_frame->sample_rate = output_codec_context->sample_rate;
                resampled_frame->nb_samples = frame->nb_samples;

                /* Allocate buffer for the resampled frame */
                if ((ret = av_frame_get_buffer(resampled_frame, 0)) < 0) {
                    fprintf(stderr, "Could not allocate buffer for resampled frame\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                /* Resample the audio data */
                ret = swr_convert(swr_ctx,
                                  resampled_frame->data, resampled_frame->nb_samples,
                                  (const uint8_t **)frame->data, frame->nb_samples);
                if (ret < 0) {
                    fprintf(stderr, "Error while resampling\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                /* It sets the presentation timestamp for the resampled frame */
                resampled_frame->pts = av_rescale_q(frame->pts, input_codec_context->time_base, output_codec_context->time_base);

                /* It will send the resampled frame to the encoder */
                ret = avcodec_send_frame(output_codec_context, resampled_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error while sending a frame to the encoder\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                /* Receive encoded packets */
                while (ret >= 0) {
                    AVPacket *output_packet = av_packet_alloc();
                    if (!output_packet) {
                        fprintf(stderr, "Could not allocate AVPacket\n");
                        ret = AVERROR(ENOMEM);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    ret = avcodec_receive_packet(output_codec_context, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_packet_free(&output_packet);
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error while receiving a packet from the encoder\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    /* Rescale the packet timestamp */
                    av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
                    output_packet->stream_index = output_stream->index;

                    /* Writes the packet to the output file */
                    ret = av_interleaved_write_frame(output_format_context, output_packet);
                    if (ret < 0) {
                        fprintf(stderr, "Error while writing a packet to the output file\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_free(&output_packet);
                }

                av_frame_free(&resampled_frame);
            }
        }
        av_packet_unref(packet);
    }

    /* It will flush the encoder */
    ret = avcodec_send_frame(output_codec_context, NULL);
    while (ret >= 0) {
        AVPacket *output_packet = av_packet_alloc();
        if (!output_packet) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        ret = avcodec_receive_packet(output_codec_context, output_packet);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&output_packet);
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a packet from the encoder\n");
            av_packet_free(&output_packet);
            break;
        }

        /* It will rescale the packet timestamp */
        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
        output_packet->stream_index = output_stream->index;

        /* It writes the packet to the output file */
        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing a packet to the output file\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_free(&output_packet);
    }

    /* It writes the trailer for the output file */
    av_write_trailer(output_format_context);

    end:
    /* It will free the memory then it will close the input file */
    swr_free(&swr_ctx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    avformat_close_input(&input_format_context);
    /* Close the output file if it's necessary */
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);

    /* It will print the error message if an error exists */
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
}


void convert_mp3_to_wav(const char *input_path, const char *output_path) {
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL;
    AVCodecContext *output_codec_context = NULL;
    AVStream *input_stream = NULL;
    AVStream *output_stream = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr_ctx = NULL;
    int ret;

    if ((ret = avformat_open_input(&input_format_context, input_path, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", input_path);
        exit(1);
    }

    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        goto end;
    }

    avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_path);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    int stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_index < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), input_path);
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_stream = input_format_context->streams[stream_index];
    AVCodec *input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    if ((ret = avcodec_open2(input_codec_context, input_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!output_codec) {
        fprintf(stderr, "Necessary encoder not found\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        fprintf(stderr, "Failed to allocate the encoder context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->sample_fmt = output_codec->sample_fmts[0];
    output_codec_context->bit_rate = 16 * output_codec_context->sample_rate * output_codec_context->channels;

    if ((ret = avcodec_open2(output_codec_context, output_codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open output codec\n");
        goto end;
    }

    if ((ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context)) < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        goto end;
    }

    output_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", output_path);
            goto end;
        }
    }

    if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate AVFrame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    swr_ctx = swr_alloc_set_opts(NULL,
                                 output_codec_context->channel_layout, output_codec_context->sample_fmt, output_codec_context->sample_rate,
                                 input_codec_context->channel_layout, input_codec_context->sample_fmt, input_codec_context->sample_rate,
                                 0, NULL);
    if (!swr_ctx || swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not allocate or initialize the resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while (av_read_frame(input_format_context, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            ret = avcodec_send_packet(input_codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                AVFrame *resampled_frame = av_frame_alloc();
                if (!resampled_frame) {
                    fprintf(stderr, "Could not allocate resampled AVFrame\n");
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                resampled_frame->channel_layout = output_codec_context->channel_layout;
                resampled_frame->format = output_codec_context->sample_fmt;
                resampled_frame->sample_rate = output_codec_context->sample_rate;
                resampled_frame->nb_samples = frame->nb_samples;

                if ((ret = av_frame_get_buffer(resampled_frame, 0)) < 0) {
                    fprintf(stderr, "Could not allocate buffer for resampled frame\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                if ((ret = swr_convert_frame(swr_ctx, resampled_frame, frame)) < 0) {
                    fprintf(stderr, "Error while resampling\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                resampled_frame->pts = frame->pts;

                ret = avcodec_send_frame(output_codec_context, resampled_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error while sending a frame to the encoder\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                while (ret >= 0) {
                    AVPacket *output_packet = av_packet_alloc();
                    if (!output_packet) {
                        fprintf(stderr, "Could not allocate AVPacket\n");
                        ret = AVERROR(ENOMEM);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    ret = avcodec_receive_packet(output_codec_context, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_packet_free(&output_packet);
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error while receiving a packet from the encoder\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
                    output_packet->stream_index = output_stream->index;

                    ret = av_interleaved_write_frame(output_format_context, output_packet);
                    if (ret < 0) {
                        fprintf(stderr, "Error while writing a packet to the output file\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_free(&output_packet);
                }

                av_frame_free(&resampled_frame);
            }
        }
        av_packet_unref(packet);
    }

    // Golim coada encoderului
    ret = avcodec_send_frame(output_codec_context, NULL);
    while (ret >= 0) {
        AVPacket *output_packet = av_packet_alloc();
        if (!output_packet) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        ret = avcodec_receive_packet(output_codec_context, output_packet);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&output_packet);
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a packet from the encoder\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
        output_packet->stream_index = output_stream->index;

        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing a packet to the output file\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_free(&output_packet);
    }

    av_write_trailer(output_format_context);

    end:
    swr_free(&swr_ctx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    avformat_close_input(&input_format_context);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
}


void convert_wav_to_aac(const char *input_path, const char *output_path) {
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL;
    AVCodecContext *output_codec_context = NULL;
    AVStream *input_stream = NULL;
    AVStream *output_stream = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr_ctx = NULL;
    int ret;

    if ((ret = avformat_open_input(&input_format_context, input_path, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", input_path);
        exit(1);
    }

    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        goto end;
    }

    avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_path);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    int stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_index < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), input_path);
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_stream = input_format_context->streams[stream_index];
    AVCodec *input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    if ((ret = avcodec_open2(input_codec_context, input_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!output_codec) {
        fprintf(stderr, "Necessary encoder not found\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        fprintf(stderr, "Failed to allocate the encoder context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->sample_fmt = output_codec->sample_fmts[0];
    output_codec_context->bit_rate = 192000;

    if ((ret = avcodec_open2(output_codec_context, output_codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open output codec\n");
        goto end;
    }

    if ((ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context)) < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        goto end;
    }

    output_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", output_path);
            goto end;
        }
    }

    if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate AVFrame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    // Set explicit input and output channel layouts for the resampler context
    swr_ctx = swr_alloc_set_opts(NULL,
                                 av_get_default_channel_layout(output_codec_context->channels), output_codec_context->sample_fmt, output_codec_context->sample_rate,
                                 av_get_default_channel_layout(input_codec_context->channels), input_codec_context->sample_fmt, input_codec_context->sample_rate,
                                 0, NULL);
    if (!swr_ctx || swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not allocate or initialize the resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while (av_read_frame(input_format_context, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            ret = avcodec_send_packet(input_codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                AVFrame *resampled_frame = av_frame_alloc();
                if (!resampled_frame) {
                    fprintf(stderr, "Could not allocate resampled AVFrame\n");
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                resampled_frame->channel_layout = output_codec_context->channel_layout;
                resampled_frame->format = output_codec_context->sample_fmt;
                resampled_frame->sample_rate = output_codec_context->sample_rate;
                resampled_frame->nb_samples = frame->nb_samples;

                if ((ret = av_frame_get_buffer(resampled_frame, 0)) < 0) {
                    fprintf(stderr, "Could not allocate buffer for resampled frame\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                // Resample the audio data
                ret = swr_convert(swr_ctx,
                                  resampled_frame->data, resampled_frame->nb_samples,
                                  (const uint8_t **)frame->data, frame->nb_samples);
                if (ret < 0) {
                    fprintf(stderr, "Error while resampling\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                resampled_frame->pts = av_rescale_q(frame->pts, input_codec_context->time_base, output_codec_context->time_base);

                ret = avcodec_send_frame(output_codec_context, resampled_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error while sending a frame to the encoder\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                while (ret >= 0) {
                    AVPacket *output_packet = av_packet_alloc();
                    if (!output_packet) {
                        fprintf(stderr, "Could not allocate AVPacket\n");
                        ret = AVERROR(ENOMEM);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    ret = avcodec_receive_packet(output_codec_context, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_packet_free(&output_packet);
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error while receiving a packet from the encoder\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
                    output_packet->stream_index = output_stream->index;

                    ret = av_interleaved_write_frame(output_format_context, output_packet);
                    if (ret < 0) {
                        fprintf(stderr, "Error while writing a packet to the output file\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_free(&output_packet);
                }

                av_frame_free(&resampled_frame);
            }
        }
        av_packet_unref(packet);
    }

    // Golim coada encoderului
    ret = avcodec_send_frame(output_codec_context, NULL);
    while (ret >= 0) {
        AVPacket *output_packet = av_packet_alloc();
        if (!output_packet) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        ret = avcodec_receive_packet(output_codec_context, output_packet);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&output_packet);
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a packet from the encoder\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
        output_packet->stream_index = output_stream->index;

        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing a packet to the output file\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_free(&output_packet);
    }

    av_write_trailer(output_format_context);

    end:
    swr_free(&swr_ctx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    avformat_close_input(&input_format_context);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
}

void convert_wav_to_mp3(const char *input_path, const char *output_path) {
    AVFormatContext *input_format_context = NULL;
    AVFormatContext *output_format_context = NULL;
    AVCodecContext *input_codec_context = NULL;
    AVCodecContext *output_codec_context = NULL;
    AVStream *input_stream = NULL;
    AVStream *output_stream = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr_ctx = NULL;
    int ret;

    if ((ret = avformat_open_input(&input_format_context, input_path, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'\n", input_path);
        exit(1);
    }

    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information\n");
        goto end;
    }

    avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_path);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    int stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_index < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO), input_path);
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_stream = input_format_context->streams[stream_index];
    AVCodec *input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(EINVAL);
        goto end;
    }

    input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    if ((ret = avcodec_open2(input_codec_context, input_codec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto end;
    }

    AVCodec *output_codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!output_codec) {
        fprintf(stderr, "Necessary encoder not found\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    output_stream = avformat_new_stream(output_format_context, NULL);
    if (!output_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        fprintf(stderr, "Failed to allocate the encoder context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->sample_fmt = output_codec->sample_fmts[0];
    output_codec_context->bit_rate = 192000;

    if ((ret = avcodec_open2(output_codec_context, output_codec, NULL)) < 0) {
        fprintf(stderr, "Cannot open output codec\n");
        goto end;
    }

    if ((ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context)) < 0) {
        fprintf(stderr, "Failed to copy encoder parameters to output stream\n");
        goto end;
    }

    output_stream->time_base = (AVRational){1, output_codec_context->sample_rate};

    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&output_format_context->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
            fprintf(stderr, "Could not open output file '%s'\n", output_path);
            goto end;
        }
    }

    if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate AVFrame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    // Set explicit input and output channel layouts for the resampler context
    swr_ctx = swr_alloc_set_opts(NULL,
                                 av_get_default_channel_layout(output_codec_context->channels), output_codec_context->sample_fmt, output_codec_context->sample_rate,
                                 av_get_default_channel_layout(input_codec_context->channels), input_codec_context->sample_fmt, input_codec_context->sample_rate,
                                 0, NULL);
    if (!swr_ctx || swr_init(swr_ctx) < 0) {
        fprintf(stderr, "Could not allocate or initialize the resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while (av_read_frame(input_format_context, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            ret = avcodec_send_packet(input_codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error while receiving a frame from the decoder\n");
                    goto end;
                }

                AVFrame *resampled_frame = av_frame_alloc();
                if (!resampled_frame) {
                    fprintf(stderr, "Could not allocate resampled AVFrame\n");
                    ret = AVERROR(ENOMEM);
                    goto end;
                }

                resampled_frame->channel_layout = output_codec_context->channel_layout;
                resampled_frame->format = output_codec_context->sample_fmt;
                resampled_frame->sample_rate = output_codec_context->sample_rate;
                resampled_frame->nb_samples = frame->nb_samples;

                if ((ret = av_frame_get_buffer(resampled_frame, 0)) < 0) {
                    fprintf(stderr, "Could not allocate buffer for resampled frame\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                // Resample the audio data
                ret = swr_convert(swr_ctx,
                                  resampled_frame->data, resampled_frame->nb_samples,
                                  (const uint8_t **)frame->data, frame->nb_samples);
                if (ret < 0) {
                    fprintf(stderr, "Error while resampling\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                resampled_frame->pts = av_rescale_q(frame->pts, input_codec_context->time_base, output_codec_context->time_base);

                ret = avcodec_send_frame(output_codec_context, resampled_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error while sending a frame to the encoder\n");
                    av_frame_free(&resampled_frame);
                    goto end;
                }

                while (ret >= 0) {
                    AVPacket *output_packet = av_packet_alloc();
                    if (!output_packet) {
                        fprintf(stderr, "Could not allocate AVPacket\n");
                        ret = AVERROR(ENOMEM);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    ret = avcodec_receive_packet(output_codec_context, output_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_packet_free(&output_packet);
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error while receiving a packet from the encoder\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
                    output_packet->stream_index = output_stream->index;

                    ret = av_interleaved_write_frame(output_format_context, output_packet);
                    if (ret < 0) {
                        fprintf(stderr, "Error while writing a packet to the output file\n");
                        av_packet_free(&output_packet);
                        av_frame_free(&resampled_frame);
                        goto end;
                    }

                    av_packet_free(&output_packet);
                }

                av_frame_free(&resampled_frame);
            }
        }
        av_packet_unref(packet);
    }

    // Golim coada encoderului
    ret = avcodec_send_frame(output_codec_context, NULL);
    while (ret >= 0) {
        AVPacket *output_packet = av_packet_alloc();
        if (!output_packet) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            ret = AVERROR(ENOMEM);
            break;
        }

        ret = avcodec_receive_packet(output_codec_context, output_packet);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            av_packet_free(&output_packet);
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while receiving a packet from the encoder\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);
        output_packet->stream_index = output_stream->index;

        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
            fprintf(stderr, "Error while writing a packet to the output file\n");
            av_packet_free(&output_packet);
            break;
        }

        av_packet_free(&output_packet);
    }

    av_write_trailer(output_format_context);

    end:
    swr_free(&swr_ctx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    avformat_close_input(&input_format_context);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
}
