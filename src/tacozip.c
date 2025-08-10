/*
 * tacozip.c — High-performance ZIP64 (CIP64) writer with a fixed 64-byte "TACO Ghost" LFH at byte 0.
 *
 * Version:    0.1.0
 * Author:     Cesar Aybar (csaybar)
 * Inspired by: Strongly inspired by libzip's implementation details (https://libzip.org), but 
 *      reduced to the essentials for the specific use case: CIP64 + STORE + ghost LFH.
 *
 * Overview:
 *  - Always uses ZIP64 structures (ver_needed = 45) regardless of file sizes.
 *  - STORE-only (no compression) for maximum throughput and simplicity.
 *  - Writes a special, fixed-size 64-byte "TACO Ghost" Local File Header (LFH) at offset 0.
 *    This entry is not listed in the Central Directory and contains two uint64 fields
 *    (metadata_offset, metadata_length) for application-level metadata retrieval.
 *  - Optimized for sequential appending and high-bandwidth writes:
 *      • Large dedicated output buffer (default 4 MiB) bound to FILE* via setvbuf().
 *      • Reusable scratch buffer for file copies (default 1 MiB).
 *      • Optional posix_fallocate() preallocation to reduce fragmentation.
 *      • Unlocked stdio I/O (glibc fread_unlocked/fwrite_unlocked) when available.
 *      • Minimal heap churn — name strings duplicated once, buffers reused.
 *  - Correct streaming CRC32 computation in a single pass, using the canonical IEEE
 *    polynomial (0xEDB88320) with precomputed lookup table.
 *  - Fully contiguous Central Directory write followed by ZIP64 EOCD and locator.
 *
 * Design Choices:
 *  - All sizes/offsets written as ZIP64 fields — even small files get 0xFFFFFFFF in
 *    32-bit size fields with the actual values in the extra block.
 *  - Data Descriptors (GP flag bit 3) are used for CRC/size to avoid fseek back-patching.
 *  - UTF-8 general-purpose flag (bit 11) can be enabled at compile time via TACOZ_SET_UTF8_FLAG.
 *  - Internal memory buffers are freed *only after* fclose(), because setvbuf() attaches
 *    the output buffer to the FILE* stream.
 *
 * Build:
 *     cc -O3 -DNDEBUG -std=c11 -Wall -Wextra -pedantic -c tacozip.c
 *
 * Tunables (can be overridden at compile time):
 *     TACOZ_OUT_BUFSZ   — size of FILE* output buffer (default 4 MiB)
 *     TACOZ_COPY_BUFSZ  — size of scratch buffer for fread/fwrite loop (default 1 MiB)
 *     TACOZ_SET_UTF8_FLAG — set to 1 to always mark entries as UTF-8
 * 
 */


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64  /* large-file I/O on POSIX */
#endif

#include "tacozip.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <fcntl.h>   /* posix_fallocate */
#endif

/* ------------------------------- Tunables ---------------------------------- */
/* These can be overridden at compile time (CMake passes -D… if desired). */
#ifndef TACOZ_OUT_BUFSZ
#define TACOZ_OUT_BUFSZ  (4u << 20)    /* 4 MiB output buffer for FILE* */
#endif
#ifndef TACOZ_COPY_BUFSZ
#define TACOZ_COPY_BUFSZ (1u << 20)    /* 1 MiB reusable copy buffer */
#endif
#ifndef TACOZ_SET_UTF8_FLAG
#define TACOZ_SET_UTF8_FLAG 0          /* set GP bit 11 if caller guarantees UTF-8 names */
#endif

/* Unlocked stdio cuts lock overhead on glibc while remaining drop-in elsewhere. */
#if defined(__GLIBC__)
#define TZ_HAVE_UNLOCKED_IO 1
#else
#define TZ_HAVE_UNLOCKED_IO 0
#endif
#if TZ_HAVE_UNLOCKED_IO
#define TZ_FREAD  fread_unlocked
#define TZ_FWRITE fwrite_unlocked
#else
#define TZ_FREAD  fread
#define TZ_FWRITE fwrite
#endif

