/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MP3 (MPEG-1/2/2.5 Layer I/II/III) decoder over vendored libmad.
 *
 * The codec works over the whole-file buffer passed at open time; libmad
 * never writes to the input buffer, so the const buffer is handed to it
 * through mad_stream. Output is interleaved stereo 16-bit PCM converted
 * from libmad's fixed-point samples with clipping and rounding.
 * No malloc anywhere: the caller provides the workspace that holds all
 * decoder state. Seek translates a frame index to a byte offset using the
 * Xing/Info TOC for VBR files and frame math for CBR files.
 */
#ifndef HELIKON_CODEC_MP3_H
#define HELIKON_CODEC_MP3_H

extern const struct helikon_codec_ops helikon_mp3_codec_ops;

#endif /* HELIKON_CODEC_MP3_H */
