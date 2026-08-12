#ifndef MINIZ_H
#define MINIZ_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int mz_uint;
typedef int mz_bool;
#define MZ_TRUE 1
#define MZ_FALSE 0

typedef struct {
    unsigned int m_file_index;
    unsigned int m_method;
    unsigned int m_uncomp_size;
    unsigned int m_comp_size;
    unsigned int m_crc32;
    unsigned long long m_local_header_ofs;
    char m_filename[256];
} mz_zip_archive_file_stat;

typedef struct {
    FILE *file;
    long file_size;
    int num_files;
    mz_zip_archive_file_stat *files;
} mz_zip_archive;

inline mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, unsigned int flags) {
    memset(pZip, 0, sizeof(mz_zip_archive));
    pZip->file = fopen(pFilename, "rb");
    if (!pZip->file) return MZ_FALSE;

    fseek(pZip->file, 0, SEEK_END);
    pZip->file_size = ftell(pZip->file);
    fseek(pZip->file, 0, SEEK_SET);

    int capacity = 10;
    pZip->files = (mz_zip_archive_file_stat*)malloc(capacity * sizeof(mz_zip_archive_file_stat));
    
    long pos = 0;
    while (pos < pZip->file_size - 30) {
        fseek(pZip->file, pos, SEEK_SET);
        unsigned int sig = 0;
        if (fread(&sig, 1, 4, pZip->file) != 4) break;
        
        if (sig == 0x04034b50) {
            unsigned short version, flags_val, method, time, date;
            unsigned int crc, comp_size, uncomp_size;
            unsigned short fname_len, extra_len;

            fread(&version, 2, 1, pZip->file);
            fread(&flags_val, 2, 1, pZip->file);
            fread(&method, 2, 1, pZip->file);
            fread(&time, 2, 1, pZip->file);
            fread(&date, 2, 1, pZip->file);
            fread(&crc, 4, 1, pZip->file);
            fread(&comp_size, 4, 1, pZip->file);
            fread(&uncomp_size, 4, 1, pZip->file);
            fread(&fname_len, 2, 1, pZip->file);
            fread(&extra_len, 2, 1, pZip->file);

            char fname[256] = {0};
            if (fname_len > 0 && fname_len < 256) {
                fread(fname, 1, fname_len, pZip->file);
            } else if (fname_len >= 256) {
                fseek(pZip->file, fname_len, SEEK_CUR);
            }

            if (pZip->num_files >= capacity) {
                capacity *= 2;
                pZip->files = (mz_zip_archive_file_stat*)realloc(pZip->files, capacity * sizeof(mz_zip_archive_file_stat));
            }

            mz_zip_archive_file_stat *st = &pZip->files[pZip->num_files];
            st->m_file_index = pZip->num_files;
            st->m_method = method;
            st->m_comp_size = comp_size;
            st->m_uncomp_size = uncomp_size;
            st->m_crc32 = crc;
            st->m_local_header_ofs = pos;
            strncpy(st->m_filename, fname, 255);

            pZip->num_files++;
            pos = ftell(pZip->file) + extra_len + comp_size;
        } else {
            pos++;
        }
    }

    return pZip->num_files > 0 ? MZ_TRUE : MZ_FALSE;
}

inline unsigned int mz_zip_reader_get_num_files(mz_zip_archive *pZip) {
    return pZip ? pZip->num_files : 0;
}

inline mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, unsigned int file_index, mz_zip_archive_file_stat *pStat) {
    if (!pZip || file_index >= (unsigned int)pZip->num_files) return MZ_FALSE;
    *pStat = pZip->files[file_index];
    return MZ_TRUE;
}

inline mz_bool mz_zip_reader_is_file_a_directory(mz_zip_archive *pZip, unsigned int file_index) {
    if (!pZip || file_index >= (unsigned int)pZip->num_files) return MZ_FALSE;
    size_t len = strlen(pZip->files[file_index].m_filename);
    if (len == 0) return MZ_FALSE;
    char last = pZip->files[file_index].m_filename[len - 1];
    return (last == '/' || last == '\\') ? MZ_TRUE : MZ_FALSE;
}

inline mz_bool mz_zip_reader_extract_to_file(mz_zip_archive *pZip, unsigned int file_index, const char *pDst_filename, unsigned int flags) {
    if (!pZip || file_index >= (unsigned int)pZip->num_files) return MZ_FALSE;
    mz_zip_archive_file_stat *st = &pZip->files[file_index];

    fseek(pZip->file, st->m_local_header_ofs, SEEK_SET);
    unsigned int sig;
    fread(&sig, 1, 4, pZip->file);
    
    unsigned short version, flags_val, method, time, date;
    unsigned int crc, comp_size, uncomp_size;
    unsigned short fname_len, extra_len;

    fread(&version, 2, 1, pZip->file);
    fread(&flags_val, 2, 1, pZip->file);
    fread(&method, 2, 1, pZip->file);
    fread(&time, 2, 1, pZip->file);
    fread(&date, 2, 1, pZip->file);
    fread(&crc, 4, 1, pZip->file);
    fread(&comp_size, 4, 1, pZip->file);
    fread(&uncomp_size, 4, 1, pZip->file);
    fread(&fname_len, 2, 1, pZip->file);
    fread(&extra_len, 2, 1, pZip->file);

    fseek(pZip->file, fname_len + extra_len, SEEK_CUR);

    FILE *out = fopen(pDst_filename, "wb");
    if (!out) return MZ_FALSE;

    if (st->m_method == 0) {
        char *buf = (char*)malloc(8192);
        unsigned int remaining = comp_size;
        while (remaining > 0) {
            unsigned int to_read = remaining < 8192 ? remaining : 8192;
            size_t read_bytes = fread(buf, 1, to_read, pZip->file);
            if (read_bytes == 0) break;
            fwrite(buf, 1, read_bytes, out);
            remaining -= read_bytes;
        }
        free(buf);
    } else if (st->m_method == 8) {
        char *comp_buf = (char*)malloc(comp_size);
        fread(comp_buf, 1, comp_size, pZip->file);

        char *uncomp_buf = (char*)malloc(uncomp_size > 0 ? uncomp_size : 1);

        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        strm.next_in = (Bytef*)comp_buf;
        strm.avail_in = comp_size;
        strm.next_out = (Bytef*)uncomp_buf;
        strm.avail_out = uncomp_size;

        if (inflateInit2(&strm, -15) == Z_OK) {
            inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
        }

        fwrite(uncomp_buf, 1, uncomp_size, out);
        free(comp_buf);
        free(uncomp_buf);
    }

    fclose(out);
    return MZ_TRUE;
}

inline mz_bool mz_zip_reader_end(mz_zip_archive *pZip) {
    if (!pZip) return MZ_FALSE;
    if (pZip->file) fclose(pZip->file);
    if (pZip->files) free(pZip->files);
    memset(pZip, 0, sizeof(mz_zip_archive));
    return MZ_TRUE;
}

#ifdef __cplusplus
}
#endif

#endif // MINIZ_H