/* ----------------------------- ZIP constants ------------------------------- */
#define SIG_LFH              0x04034B50u
#define SIG_CDFH             0x02014B50u
#define SIG_EOCD             0x06054B50u
#define SIG_ZIP64_EOCD       0x06064B50u
#define SIG_ZIP64_LOCATOR    0x07064B50u
#define SIG_DD               0x08074B50u

#define VER_NEEDED_ZIP64     45u       /* version needed to extract = ZIP64 required */
#define VER_MADE_BY          0x031E    /* host=3 (Unix), ver=30 — informational */

#define GPFLAG_USE_DD        0x0008    /* data descriptor follows (bit 3) */
#define GPFLAG_UTF8          0x0800    /* filenames are UTF-8 (bit 11) */
#define METHOD_STORE         0         /* no compression */

/* ---------- TACO Ghost constants ---------- */
#define EXTRA_TOTAL_LEN      20u  /* 2B id + 2B size + 16B payload (two u64) */


/* -------------------------- Little-endian writers -------------------------- */
static inline void le16(unsigned char *p, uint16_t v){
    p[0] = (unsigned char)(v     );
    p[1] = (unsigned char)(v >> 8);
}
static inline void le32(unsigned char *p, uint32_t v){
    p[0] = (unsigned char)(v      );
    p[1] = (unsigned char)(v >> 8 );
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static inline void le64(unsigned char *p, uint64_t v){
    p[0] = (unsigned char)(v      );
    p[1] = (unsigned char)(v >> 8 );
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    p[4] = (unsigned char)(v >> 32);
    p[5] = (unsigned char)(v >> 40);
    p[6] = (unsigned char)(v >> 48);
    p[7] = (unsigned char)(v >> 56);
}

/* ------------------------------- CRC32 ------------------------------------- */
/* Canonical IEEE CRC32 (0xEDB88320). We precompute once and stream update. */
static uint32_t crc32_tab[256];
static void crc32_init_table(void){
    static int inited = 0;
    if (inited) return;
    inited = 1;
    const uint32_t poly = 0xEDB88320u;
    for (uint32_t i = 0; i < 256; i++){
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1u) ? (poly ^ (c >> 1)) : (c >> 1);
        crc32_tab[i] = c;
    }
}

