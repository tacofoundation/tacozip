#ifndef TACOZIP_H
#define TACOZIP_H

/**
 * @file tacozip.h
 * @brief ZIP64 (STORE-only) writer with libzip backend and a fixed "TACO Ghost"
 *        Local File Header at byte 0. The ghost supports up to 7 metadata entries.
 *
 * ## Overview
 * - Always ZIP64: forces ZIP64 format regardless of file sizes for serialization consistency.
 * - STORE-only (method=0). No compression for maximum throughput.
 * - Uses libzip as the underlying ZIP implementation (no more custom ZIP code).
 * - A "TACO Ghost" entry is written first so its LFH appears at file start.
 *   This ghost appear in the Central Directory as a normal file entry.
 * - Up to 7 (offset,length) metadata pairs for external indices stored in ghost payload.
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
 * ## Dependencies
 * - Requires libzip for all ZIP operations.
 *
 * ## ABI / Visibility
 * - Functions are exported with default visibility when building the shared lib.
 *   Define `TACOZIP_BUILD` when compiling the library itself.
 *
 * ## Typical usage (C)
 * @code
 *   const char *src[] = {"/abs/a.bin", "/abs/b.bin"};
 *   const char *arc[] = {"a.bin", "sub/b.bin"};
 *   
 *   // Up to 7 metadata entries
 *   uint64_t offsets[] = {1000, 2000, 0, 0, 0, 0, 0};  // 0 means unused
 *   uint64_t lengths[] = {500, 750, 0, 0, 0, 0, 0};    // 0 means unused
 *   
 *   // Create archive with files and metadata
 *   int rc = tacozip_create("out.taco.zip", src, arc, 2, offsets, lengths, 7);
 *   if (rc != TACOZ_OK) { handle error }
 *   
 *   // Update metadata entries
 *   uint64_t new_offsets[7] = {1500, 2000, 0, 0, 0, 0, 0};
 *   uint64_t new_lengths[7] = {600, 750, 0, 0, 0, 0, 0};
 *   rc = tacozip_update_ghost("out.taco.zip", new_offsets, new_lengths, 7);
 *   
 *   // Append a new file to the archive
 *   rc = tacozip_append_file("out.taco.zip", "/path/to/c.bin", "data/c.bin");
 *   
 *   // Replace an existing file in the archive
 *   rc = tacozip_replace_file("out.taco.zip", "a.bin", "/path/to/new_a.bin");
 * @endcode
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TACO Ghost Layout (variable size, minimum 160 bytes):
 *
 * The ghost entry is a regular ZIP entry but with special content:
 * - Entry name: "TACO_GHOST"
 * - Method: STORE (0)
 * - Content: binary payload with metadata pairs
 *
 * Ghost payload format:
 *  [0]      : uint8_t count (number of valid entries, 0-7)
 *  [1..3]   : padding (3 bytes for alignment)
 *  [4..115] : 7 pairs of uint64_le (offset, length) - total 112 bytes
 *
 * Total payload size: 116 bytes (4 + 7*16)
 */

#define TACO_GHOST_MAX_ENTRIES   7u
#define TACO_GHOST_PAYLOAD_SIZE  116u  /* 4 bytes header + 7*16 bytes pairs */
#define TACO_GHOST_NAME          "TACO_GHOST"
#define TACO_GHOST_NAME_LEN      10u

/** @brief Single metadata entry */
typedef struct {
    uint64_t offset;  /**< Absolute byte offset of external metadata. */
    uint64_t length;  /**< Length in bytes of external metadata.      */
} taco_meta_entry_t;

/** @brief Array of up to 7 metadata entries carried by the ghost. */
typedef struct {
    uint8_t count;                               /**< Number of valid entries (0-7). */
    taco_meta_entry_t entries[TACO_GHOST_MAX_ENTRIES]; /**< Metadata entries array. */
} taco_meta_array_t;

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
    TACOZ_ERR_LIBZIP        = -2,  /**< libzip error. */
    TACOZ_ERR_INVALID_GHOST = -3,  /**< Ghost bytes malformed or unexpected. */
    TACOZ_ERR_PARAM         = -4,  /**< Invalid argument(s). */
    TACOZ_ERR_NOT_FOUND     = -5,  /**< File not found in archive. */
    TACOZ_ERR_EXISTS        = -6   /**< File already exists in archive. */
};

/* ========================================================================== */
/*                             VERSION INFORMATION                           */
/* ========================================================================== */

/**
 * @brief Get the version string of the tacozip C library.
 *
 * This function returns a compile-time version string that identifies the 
 * version of the tacozip C library. The version string is defined by the
 * TACOZIP_VERSION_STRING macro, which is typically set during the build
 * process via CMake using the PROJECT_VERSION variable.
 *
 * @return Version string (e.g., "1.2.3").
 */
TACOZIP_EXPORT
const char* tacozip_get_version(void);

/* ========================================================================== */
/*                                 CORE API                                  */
/* ========================================================================== */

