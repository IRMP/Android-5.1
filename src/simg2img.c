#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SPARSE_HEADER_MAGIC 0xed26ff3a
#define CHUNK_TYPE_RAW      0xCAC1
#define CHUNK_TYPE_FILL     0xCAC2
#define CHUNK_TYPE_DONT_CARE 0xCAC3
#define CHUNK_TYPE_CRC32    0xCAC4

typedef struct {
    uint32_t magic;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t file_hdr_sz;
    uint16_t chunk_hdr_sz;
    uint32_t blk_sz;
    uint32_t total_blks;
    uint32_t total_chunks;
    uint32_t image_checksum;
} sparse_header_t;

typedef struct {
    uint16_t chunk_type;
    uint16_t reserved1;
    uint32_t chunk_sz;
    uint32_t total_sz;
} chunk_header_t;

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <sparse_img> <raw_img>\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror("fopen in"); return 1; }

    sparse_header_t sh;
    if (fread(&sh, sizeof(sh), 1, in) != 1) {
        fprintf(stderr, "Failed to read sparse header\n");
        return 1;
    }
    if (sh.magic != SPARSE_HEADER_MAGIC) {
        fprintf(stderr, "Not a sparse image (magic=0x%08x)\n", sh.magic);
        return 2;
    }
    if (sh.file_hdr_sz > sizeof(sh)) {
        fseek(in, sh.file_hdr_sz - sizeof(sh), SEEK_CUR);
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("fopen out"); return 1; }

    uint8_t *buf = malloc(1024 * 1024);
    uint8_t *zero_blk = calloc(1, sh.blk_sz);
    uint64_t out_bytes = 0;

    for (uint32_t i = 0; i < sh.total_chunks; i++) {
        chunk_header_t ch;
        if (fread(&ch, sizeof(ch), 1, in) != 1) {
            fprintf(stderr, "Failed to read chunk %u\n", i);
            return 1;
        }
        if (sh.chunk_hdr_sz > sizeof(ch)) {
            fseek(in, sh.chunk_hdr_sz - sizeof(ch), SEEK_CUR);
        }
        uint64_t data_bytes = (uint64_t)ch.chunk_sz * sh.blk_sz;
        if (ch.chunk_type == CHUNK_TYPE_RAW) {
            uint64_t rem = data_bytes;
            while (rem > 0) {
                size_t n = rem > 1024 * 1024 ? 1024 * 1024 : (size_t)rem;
                if (fread(buf, 1, n, in) != n) { fprintf(stderr, "EOF raw\n"); return 1; }
                fwrite(buf, 1, n, out);
                rem -= n;
            }
            out_bytes += data_bytes;
        } else if (ch.chunk_type == CHUNK_TYPE_FILL) {
            uint32_t fill_val;
            if (fread(&fill_val, 4, 1, in) != 1) return 1;
            uint32_t *fblk = malloc(sh.blk_sz);
            for (uint32_t j = 0; j < sh.blk_sz / 4; j++) fblk[j] = fill_val;
            for (uint32_t b = 0; b < ch.chunk_sz; b++) fwrite(fblk, 1, sh.blk_sz, out);
            free(fblk);
            out_bytes += data_bytes;
        } else if (ch.chunk_type == CHUNK_TYPE_DONT_CARE) {
            if (fseeko(out, data_bytes, SEEK_CUR) != 0) {
                for (uint32_t b = 0; b < ch.chunk_sz; b++) fwrite(zero_blk, 1, sh.blk_sz, out);
            }
            out_bytes += data_bytes;
        } else if (ch.chunk_type == CHUNK_TYPE_CRC32) {
            uint32_t crc;
            if (fread(&crc, 4, 1, in) != 1) return 1;
        } else {
            fprintf(stderr, "Unknown chunk type 0x%x\n", ch.chunk_type);
            return 1;
        }
    }
    if (ftruncate(fileno(out), (off_t)sh.total_blks * sh.blk_sz) != 0) {
        /* ignore if not regular */
    }
    fclose(out);
    fclose(in);
    free(buf);
    free(zero_blk);
    printf("Converted %s -> %s (%llu bytes, %u blocks of %u)\n",
           argv[1], argv[2], (unsigned long long)((uint64_t)sh.total_blks * sh.blk_sz),
           sh.total_blks, sh.blk_sz);
    return 0;
}