/* NOTE: caller is responsible for xor-in/out (init 0xFFFFFFFF, finalize ^ 0xFFFFFFFF). */
static inline uint32_t crc32_update_stream(uint32_t crc, const unsigned char * restrict buf, size_t len){
    for (size_t i = 0; i < len; i++)
        crc = crc32_tab[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}


/* ------------------------------ Ghost writer ------------------------------- */
/* Emits the fixed 64-byte “TACO_GHOST” LFH at offset 0. Not listed in the CDR. */
static int write_ghost(FILE *fp, uint64_t meta_off, uint64_t meta_len){
    unsigned char b[TACO_GHOST_SIZE] = {0};

    le32(b + 0,  SIG_LFH);
    le16(b + 4,  VER_NEEDED_ZIP64);
    le16(b + 6,  0);                 /* flags */
    le16(b + 8,  0);                 /* method (STORE) */
    le16(b + 10, 0); le16(b + 12, 0);/* DOS time/date = 0 for determinism */
    le32(b + 14, 0);                 /* crc32 */
    le32(b + 18, 0); le32(b + 22, 0);/* sizes=0 */
    le16(b + 26, TACO_GHOST_NAME_LEN);
    le16(b + 28, EXTRA_TOTAL_LEN);

    memcpy(b + 30, TACO_GHOST_NAME, TACO_GHOST_NAME_LEN);

    le16(b + 40, TACO_GHOST_EXTRA_ID);
    le16(b + 42, TACO_GHOST_EXTRA_SIZE); /* fixed 16 bytes payload */
    le64(b + 44, meta_off);
    le64(b + 52, meta_len);

    if (TZ_FWRITE(b, 1, sizeof b, fp) != sizeof b) return TACOZ_ERR_IO;
    return TACOZ_OK;
}

/* ------------------------ Central Directory bookkeeping -------------------- */
typedef struct {
    char    *name;                    /* archive name (duplicated) */
    uint32_t gp_flags;                /* GP bits used for this file */
    uint16_t method;                  /* STORE */
    uint32_t crc32;                   /* CRC of file data */
    uint64_t comp_size, uncomp_size;  /* == data bytes (STORE) */
    uint64_t lfh_offset;              /* absolute LFH offset */
} cd_ent_t;

typedef struct {
    FILE   *fp;
    cd_ent_t *ents;
    size_t  n, cap;

    /* Output stream buffer (owned; must be freed AFTER fclose) */
    unsigned char *out_buf;
    size_t  out_buf_cap;

    /* Scratch copy buffer reused across files */
    unsigned char *buf;
    size_t  buf_cap;
} zipw_t;

/* Grow the in-memory CD list; duplicate name for stability. */
static int push_entry(zipw_t *w, const cd_ent_t *src){
    if (w->n == w->cap){
        size_t nc = w->cap ? w->cap * 2 : 16;
        void *p = realloc(w->ents, nc * sizeof(cd_ent_t));
        if (!p) return TACOZ_ERR_IO;
        w->ents = (cd_ent_t*)p;
        w->cap  = nc;
    }
    /* Only advance the count after all allocations succeed.
       Otherwise a failed strdup() would leave w->n incremented and
       the caller would see a partially initialized entry. */
    cd_ent_t *e = &w->ents[w->n];
    *e = *src;
    e->name = strdup(src->name);
    if (!e->name) return TACOZ_ERR_IO;
    w->n++;
    return TACOZ_OK;
}

/* ------------------------ Preallocation (best-effort) ----------------------- */
/* Estimate final size and ask the FS to reserve space. Reduces fragmentation. */
static void try_posix_fallocate_estimate(FILE *fp,
                                         const char * const *src_files,
                                         const char * const *arc_files,
                                         size_t num_files)
{
#if defined(__linux__)
    uint64_t sum = TACO_GHOST_SIZE;
    for (size_t i = 0; i < num_files; i++){
        struct stat st;
        uint64_t fsz = 0;
        if (src_files[i] && stat(src_files[i], &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
            fsz = (uint64_t)st.st_size;
        size_t nlen = arc_files[i] ? strlen(arc_files[i]) : 0;
        if (nlen > 0xFFFFu) nlen = 0xFFFFu;   /* spec limit for name field */

        sum += (30u + (uint64_t)nlen)         /* LFH + name */
             + fsz                            /* file data */
             + (4u + 4u + 8u + 8u)           /* ZIP64 data descriptor */
             + (46u + (uint64_t)nlen + 28u); /* CDFH + name + ZIP64 extra */
    }
    sum += 56u + 20u + 22u;                   /* ZIP64 EOCD + locator + EOCD */

    int fd = fileno(fp);
    if (fd >= 0) (void)posix_fallocate(fd, 0, (off_t)sum); /* ignore errors */
#else
    (void)fp; (void)src_files; (void)arc_files; (void)num_files;
#endif
}

/* ----------------------------- Add one file -------------------------------- */
/* Emits LFH→data→ZIP64 DD, tracks metadata for the CD. */
static int add_file(zipw_t *w, const char *src_path, const char *arc_name){
    size_t name_len = strlen(arc_name);
    if (name_len > 0xFFFFu) return TACOZ_ERR_PARAM; /* ZIP spec name field is uint16 */

    FILE *in = fopen(src_path, "rb");
    if (!in) return TACOZ_ERR_IO;

    /* Ensure a single reusable copy buffer exists (avoid per-file malloc/free). */
    if (!w->buf || w->buf_cap < TACOZ_COPY_BUFSZ){
        unsigned char *nb = (unsigned char*)realloc(w->buf, TACOZ_COPY_BUFSZ);
        if (!nb) { fclose(in); return TACOZ_ERR_IO; }
        w->buf = nb; w->buf_cap = TACOZ_COPY_BUFSZ;
    }

    /* Absolute LFH offset (ZIP readers expect this to include the ghost). */
    long long off = ftello(w->fp);
    if (off < 0) { fclose(in); return TACOZ_ERR_IO; }
    uint64_t lfh_off = (uint64_t)off;

    uint16_t gpflags = GPFLAG_USE_DD;
#if TACOZ_SET_UTF8_FLAG
    gpflags |= GPFLAG_UTF8;
#endif

    /* Local File Header with unknown sizes; ZIP64 DD will carry real values. */
    unsigned char lfh[30];
    le32(lfh + 0,  SIG_LFH);
    le16(lfh + 4,  VER_NEEDED_ZIP64);
    le16(lfh + 6,  gpflags);
    le16(lfh + 8,  METHOD_STORE);
    le16(lfh + 10, 0); le16(lfh + 12, 0);    /* DOS time/date = 0 */
    le32(lfh + 14, 0);                       /* CRC (unknown) */
    le32(lfh + 18, 0xFFFFFFFFu);             /* comp size (unknown) */
    le32(lfh + 22, 0xFFFFFFFFu);             /* uncomp size (unknown) */
    le16(lfh + 26, (uint16_t)name_len);
    le16(lfh + 28, 0);                       /* no LFH extras (we use DD) */

    if (TZ_FWRITE(lfh, 1, sizeof lfh, w->fp) != sizeof lfh) { fclose(in); return TACOZ_ERR_IO; }
    if (name_len && TZ_FWRITE(arc_name, 1, name_len, w->fp) != name_len) { fclose(in); return TACOZ_ERR_IO; }

    /* Stream the file bytes while updating CRC32. */
    crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;  /* xor-in */
    uint64_t sz  = 0;

    for (;;) {
        size_t r = TZ_FREAD(w->buf, 1, w->buf_cap, in);
        if (r > 0) {
            crc = crc32_update_stream(crc, w->buf, r);
            if (TZ_FWRITE(w->buf, 1, r, w->fp) != r) { fclose(in); return TACOZ_ERR_IO; }
            sz += (uint64_t)r;
        }
        if (r < w->buf_cap) {    /* short read ⇒ EOF or error */
            if (ferror(in)) { fclose(in); return TACOZ_ERR_IO; }
            break;
        }
    }
    fclose(in);
    crc ^= 0xFFFFFFFFu;          /* xor-out */

    /* ZIP64 data descriptor: signature + CRC32 + comp(8) + uncomp(8). */
    unsigned char dd[4 + 4 + 8 + 8];
    size_t dd_len = 0;
    le32(dd + dd_len, SIG_DD); dd_len += 4;
    le32(dd + dd_len, crc);    dd_len += 4;
    le64(dd + dd_len, sz);     dd_len += 8;   /* comp size (STORE == raw) */
    le64(dd + dd_len, sz);     dd_len += 8;   /* uncomp size */

    if (TZ_FWRITE(dd, 1, dd_len, w->fp) != dd_len) return TACOZ_ERR_IO;

    /* Record for Central Directory emission. */
    cd_ent_t meta = (cd_ent_t){0};
    meta.name        = (char*)arc_name;  /* duplicated in push_entry() */
    meta.gp_flags    = gpflags;
    meta.method      = METHOD_STORE;
    meta.crc32       = crc;
    meta.comp_size   = sz;
    meta.uncomp_size = sz;
    meta.lfh_offset  = lfh_off;

    return push_entry(w, &meta);
}

/* -------------------- Central Directory + EOCD emitters -------------------- */
static int write_cd_and_eocd(zipw_t *w){
    long long cd_start_ll = ftello(w->fp);
    if (cd_start_ll < 0) return TACOZ_ERR_IO;
    uint64_t cd_start = (uint64_t)cd_start_ll;

    /* Emit each CDFH as one contiguous block: [CDFH | name | ZIP64 extra]. */
    for (size_t i = 0; i < w->n; i++) {
        cd_ent_t *e = &w->ents[i];
        size_t nlen = strlen(e->name);
        if (nlen > 0xFFFFu) return TACOZ_ERR_PARAM;
        uint16_t nlen16 = (uint16_t)nlen;

        const size_t extra_len = 2 + 2 + 8 + 8 + 8; /* id+sz + uncomp + comp + lfh_off = 28 */
        const size_t cdfh_len  = 46;
        const size_t total_len = cdfh_len + nlen + extra_len;

        unsigned char *blk = (unsigned char*)malloc(total_len);
        if (!blk) return TACOZ_ERR_IO;

        unsigned char *p = blk;
        le32(p + 0,  SIG_CDFH);
        le16(p + 4,  VER_MADE_BY);
        le16(p + 6,  VER_NEEDED_ZIP64);
        le16(p + 8,  e->gp_flags);
        le16(p + 10, e->method);
        le16(p + 12, 0); le16(p + 14, 0);   /* DOS time/date = 0 */
        le32(p + 16, e->crc32);
        le32(p + 20, 0xFFFFFFFFu);          /* comp size (ZIP64 extra has real) */
        le32(p + 24, 0xFFFFFFFFu);          /* uncomp size */
        le16(p + 28, nlen16);
        le16(p + 30, (uint16_t)extra_len);
        le16(p + 32, 0);                    /* file comment length */
        le16(p + 34, 0);                    /* disk number start */
        le16(p + 36, 0);                    /* internal attrs */
        le32(p + 38, 0);                    /* external attrs */
        le32(p + 42, 0xFFFFFFFFu);          /* rel. LFH offset (real in ZIP64 extra) */

        if (nlen) memcpy(p + cdfh_len, e->name, nlen);

        /* ZIP64 extra: ID=0x0001, size=24: [uncomp(8), comp(8), lfh_off(8)] */
        unsigned char *ex = p + cdfh_len + nlen;
        le16(ex + 0,  0x0001);
        le16(ex + 2,  24);
        le64(ex + 4,  e->uncomp_size);
        le64(ex + 12, e->comp_size);
        le64(ex + 20, e->lfh_offset);

        if (TZ_FWRITE(blk, 1, total_len, w->fp) != total_len) { free(blk); return TACOZ_ERR_IO; }
        free(blk);
    }

    long long cd_end_ll = ftello(w->fp);
    if (cd_end_ll < 0) return TACOZ_ERR_IO;
    uint64_t cd_end  = (uint64_t)cd_end_ll;
    uint64_t cd_size = cd_end - cd_start;
    uint64_t total_entries = (uint64_t)w->n;

    /* ZIP64 EOCD (fixed-size body = 44 bytes). */
    unsigned char z64e[56];
    le32(z64e + 0,  SIG_ZIP64_EOCD);
    le64(z64e + 4,  44);
    le16(z64e + 12, VER_MADE_BY);
    le16(z64e + 14, VER_NEEDED_ZIP64);
    le32(z64e + 16, 0);                    /* disk number */
    le32(z64e + 20, 0);                    /* CD start disk */
    le64(z64e + 24, total_entries);        /* # entries on this disk */
    le64(z64e + 32, total_entries);        /* total # entries */
    le64(z64e + 40, cd_size);              /* size of central dir */
    le64(z64e + 48, cd_start);             /* offset of start of central dir */
    if (TZ_FWRITE(z64e, 1, sizeof z64e, w->fp) != sizeof z64e) return TACOZ_ERR_IO;

    /* ZIP64 Locator: points to the EOCD64 we just wrote. */
    uint64_t z64e_off = (uint64_t)cd_end;
    unsigned char loc[20];
    le32(loc + 0,  SIG_ZIP64_LOCATOR);
    le32(loc + 4,  0);                     /* disk with EOCD64 */
    le64(loc + 8,  z64e_off);              /* absolute offset of EOCD64 */
    le32(loc + 16, 1);                     /* total number of disks */
    if (TZ_FWRITE(loc, 1, sizeof loc, w->fp) != sizeof loc) return TACOZ_ERR_IO;

    /* Classic EOCD with truncated maxima; required for compatibility. */
    unsigned char eocd[22];
    le32(eocd + 0,  SIG_EOCD);
    le16(eocd + 4,  0);                     /* disk no */
    le16(eocd + 6,  0);                     /* CD start disk */
    le16(eocd + 8,  0xFFFF);                /* entries on this disk (truncated) */
    le16(eocd + 10, 0xFFFF);                /* total entries (truncated) */
    le32(eocd + 12, 0xFFFFFFFFu);           /* size of CD (truncated) */
    le32(eocd + 16, 0xFFFFFFFFu);           /* offset of CD start (truncated) */
    le16(eocd + 20, 0);                     /* comment length */
    if (TZ_FWRITE(eocd, 1, sizeof eocd, w->fp) != sizeof eocd) return TACOZ_ERR_IO;

    return TACOZ_OK;
}

/* -------------------------------- Public API ------------------------------- */
int tacozip_create(const char *zip_path,
                   const char * const *src_files,
                   const char * const *arc_files,
                   size_t num_files,
                   uint64_t meta_offset,
                   uint64_t meta_length)
{
    if (!zip_path || !src_files || !arc_files || num_files == 0)
        return TACOZ_ERR_PARAM;

    FILE *fp = fopen(zip_path, "wb");
    if (!fp) return TACOZ_ERR_IO;

    zipw_t w = {0};
    w.fp = fp;

    /* Attach a large output buffer to this FILE*. IMPORTANT: do not free before fclose(). */
    w.out_buf = (unsigned char*)malloc(TACOZ_OUT_BUFSZ);
    if (w.out_buf) {
        w.out_buf_cap = TACOZ_OUT_BUFSZ;
        (void)setvbuf(fp, (char*)w.out_buf, _IOFBF, w.out_buf_cap);
    }

    try_posix_fallocate_estimate(fp, src_files, arc_files, num_files);

    /* 1) Fixed ghost at byte 0. */
    if (write_ghost(fp, meta_offset, meta_length) != TACOZ_OK) {
        (void)fclose(fp);                /* flush before freeing setvbuf buffer */
        if (w.out_buf) free(w.out_buf);
        return TACOZ_ERR_IO;
    }

    /* 2) Stream each file: LFH→data→DD. */
    for (size_t i = 0; i < num_files; i++) {
        if (!src_files[i] || !arc_files[i]) {
            (void)fclose(fp);
            if (w.buf) free(w.buf);
            if (w.out_buf) free(w.out_buf);
            for (size_t j = 0; j < w.n; j++) free(w.ents[j].name);
            free(w.ents);
            return TACOZ_ERR_PARAM;
        }
        int r = add_file(&w, src_files[i], arc_files[i]);
        if (r != TACOZ_OK) {
            (void)fclose(fp);
            if (w.buf) free(w.buf);
            if (w.out_buf) free(w.out_buf);
            for (size_t j = 0; j < w.n; j++) free(w.ents[j].name);
            free(w.ents);
            return r;
        }
    }

    /* 3) Emit CD + EOCD64 + locator + classic EOCD. */
    int rc = write_cd_and_eocd(&w);

    /* Flush/close BEFORE freeing the setvbuf buffer (glibc uses it on flush). */
    if (fclose(fp) != 0) {
        if (w.buf) free(w.buf);
        if (w.out_buf) free(w.out_buf);
        for (size_t i = 0; i < w.n; i++) free(w.ents[i].name);
        free(w.ents);
        return TACOZ_ERR_IO;
    }

    /* Now release memory safely. */
    if (w.buf) free(w.buf);
    if (w.out_buf) free(w.out_buf);
    for (size_t i = 0; i < w.n; i++) free(w.ents[i].name);
    free(w.ents);

    if (rc != TACOZ_OK) return rc;
    return TACOZ_OK;
}

/* ----------------------------- Ghost helpers ------------------------------- */
/* Minimal structural validation: signature, name/extra lengths, fixed name. */
static int validate_ghost_buf(const unsigned char b[TACO_GHOST_SIZE]) {
    uint32_t sig = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    if (sig != SIG_LFH) return TACOZ_ERR_INVALID_GHOST;
    uint16_t name_len  = (uint16_t)b[26] | ((uint16_t)b[27] << 8);
    uint16_t extra_len = (uint16_t)b[28] | ((uint16_t)b[29] << 8);
    if (name_len != TACO_GHOST_NAME_LEN || extra_len != EXTRA_TOTAL_LEN) return TACOZ_ERR_INVALID_GHOST;
    if (memcmp(b + 30, TACO_GHOST_NAME, TACO_GHOST_NAME_LEN) != 0) return TACOZ_ERR_INVALID_GHOST;
    return TACOZ_OK;
}

int tacozip_read_ghost(const char *zip_path, taco_meta_ptr_t *out) {
    if (!zip_path || !out) return TACOZ_ERR_PARAM;
    FILE *fp = fopen(zip_path, "rb");
    if (!fp) return TACOZ_ERR_IO;

    unsigned char b[TACO_GHOST_SIZE];
    size_t n = fread(b, 1, sizeof b, fp);
    fclose(fp);
    if (n != sizeof b) return TACOZ_ERR_IO;

    int v = validate_ghost_buf(b);
    if (v != TACOZ_OK) return v;

    /* LE64 decode of offset/length payload. */
    uint64_t off = 0, len = 0;
    for (int i = 0; i < 8; i++) off |= ((uint64_t)b[44 + i]) << (8u * i);
    for (int i = 0; i < 8; i++) len |= ((uint64_t)b[52 + i]) << (8u * i);
    out->offset = off;
    out->length = len;
    return TACOZ_OK;
}

int tacozip_update_ghost(const char *zip_path, uint64_t new_offset, uint64_t new_length) {
    if (!zip_path) return TACOZ_ERR_PARAM;
    FILE *fp = fopen(zip_path, "r+b");
    if (!fp) return TACOZ_ERR_IO;

    /* Validate existing ghost before in-place patch. */
    unsigned char b[TACO_GHOST_SIZE];
    if (fread(b, 1, sizeof b, fp) != sizeof b) { fclose(fp); return TACOZ_ERR_IO; }
    int v = validate_ghost_buf(b);
    if (v != TACOZ_OK) { fclose(fp); return v; }

    /* Seek to payload and patch LE64 offset/length. */
    if (fseeko(fp, 44, SEEK_SET) != 0) { fclose(fp); return TACOZ_ERR_IO; }
    unsigned char le[8];
    le64(le, new_offset);
    if (TZ_FWRITE(le, 1, 8, fp) != 8) { fclose(fp); return TACOZ_ERR_IO; }
    le64(le, new_length);
    if (TZ_FWRITE(le, 1, 8, fp) != 8) { fclose(fp); return TACOZ_ERR_IO; }

    if (fclose(fp) != 0) return TACOZ_ERR_IO;
    return TACOZ_OK;
}