/**
 * @brief Create a ZIP64 archive with a TACO Ghost supporting up to 7 metadata entries.
 *
 * Creates a new archive using libzip backend with forced ZIP64 format and STORE compression.
 * The TACO Ghost entry is written first so its Local File Header appears at byte 0.
 * 
 * @param zip_path     Output path for the archive.
 * @param src_files    Array of absolute or relative filesystem paths (N elements).
 * @param arc_files    Array of archive names (N elements; used verbatim).
 * @param num_files    Number of files N.
 * @param meta_offsets Array of 7 uint64_t offsets (use 0 for unused entries).
 * @param meta_lengths Array of 7 uint64_t lengths (use 0 for unused entries).
 * @param array_size   Must be TACO_GHOST_MAX_ENTRIES (7) for validation.
 * @return             TACOZ_OK on success; negative error code otherwise.
 *
 * @note The function automatically detects how many entries are valid by counting
 *       non-zero pairs from the start of the arrays.
 * @note Both meta_offsets and meta_lengths arrays must have exactly 7 elements.
 * @note All files will be stored using STORE method (no compression).
 */
TACOZIP_EXPORT
int tacozip_create(const char *zip_path,
                  const char * const *src_files,
                  const char * const *arc_files,
                  size_t num_files,
                  const uint64_t *meta_offsets,
                  const uint64_t *meta_lengths,
                  size_t array_size);


/**
 * @brief Update all metadata entries in the ghost in place.
 *
 * Modifies the TACO Ghost entry in an existing archive without affecting
 * other files. The archive structure remains unchanged.
 *
 * @param zip_path     Path to an existing archive created by this library.
 * @param meta_offsets Array of 7 uint64_t offsets (use 0 for unused entries).
 * @param meta_lengths Array of 7 uint64_t lengths (use 0 for unused entries).
 * @param array_size   Must be TACO_GHOST_MAX_ENTRIES (7) for validation.
 * @return             TACOZ_OK on success; negative error code otherwise.
 *
 * @note The function automatically detects how many entries are valid by counting
 *       non-zero pairs from the start of the arrays.
 */
TACOZIP_EXPORT
int tacozip_update_ghost(const char *zip_path,
                        const uint64_t *meta_offsets,
                        const uint64_t *meta_lengths,
                        size_t array_size);

/**
 * @brief Append a new file to an existing TACO archive.
 *
 * Adds a new file to an existing archive created by this library.
 * The TACO Ghost and existing files remain unchanged. The new file will use 
 * STORE method (no compression) like all other files in the archive.
 *
 * @param zip_path    Path to an existing archive created by this library.
 * @param src_path    Path to the file to append to the archive.
 * @param arc_name    Name to use for the file in the archive.
 * @return            TACOZ_OK on success; negative error code otherwise.
 *                    Returns TACOZ_ERR_EXISTS if arc_name already exists in archive.
 *
 * @note The arc_name must not conflict with existing files in the archive.
 * @note This operation preserves the TACO Ghost and all metadata entries.
 * @note The appended file will use STORE compression method for consistency.
 */
TACOZIP_EXPORT
int tacozip_append_file(const char *zip_path,
                       const char *src_path,
                       const char *arc_name);

/**
 * @brief Replace a specific file in an existing TACO archive.
 *
 * Finds a file by its archive name and replaces it with content from a new 
 * source file. The TACO Ghost and other files remain unchanged. The replacement 
 * file will use STORE method (no compression) like all other files in the archive.
 *
 * @param zip_path     Path to an existing archive created by this library.
 * @param file_name    Name of the file in the archive to replace (exact match).
 * @param new_src_path Path to the new file that will replace the existing one.
 * @return             TACOZ_OK on success; negative error code otherwise.
 *                     Returns TACOZ_ERR_NOT_FOUND if file_name doesn't exist in archive.
 *
 * @note The file_name must match exactly as it was stored in the archive.
 * @note The new file will maintain the same archive name but with updated content.
 * @note This operation preserves the TACO Ghost and all metadata entries.
 */
TACOZIP_EXPORT
int tacozip_replace_file(const char *zip_path,
                        const char *file_name,
                        const char *new_src_path);

/* ========================================================================== */
/*                             Implementation Notes                           */
/* ========================================================================== */
/*
 * ## Automatic Count Detection
 * The library counts valid entries by scanning from index 0 until it finds
 * the first (offset=0, length=0) pair.
 * Example: [1000, 2000, 0, 0, 0, 0, 0] + [500, 750, 0, 0, 0, 0, 0] = count=2
 *
 * ## Ghost Storage Format
 * - Ghost stores exactly 7 pairs regardless of how many are valid
 * - Unused pairs are stored as (0, 0) for deterministic output
 * - Count byte allows efficient reading without scanning
 *
 * ## Validation
 * - Arrays must be exactly 7 elements for safety
 * - Functions will return TACOZ_ERR_PARAM if array_size != 7
 * - Count is automatically computed, not passed by user
 *
 * ## libzip Backend
 * - All ZIP operations use libzip for robustness
 * - Always forces ZIP64 format regardless of file sizes
 * - Always uses STORE method (no compression)
 * - Ghost entry is included in central directory as normal entry
 *
 * ## File Operations
 * - tacozip_replace_file() preserves the TACO Ghost and all metadata
 * - tacozip_append_file() preserves the TACO Ghost and all metadata
 * - All file operations maintain STORE compression method for consistency
 * - File name matching uses exact string comparison
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TACOZIP_H */