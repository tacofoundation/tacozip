#ifndef TACOZIP_H
#define TACOZIP_H

/**
 * @file tacozip.h
 * @brief Minimal ZIP64 (STORE-only) writer with a fixed 64-byte "TACO Ghost"
 *        Local File Header at byte 0. The ghost is not listed in the Central Directory.
 *
 * ## Overview
 * - Always ZIP64 (“CIP64”): version_needed=45, ZIP64 extras for sizes and offsets,
 *   ZIP64 EOCD + locator, plus classic EOCD with truncated fields.
 * - STORE-only (method=0). Sizes are streamed via ZIP64 data descriptors.
 * - A fixed 64-byte “TACO Ghost” LFH is written at file start to carry
 *   (offset,length) metadata for external indices. This ghost **does not**
 *   appear in the Central Directory.
 * - No filename normalization in C; callers must pass sanitized archive names.
 *
 * ## Threading
 * - Functions are not thread-safe on the same zip_path concurrently.
 *
 * ## Large files
 * - Designed for large files: build with `_FILE_OFFSET_BITS=64`.
 *
 * ## Encoding
 * - If compiled with `TACOZ_SET_UTF8_FLAG=1`, the general purpose bit 11 is set
 *   (caller guarantees archive names are UTF-8). Otherwise, bit 11 is 0.
 *
 * ## ABI / Visibility
 * - Functions are exported with default visibility when building the shared lib.
 *   Define `TACOZIP_BUILD` when compiling the library itself.
 *
 * ## Typical usage (C)
 * @code
 *   const char *src[] = {"/abs/a.bin", "/abs/b.bin"};
 *   const char *arc[] = {"a.bin", "sub/b.bin"};
 *   int rc = tacozip_create("out.taco.zip", src, arc, 2, meta_off, meta_len);
 *   if (rc != TACOZ_OK) { handle error }
 *
 *   taco_meta_ptr_t m = {0};
 *   rc = tacozip_read_ghost("out.taco.zip", &m);
 *   rc = tacozip_update_ghost("out.taco.zip", m.offset, m.length);
 * @endcode
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TACO Ghost Layout (fixed 64 bytes at start of file):
 *
 *  [0..3]   : 0x04034B50 (Local File Header signature)
 *  [4..5]   : version_needed = 45 (ZIP64 required)
 *  [6..7]   : general_purpose_bit_flag = 0
 *  [8..9]   : compression_method = 0 (STORE)
 *  [10..13] : last_mod_time/date = 0
 *  [14..17] : crc32 = 0
 *  [18..21] : compressed_size = 0
 *  [22..25] : uncompressed_size = 0
 *  [26..27] : file_name_length = 10 ("TACO_GHOST")
 *  [28..29] : extra_field_length = 20
 *  [30..39] : file_name = "TACO_GHOST"
 *  [40..41] : extra_header_id = 0x7454
 *  [42..43] : extra_data_size = 16
 *  [44..51] : uint64_le metadata_offset
 *  [52..59] : uint64_le metadata_length
 *  [60..63] : zero padding
 *
 * This LFH is intentionally omitted from the CDR.
 */

#define TACO_GHOST_SIZE          64u
#define TACO_GHOST_NAME          "TACO_GHOST"
#define TACO_GHOST_NAME_LEN      10u
#define TACO_GHOST_EXTRA_ID      0x7454u  /* 'tT' little-endian (project-assigned) */
#define TACO_GHOST_EXTRA_SIZE    16u      /* two little-endian uint64_t */

/** @brief Pointer to external metadata carried by the ghost. */
typedef struct {
    uint64_t offset;  /**< Absolute byte offset of external metadata. */
    uint64_t length;  /**< Length in bytes of external metadata.      */
} taco_meta_ptr_t;


/* Export / visibility macro */
#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef TACOZIP_BUILD
    #define TACOZIP_EXPORT __declspec(dllexport)
  #else
    #define TACOZIP_EXPORT __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define TACOZIP_EXPORT __attribute__((visibility("default")))
  #else
    #define TACOZIP_EXPORT
  #endif
#endif


/**
 * @enum tacozip_status
 * @brief Return codes (0 = success, negative = error).
 */
enum {
    TACOZ_OK                =  0,  /**< Success. */
    TACOZ_ERR_IO            = -1,  /**< I/O error (open/read/write/close/flush). */
    TACOZ_ERR_LIBZIP        = -2,  /**< Reserved (historical); currently unused. */
    TACOZ_ERR_INVALID_GHOST = -3,  /**< Ghost bytes malformed or unexpected. */
    TACOZ_ERR_PARAM         = -4   /**< Invalid argument(s). */
};

