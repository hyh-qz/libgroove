/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libgroove, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

/* Read or update metadata in a media file.
 * This program scans the audio of the file before the metadata change, changes
 * the metadata in a temporary file, scans the audio of the temporary file to
 * make sure it matches the original, and then atomically renames the temporary
 * file over the original file.
 */

#include <groove/groove.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

static int usage(char *exe) {
    fprintf(stderr, "Usage: %s file [--update key value] [--delete key]\n"
            "Repeat --update and --delete as many times as you need to.\n", exe);
    return 1;
}

__attribute__ ((cold))
__attribute__ ((noreturn))
__attribute__ ((format (printf, 1, 2)))
static void panic(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

static const uint32_t crc_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
    0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
    0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
    0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
    0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
    0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
    0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
    0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
    0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
    0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
    0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
    0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
    0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
    0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
    0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
    0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
    0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
    0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
    0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
    0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
    0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
    0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
    0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
    0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
    0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
    0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
    0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
    0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
    0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
    0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
    0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
    0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
    0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
    0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
    0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
    0x2d02ef8d
};

static uint32_t crc32(uint32_t crc, const unsigned char *buf, int len) {
    if (!buf)
        return 0;

    crc = crc ^ 0xffffffff;

    while (len--)
        crc = crc_table[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8);

    return crc ^ 0xffffffff;
}

static int tempfileify(char * str, size_t max_len) {
    size_t len = strlen(str);
    if (len + 10 > max_len)
        return -1;
    char prepend[11];
    int n = rand() % 99999;
    snprintf(prepend, 11, ".tmp%05d-", n);
    // find the last slash and insert after it
    // if no slash, insert at beginning
    char * slash = strrchr(str, '/');
    char * pos = slash ? slash + 1 : str;
    size_t orig_len = len - (pos - str);
    memmove(pos + 10, pos, orig_len);
    strncpy(pos, prepend, 10);
    return 0;
}

int main(int argc, char * argv[]) {
    char *exe = argv[0];
    if (argc < 2)
        return usage(exe);

    fprintf(stderr, "Using libgroove v%s\n", groove_version());

    groove_init();
    groove_set_logging(GROOVE_LOG_INFO);
    struct GroovePlaylist *playlist = groove_playlist_create();

    const char *filename = argv[1];
    int temp_filename_size = strlen(filename) + 32;
    char *temp_filename = malloc(temp_filename_size);
    if (!temp_filename)
        panic("out of memory");
    strcpy(temp_filename, filename);
    if (tempfileify(temp_filename, temp_filename_size))
        panic("temp filename too long");

    fprintf(stderr, "Scanning file...\n");
    struct GrooveFile *file = groove_file_open(filename);
    if (!file)
        panic("error opening %s", filename);

    groove_playlist_set_fill_mode(playlist, GROOVE_ANY_SINK_FULL);

    struct GrooveSink *sink = groove_sink_create();
    sink->audio_format.sample_rate = 44100;
    sink->audio_format.channel_layout = GROOVE_CH_LAYOUT_MONO;
    sink->audio_format.sample_fmt = GROOVE_SAMPLE_FMT_S16;

    if (groove_sink_attach(sink, playlist) < 0)
        panic("error attaching sink");

    uint32_t crc_begin = 0;
    int byte_count_begin = 0;
    struct GroovePlaylistItem *item = groove_playlist_insert(playlist, file, 1.0, 1.0, NULL);
    struct GrooveBuffer *buffer;
    while (groove_sink_buffer_get(sink, &buffer, 1) == GROOVE_BUFFER_YES) {
        byte_count_begin += buffer->size; 
        crc_begin = crc32(crc_begin, buffer->data[0], buffer->size);
        groove_buffer_unref(buffer);
    }
    groove_playlist_remove(playlist, item);
    groove_playlist_destroy(playlist);
    playlist = NULL;
    groove_sink_destroy(sink);
    sink = NULL;
    groove_file_close(file);
    file = groove_file_open(filename);
    if (!file)
        panic("error opening %s", filename);
    fprintf(stderr, "before checksum: %x\n", crc_begin);
    fprintf(stderr, "before byte count: %d\n", byte_count_begin);

    if (byte_count_begin < 1024)
        panic("too small, something is probably wrong.");

    for (int i = 2; i < argc; i += 1) {
        char *arg = argv[i];
        if (strcmp("--update", arg) == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "--update requires 2 arguments");
                return usage(exe);
            }
            char *key = argv[++i];
            char *value = argv[++i];
            groove_file_metadata_set(file, key, value, 0);
        } else if (strcmp("--delete", arg) == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--delete requires 1 argument");
                return usage(exe);
            }
            char *key = argv[++i];
            groove_file_metadata_set(file, key, NULL, 0);
        } else if (!filename) {
            filename = arg;
        } else {
            return usage(exe);
        }
    }
    struct GrooveTag *tag = NULL;
    while ((tag = groove_file_metadata_get(file, "", tag, 0)))
        printf("%s=%s\n", groove_tag_key(tag), groove_tag_value(tag));

    fprintf(stderr, "Saving as %s\n", temp_filename);
    if (groove_file_save_as(file, temp_filename) < 0)
        panic("error saving file");

    fprintf(stderr, "Scanning newly generated file...\n");
    groove_file_close(file);
    file = groove_file_open(temp_filename);
    if (!file)
        panic("error opening %s", temp_filename);

    playlist = groove_playlist_create();

    groove_playlist_set_fill_mode(playlist, GROOVE_ANY_SINK_FULL);

    sink = groove_sink_create();
    sink->audio_format.sample_rate = 44100;
    sink->audio_format.channel_layout = GROOVE_CH_LAYOUT_MONO;
    sink->audio_format.sample_fmt = GROOVE_SAMPLE_FMT_S16;

    if (groove_sink_attach(sink, playlist) < 0)
        panic("error attaching sink");

    uint32_t crc_end = 0;
    int byte_count_end = 0;
    item = groove_playlist_insert(playlist, file, 1.0, 1.0, NULL);
    while (groove_sink_buffer_get(sink, &buffer, 1) == GROOVE_BUFFER_YES) {
        byte_count_end += buffer->size; 
        crc_end = crc32(crc_end, buffer->data[0], buffer->size);
        groove_buffer_unref(buffer);
    }
    groove_playlist_remove(playlist, item);
    fprintf(stderr, "after checksum: %x\n", crc_end);
    fprintf(stderr, "after byte count: %d\n", byte_count_end);

    groove_finish();

    if (crc_begin != crc_end || byte_count_begin != byte_count_end)
        panic("checksum failed");

    if (rename(temp_filename, filename) != 0)
        panic("rename failed");

    fprintf(stderr, "OK\n");
    return 0;
}