/* ========================================================================== */
/*                                  API                                       */
/* ========================================================================== */
/**
 * @brief Create a ZIP64 archive with a fixed 64-byte TACO Ghost at byte 0.
 *
 * Behavior:
 *  - Always ZIP64 (“CIP64”), regardless of sizes.
 *  - All entries are STORE (no compression).
 *  - Each entry is written as: LFH (sizes unknown) → raw data stream →
 *    ZIP64 data descriptor (signature + CRC32 + comp_size(8) + uncomp_size(8)).
 *  - Central Directory records carry ZIP64 extra fields for sizes and LFH offsets.
 *  - ZIP64 EOCD + ZIP64 locator + classic EOCD (with truncated fields) are emitted.
 *  - The ghost LFH is **not** present in the Central Directory.
 *
 * Requirements:
 *  - `src_files[i]` exist and are readable regular files.
 *  - `arc_files[i]` are the exact names to write into the ZIP (no normalization).
 *  - `num_files > 0`.
 *
 * @param zip_path    Output path for the archive.
 * @param src_files   Array of absolute or relative filesystem paths (N elements).
 * @param arc_files   Array of archive names (N elements; used verbatim).
 * @param num_files   Number of files N.
 * @param meta_offset Metadata offset to store in the ghost (bytes).
 * @param meta_length Metadata length to store in the ghost (bytes).
 * @return            TACOZ_OK on success; negative error code otherwise.
 *
 * @note If built with `_FILE_OFFSET_BITS=64`, very large archives are supported.
 * @note If built with `TACOZ_SET_UTF8_FLAG=1`, the UTF-8 GP bit is set on entries.
 */
TACOZIP_EXPORT
int tacozip_create(const char *zip_path,
                   const char * const *src_files,
                   const char * const *arc_files,
                   size_t num_files,
                   uint64_t meta_offset,
                   uint64_t meta_length);

/**
 * @brief Read the (offset,length) pair from the 64-byte TACO Ghost at file start.
 *
 * @param zip_path  Path to an existing archive.
 * @param out       Output pointer filled with {offset,length} on success.
 * @return          TACOZ_OK on success; TACOZ_ERR_PARAM if args null;
 *                  TACOZ_ERR_IO if file I/O fails; TACOZ_ERR_INVALID_GHOST if
 *                  the first 64 bytes do not match the expected ghost layout.
 *
 * @warning This function only trusts bytes 0..63. It does not parse the Central
 *          Directory nor validate the rest of the archive.
 */
TACOZIP_EXPORT
int tacozip_read_ghost(const char *zip_path, taco_meta_ptr_t *out);

/**
 * @brief Update the ghost’s (offset,length) in place.
 *
 * @param zip_path   Path to an existing archive created by this library.
 * @param new_offset New metadata offset to write at ghost[44..51] (LE).
 * @param new_length New metadata length to write at ghost[52..59] (LE).
 * @return           TACOZ_OK on success; negative error code otherwise.
 *
 * @note The routine verifies the surrounding ghost structure before writing.
 * @note Safe for updating the ghost after external metadata is appended.
 */
TACOZIP_EXPORT
int tacozip_update_ghost(const char *zip_path,
                         uint64_t new_offset,
                         uint64_t new_length);


/* ========================================================================== */
/*                             Implementation notes                           */
/* ========================================================================== */
/*
 * 1) ZIP64 Policy
 *    - All size/offset 32-bit fields in CDFH/EOCD use truncated maxima
 *      (0xFFFF / 0xFFFFFFFF) per ZIP64 rules; true values live in ZIP64 extras.
 *
 * 2) Data descriptors
 *    - LFH uses unknown sizes (0xFFFFFFFF) + GPFLAG bit 3 set; we append a
 *      ZIP64 data descriptor with signature 0x08074B50.
 *
 * 3) Time fields
 *    - DOS time/date in LFH/CDFH are zeroed for determinism. If you later
 *      add real timestamps, ensure DOS packing and reproducibility settings.
 *
 * 4) Filenames
 *    - The library never rewrites or validates names. Enforce policy in your
 *      caller (Python/R/Matlab/Julia frontends).
 *
 * 5) Portability
 *    - Use `_FILE_OFFSET_BITS=64` on POSIX. On Windows, the implementation
 *      provides large-file I/O via 64-bit off_t equivalents.
 *
 * 6) Determinism
 *    - With fixed input order and fixed metadata, output is stable across runs.
 */                      
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TACOZIP_H */
