/*
 * Generic converter template for a selected ASN.1 type.
 * Copyright (c) 2005-2017 Lev Walkin <vlm@lionet.info>.
 * All rights reserved.
 * 
 * To compile with your own ASN.1 type, please redefine the PDU as shown:
 * 
 * cc -DPDU=MyCustomType -o myDecoder.o -c converter-sample.c
 */


#ifdef    HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>    /* for atoi(3) */
#include <unistd.h>    /* for getopt(3) */
#include <string.h>    /* for strerror(3) */
#include <sysexits.h>    /* for EX_* exit codes */
#include <errno.h>    /* for errno */

#include "asn1-j2735-lib.h"

/* Convert "Type" defined by -DPDU into "asn_DEF_Type" */
// As an example, Type = MessageFrame for J2735, so the type asn_DEF_MessageFrame has been automatically generated by
// the compiler and these preprocessor directives infer the name of this type from the -DPDU specifier when compiled.
// See MessageFrame.c for the actual type in this case.
#define    ASN_DEF_PDU(t)    asn_DEF_ ## t
#define    DEF_PDU_Type(t)    ASN_DEF_PDU(t)
#define    PDU_Type    DEF_PDU_Type(PDU)

// extern asn_TYPE_descriptor_t PDU_Type;    /* ASN.1 type to be decoded */
// #ifdef    ASN_PDU_COLLECTION        /* Generated by asn1c: -pdu=... */
// extern asn_TYPE_descriptor_t *asn_pdu_collection[];
// #endif

static int opt_stack;    /* -s (maximum stack size) */
static int opt_nopad;    /* -per-nopad (PER input is not padded) */
static int opt_onepdu;    /* -1 (decode single PDU) */

/* Debug output function */
static inline void
DEBUG(const char *fmt, ...) {
    va_list ap;
    if(!opt_debug) return;
    fprintf(stderr, "AD: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static struct dynamic_buffer {

    uint8_t *data;                  /* Pointer to the data bytes */
    size_t   offset;                /* Byte (uint8_t) Offset from the start (for the next write?) */
    size_t   length;                /* Length (amount) of meaningful contents (from start or offset) */
    size_t   unbits;                /* Unused bits in the last byte (to allow intra byte boundaries) */
    size_t   allocated;             /* Allocated memory for data (total size of buffer) */
    int      nreallocs;             /* Number of data reallocations */
    off_t    bytes_shifted;         /* Number of bytes ever shifted */

} DynamicBuffer;

static void
buffer_dump() {
    uint8_t *p = DynamicBuffer.data + DynamicBuffer.offset;
    uint8_t *e = p + DynamicBuffer.length - (DynamicBuffer.unbits ? 1 : 0);
    if(!opt_debug) return;
    DEBUG("Buffer: { d=%p, o=%ld, l=%ld, u=%ld, a=%ld, s=%ld }",
        DynamicBuffer.data,
        (long)DynamicBuffer.offset,
        (long)DynamicBuffer.length,
        (long)DynamicBuffer.unbits,
        (long)DynamicBuffer.allocated,
        (long)DynamicBuffer.bytes_shifted);
    for(; p < e; p++) {
        fprintf(stderr, " %c%c%c%c%c%c%c%c",
            ((*p >> 7) & 1) ? '1' : '0',
            ((*p >> 6) & 1) ? '1' : '0',
            ((*p >> 5) & 1) ? '1' : '0',
            ((*p >> 4) & 1) ? '1' : '0',
            ((*p >> 3) & 1) ? '1' : '0',
            ((*p >> 2) & 1) ? '1' : '0',
            ((*p >> 1) & 1) ? '1' : '0',
            ((*p >> 0) & 1) ? '1' : '0');
    }
    if(DynamicBuffer.unbits) {
        unsigned int shift;
        fprintf(stderr, " ");
        for(shift = 7; shift >= DynamicBuffer.unbits; shift--)
            fprintf(stderr, "%c", ((*p >> shift) & 1) ? '1' : '0');
        fprintf(stderr, " %ld:%ld\n",
            (long)DynamicBuffer.length - 1,
            (long)8 - DynamicBuffer.unbits);
    } else {
        fprintf(stderr, " %ld\n", (long)DynamicBuffer.length);
    }
}

/*
 * Move the buffer content left N bits, possibly joining it with
 * preceeding content.
 */
static void
buffer_shift_left(size_t offset, int bits) {

    uint8_t *ptr = DynamicBuffer.data + DynamicBuffer.offset + offset;

    // not taking up the entire last byte??
    uint8_t *end = DynamicBuffer.data + DynamicBuffer.offset + DynamicBuffer.length - 1;
    
    // no shift required.
    if(!bits) return;

    DEBUG("Shifting left %d bits off %ld (o=%ld, u=%ld, l=%ld)",
        bits, (long)offset,
        (long)DynamicBuffer.offset,
        (long)DynamicBuffer.unbits,
        (long)DynamicBuffer.length);

    if(offset) {
        int right;
        right = ptr[0] >> (8 - bits);

        DEBUG("oleft: %c%c%c%c%c%c%c%c",
            ((ptr[-1] >> 7) & 1) ? '1' : '0',
            ((ptr[-1] >> 6) & 1) ? '1' : '0',
            ((ptr[-1] >> 5) & 1) ? '1' : '0',
            ((ptr[-1] >> 4) & 1) ? '1' : '0',
            ((ptr[-1] >> 3) & 1) ? '1' : '0',
            ((ptr[-1] >> 2) & 1) ? '1' : '0',
            ((ptr[-1] >> 1) & 1) ? '1' : '0',
            ((ptr[-1] >> 0) & 1) ? '1' : '0');

        DEBUG("oriht: %c%c%c%c%c%c%c%c",
            ((ptr[0] >> 7) & 1) ? '1' : '0',
            ((ptr[0] >> 6) & 1) ? '1' : '0',
            ((ptr[0] >> 5) & 1) ? '1' : '0',
            ((ptr[0] >> 4) & 1) ? '1' : '0',
            ((ptr[0] >> 3) & 1) ? '1' : '0',
            ((ptr[0] >> 2) & 1) ? '1' : '0',
            ((ptr[0] >> 1) & 1) ? '1' : '0',
            ((ptr[0] >> 0) & 1) ? '1' : '0');

        DEBUG("mriht: %c%c%c%c%c%c%c%c",
            ((right >> 7) & 1) ? '1' : '0',
            ((right >> 6) & 1) ? '1' : '0',
            ((right >> 5) & 1) ? '1' : '0',
            ((right >> 4) & 1) ? '1' : '0',
            ((right >> 3) & 1) ? '1' : '0',
            ((right >> 2) & 1) ? '1' : '0',
            ((right >> 1) & 1) ? '1' : '0',
            ((right >> 0) & 1) ? '1' : '0');

        ptr[-1] = (ptr[-1] & (0xff << bits)) | right;

        DEBUG("after: %c%c%c%c%c%c%c%c",
            ((ptr[-1] >> 7) & 1) ? '1' : '0',
            ((ptr[-1] >> 6) & 1) ? '1' : '0',
            ((ptr[-1] >> 5) & 1) ? '1' : '0',
            ((ptr[-1] >> 4) & 1) ? '1' : '0',
            ((ptr[-1] >> 3) & 1) ? '1' : '0',
            ((ptr[-1] >> 2) & 1) ? '1' : '0',
            ((ptr[-1] >> 1) & 1) ? '1' : '0',
            ((ptr[-1] >> 0) & 1) ? '1' : '0');
    }

    buffer_dump();

    // move all the remaining bytes.
    for(; ptr < end; ptr++) {
        int right = ptr[1] >> (8 - bits);
        *ptr = (*ptr << bits) | right;
    }
    *ptr <<= bits;

    // Only place where unbits is modified from 0.
    DEBUG("Unbits [%d=>", (int)DynamicBuffer.unbits);
    if(DynamicBuffer.unbits == 0) {
        DynamicBuffer.unbits += bits;
    } else {
        DynamicBuffer.unbits += bits;
        if(DynamicBuffer.unbits > 7) {
            DynamicBuffer.unbits -= 8;
            DynamicBuffer.length--;
            DynamicBuffer.bytes_shifted++;
        }
    }
    DEBUG("Unbits =>%d]", (int)DynamicBuffer.unbits);

    buffer_dump();

    DEBUG("Shifted. Now (o=%ld, u=%ld l=%ld)",
        (long)DynamicBuffer.offset,
        (long)DynamicBuffer.unbits,
        (long)DynamicBuffer.length);
    

}

/**
 * Add data from the provided buffer to the global DynamicBuffer data buffer. The data2add should contain bytes new
 * data.
 *
 * Ensure that the buffer contains at least this amount of free space.
 */
static void add_bytes_to_buffer(const void *data2add, size_t bytes) {

    // no data read or provided.
    if(bytes == 0) return;

    DEBUG("=> add_bytes(%ld) { o=%ld l=%ld u=%ld, s=%ld }",
        (long)bytes,
        (long)DynamicBuffer.offset,
        (long)DynamicBuffer.length,
        (long)DynamicBuffer.unbits,
        (long)DynamicBuffer.allocated);

    // DynamicBuffer storage priority:
    // 1. At the end of the buffer.
    // 2. At the end of the buffer after shifting the stuff we retain to the beginning.
    // 3. Create an entirely new buffer, transfer the stuff we retain.
    
    if(DynamicBuffer.allocated >= (DynamicBuffer.offset + DynamicBuffer.length + bytes)) {
        // We can fit these bytes at the end of DynamicBuffer.
        // Done with the memcpy that follows.
        DEBUG("\tNo buffer reallocation is necessary");

    } else if(bytes <= DynamicBuffer.offset) {
        // Can't fit the bytes at the end, but CAN fit the bytes at the beginning.
        DEBUG("\tContents shifted by %ld", DynamicBuffer.offset);

        /* Shift the buffer contents (contents may overlap) */
        memmove(DynamicBuffer.data, DynamicBuffer.data + DynamicBuffer.offset, DynamicBuffer.length);
        DynamicBuffer.bytes_shifted += DynamicBuffer.offset;
        DynamicBuffer.offset = 0;

    } else {

        // Double previous size and increase by the amount needed this time.
        size_t newsize = (DynamicBuffer.allocated << 2) + bytes;

        // Where the buffer gets its memory initially and dynamically.
        void *p = MALLOC(newsize);

        if(!p) {
            perror("malloc()");
            exit(EX_OSERR);
        }

        // transfer "used" memory = offset bytes into the data buffer of the specified length.
        memcpy(p, DynamicBuffer.data + DynamicBuffer.offset, DynamicBuffer.length);

        // erases all previous evidence between data and data + offset.
        FREEMEM(DynamicBuffer.data);

        DynamicBuffer.data = (uint8_t *)p;

        DynamicBuffer.offset = 0;
        DynamicBuffer.allocated = newsize;

        // Keeping track for optimization reasons maybe?
        DynamicBuffer.nreallocs++;
        DEBUG("\tBuffer reallocated to %ld (%d time)", newsize, DynamicBuffer.nreallocs);
    }

    // Add the new data to the buffer (new, shifted, or otherwise).
    memcpy(DynamicBuffer.data + DynamicBuffer.offset + DynamicBuffer.length, data2add, bytes);
    DynamicBuffer.length += bytes;

    if(DynamicBuffer.unbits) {

        int bits = DynamicBuffer.unbits;
        DynamicBuffer.unbits = 0;
        // beginning of this block, previous number of unbits.
        buffer_shift_left(DynamicBuffer.length - bytes, bits);

    }

    DEBUG("<= add_bytes(%ld) { o=%ld l=%ld u=%ld, s=%ld }",
        (long)bytes,
        (long)DynamicBuffer.offset,
        (long)DynamicBuffer.length,
        (long)DynamicBuffer.unbits,
        (long)DynamicBuffer.allocated);
}

/**
 * For our ASN.1 Module we should already have a buffer to decode, so file operations can be avoided.
 */
void *data_decode_from_buffer(asn_TYPE_descriptor_t *pduType, const uint8_t *buffer, ssize_t bufsize, int on_first_pdu) {
    static uint8_t *fbuf;
    static ssize_t fbuf_size;
    static asn_codec_ctx_t s_codec_ctx;
    asn_codec_ctx_t *opt_codec_ctx = 0;
    void *structure = 0;
    asn_dec_rval_t rval;
    size_t old_offset;    
    size_t new_offset;
    int tolerate_eof;
    size_t rd;

    if(opt_stack) {
        s_codec_ctx.max_stack_size = opt_stack;
        opt_codec_ctx = &s_codec_ctx;
    }

    if(on_first_pdu) {
        // DynamicBuffer has global static scope.
        DynamicBuffer.offset = 0;
        DynamicBuffer.length = 0;
        DynamicBuffer.unbits = 0;
        DynamicBuffer.allocated = 0;
        DynamicBuffer.bytes_shifted = 0;
        DynamicBuffer.nreallocs = 0;
    }

    old_offset = DynamicBuffer.bytes_shifted + DynamicBuffer.offset;

    /* Pretend immediate EOF */
    rval.code = RC_WMORE;
    rval.consumed = 0;

//     for(tolerate_eof = 1;    /* Allow EOF first time buffer is non-empty */ 
//         (rd = fread(fbuf, 1, fbuf_size, file)) || feof(file) == 0 || (tolerate_eof && DynamicBuffer.length) ;) {
//         int      ecbits = 0;    /* Extra consumed bits in case of PER */
//         uint8_t *i_bptr;
//         size_t   i_size;
// 
//         /*
//          * Copy the data over, or use the original buffer.
//          */
//         if(DynamicBuffer.allocated) {
//             /* Append new data into the existing dynamic buffer */
//             add_bytes_to_buffer(fbuf, rd);
//             i_bptr = DynamicBuffer.data + DynamicBuffer.offset;
//             i_size = DynamicBuffer.length;
// 
//         } else {
// 
//             i_bptr = fbuf;
//             i_size = rd;
//         }
// 
//         DEBUG("Decoding %ld bytes", (long)i_size);
// 
//         switch(iform) {
//         case INP_BER:
//             rval = ber_decode(opt_codec_ctx, pduType,
//                 (void **)&structure, i_bptr, i_size);
//             break;
//         case INP_OER:
// #ifdef ASN_DISABLE_OER_SUPPORT
//             rval.code = RC_FAIL;
//             rval.consumed = 0;
// #else
//             rval = oer_decode(opt_codec_ctx, pduType,
//                 (void **)&structure, i_bptr, i_size);
// #endif
//             break;
//         case INP_XER:
//             rval = xer_decode(opt_codec_ctx, pduType,
//                 (void **)&structure, i_bptr, i_size);
//             break;
//         case INP_PER:
// #ifdef ASN_DISABLE_PER_SUPPORT
//             rval.code = RC_FAIL;
//             rval.consumed = 0;
// #else
//             if(opt_nopad)
//             rval = uper_decode(opt_codec_ctx, pduType,
//                 (void **)&structure, i_bptr, i_size, 0,
//                 DynamicBuffer.unbits);
//             else
//             rval = uper_decode_complete(opt_codec_ctx, pduType,
//                 (void **)&structure, i_bptr, i_size);
// #endif
//             switch(rval.code) {
//             case RC_OK:
//                 /* Fall through */
//             case RC_FAIL:
//                 if(opt_nopad) {
//                     /* uper_decode() returns bits! */
//                     /* Extra bits */
//                     ecbits = rval.consumed % 8;
//                     /* Convert into bytes! */
//                     rval.consumed /= 8;
//                 }
//                 break;
//             case RC_WMORE:
//                 /* PER does not support restartability */
//                 ASN_STRUCT_FREE(*pduType, structure);
//                 structure = 0;
//                 rval.consumed = 0;
//                 /* Continue accumulating data */
//                 break;
//             }
//             break;
//         }
//         DEBUG("decode(%ld) consumed %ld+%db (%ld), code %d",
//             (long)DynamicBuffer.length,
//             (long)rval.consumed, ecbits, (long)i_size,
//             rval.code);
// 
//         if(DynamicBuffer.allocated == 0) {
//             /*
//              * Flush remainder into the intermediate buffer.
//              */
//             if(rval.code != RC_FAIL && rval.consumed < rd) {
//                 add_bytes_to_buffer(fbuf + rval.consumed,
//                     rd - rval.consumed);
//                 buffer_shift_left(0, ecbits);
//                 DynamicBuffer.bytes_shifted = rval.consumed;
//                 rval.consumed = 0;
//                 ecbits = 0;
//             }
//         }
// 
//         /*
//          * Adjust position inside the source buffer.
//          */
//         if(DynamicBuffer.allocated) {
//             DynamicBuffer.offset += rval.consumed;
//             DynamicBuffer.length -= rval.consumed;
//         } else {
//             DynamicBuffer.bytes_shifted += rval.consumed;
//         }
// 
//         switch(rval.code) {
//         case RC_OK:
//             if(ecbits) buffer_shift_left(0, ecbits);
//             DEBUG("RC_OK, finishing up with %ld+%d",
//                 (long)rval.consumed, ecbits);
//             return structure;
//         case RC_WMORE:
//             DEBUG("RC_WMORE, continuing read=%ld, cons=%ld "
//                 " with %ld..%ld-%ld..%ld",
//                 (long)rd,
//                 (long)rval.consumed,
//                 (long)DynamicBuffer.offset,
//                 (long)DynamicBuffer.length,
//                 (long)DynamicBuffer.unbits,
//                 (long)DynamicBuffer.allocated);
//             if(!rd) tolerate_eof--;
//             continue;
//         case RC_FAIL:
//             break;
//         }
//         break;
//     }

    DEBUG("Clean up partially decoded structure");
    ASN_STRUCT_FREE(*pduType, structure);

    new_offset = DynamicBuffer.bytes_shifted + DynamicBuffer.offset;

    /*
     * Print a message and return failure only if not EOF,
     * unless this is our first PDU (empty file).
     */
    if(on_first_pdu
       || DynamicBuffer.length
       || new_offset - old_offset > ((iform == INP_XER)?sizeof("\r\n")-1:0)
      ) {

        DEBUG("ofp %d, no=%ld, oo=%ld, dbl=%ld",
            on_first_pdu, (long)new_offset, (long)old_offset,
            (long)DynamicBuffer.length);
        fprintf(stderr, "%s: "
            "Decode failed past byte %ld: %s\n",
            "buffer", (long)new_offset,
            (rval.code == RC_WMORE)
                ? "Unexpected end of input"
                : "Input processing error");
#ifndef    ENOMSG
#define    ENOMSG EINVAL
#endif
#ifndef    EBADMSG
#define    EBADMSG EINVAL
#endif
        errno = (rval.code == RC_WMORE) ? ENOMSG : EBADMSG;
    } else {
        /* Got EOF after a few successful PDUs */
        errno = 0;
    }

    return 0;

    return NULL;
}

void *
data_decode_from_file(asn_TYPE_descriptor_t *pduType, FILE *file, const char *name, ssize_t suggested_bufsize, int on_first_pdu) {
    static uint8_t *fbuf;
    static ssize_t fbuf_size;
    static asn_codec_ctx_t s_codec_ctx;
    asn_codec_ctx_t *opt_codec_ctx = 0;
    void *structure = 0;
    asn_dec_rval_t rval;
    size_t old_offset;    
    size_t new_offset;
    int tolerate_eof;
    size_t rd;

    if(!file) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        errno = EINVAL;
        return 0;
    }

    if(opt_stack) {
        s_codec_ctx.max_stack_size = opt_stack;
        opt_codec_ctx = &s_codec_ctx;
    }

    DEBUG("Processing %s", name);

    /* prepare the file buffer */
    if(fbuf_size != suggested_bufsize) {
        fbuf = (uint8_t *)REALLOC(fbuf, suggested_bufsize);
        if(!fbuf) {
            perror("realloc()");
            exit(EX_OSERR);
        }
        fbuf_size = suggested_bufsize;
    }

    if(on_first_pdu) {
        DynamicBuffer.offset = 0;
        DynamicBuffer.length = 0;
        DynamicBuffer.unbits = 0;
        DynamicBuffer.allocated = 0;
        DynamicBuffer.bytes_shifted = 0;
        DynamicBuffer.nreallocs = 0;
    }

    old_offset = DynamicBuffer.bytes_shifted + DynamicBuffer.offset;

    /* Pretend immediate EOF */
    rval.code = RC_WMORE;
    rval.consumed = 0;

    for(tolerate_eof = 1;    /* Allow EOF first time buffer is non-empty */
        (rd = fread(fbuf, 1, fbuf_size, file))
        || feof(file) == 0
        || (tolerate_eof && DynamicBuffer.length)
        ;) {
        int      ecbits = 0;    /* Extra consumed bits in case of PER */
        uint8_t *i_bptr;
        size_t   i_size;

        /*
         * Copy the data over, or use the original buffer.
         */
        if(DynamicBuffer.allocated) {
            /* Append new data into the existing dynamic buffer */
            add_bytes_to_buffer(fbuf, rd);
            i_bptr = DynamicBuffer.data + DynamicBuffer.offset;
            i_size = DynamicBuffer.length;
        } else {
            i_bptr = fbuf;
            i_size = rd;
        }

        DEBUG("Decoding %ld bytes", (long)i_size);

        switch(iform) {
        case INP_BER:
            rval = ber_decode(opt_codec_ctx, pduType,
                (void **)&structure, i_bptr, i_size);
            break;
        case INP_OER:
#ifdef ASN_DISABLE_OER_SUPPORT
            rval.code = RC_FAIL;
            rval.consumed = 0;
#else
            rval = oer_decode(opt_codec_ctx, pduType,
                (void **)&structure, i_bptr, i_size);
#endif
            break;
        case INP_XER:
            rval = xer_decode(opt_codec_ctx, pduType,
                (void **)&structure, i_bptr, i_size);
            break;
        case INP_PER:
#ifdef ASN_DISABLE_PER_SUPPORT
            rval.code = RC_FAIL;
            rval.consumed = 0;
#else
            if(opt_nopad)
                rval = uper_decode(opt_codec_ctx, pduType, (void **)&structure, i_bptr, i_size, 0, DynamicBuffer.unbits);
            else
                rval = uper_decode_complete(opt_codec_ctx, pduType, (void **)&structure, i_bptr, i_size);
#endif
            switch(rval.code) {
            case RC_OK:
                /* Fall through */
            case RC_FAIL:
                if(opt_nopad) {
                    /* uper_decode() returns bits! */
                    /* Extra bits */
                    ecbits = rval.consumed % 8;
                    /* Convert into bytes! */
                    rval.consumed /= 8;
                }
                break;
            case RC_WMORE:
                /* PER does not support restartability */
                ASN_STRUCT_FREE(*pduType, structure);
                structure = 0;
                rval.consumed = 0;
                /* Continue accumulating data */
                break;
            }
            break;
        }
        DEBUG("decode(%ld) consumed %ld+%db (%ld), code %d",
            (long)DynamicBuffer.length,
            (long)rval.consumed, ecbits, (long)i_size,
            rval.code);

        if(DynamicBuffer.allocated == 0) {
            /*
             * Flush remainder into the intermediate buffer.
             */
            if(rval.code != RC_FAIL && rval.consumed < rd) {
                add_bytes_to_buffer(fbuf + rval.consumed,
                    rd - rval.consumed);
                buffer_shift_left(0, ecbits);
                DynamicBuffer.bytes_shifted = rval.consumed;
                rval.consumed = 0;
                ecbits = 0;
            }
        }

        /*
         * Adjust position inside the source buffer.
         */
        if(DynamicBuffer.allocated) {
            DynamicBuffer.offset += rval.consumed;
            DynamicBuffer.length -= rval.consumed;
        } else {
            DynamicBuffer.bytes_shifted += rval.consumed;
        }

        switch(rval.code) {
        case RC_OK:
            if(ecbits) buffer_shift_left(0, ecbits);
            DEBUG("RC_OK, finishing up with %ld+%d",
                (long)rval.consumed, ecbits);
            return structure;
        case RC_WMORE:
            DEBUG("RC_WMORE, continuing read=%ld, cons=%ld "
                " with %ld..%ld-%ld..%ld",
                (long)rd,
                (long)rval.consumed,
                (long)DynamicBuffer.offset,
                (long)DynamicBuffer.length,
                (long)DynamicBuffer.unbits,
                (long)DynamicBuffer.allocated);
            if(!rd) tolerate_eof--;
            continue;
        case RC_FAIL:
            break;
        }
        break;
    }

    DEBUG("Clean up partially decoded structure");
    ASN_STRUCT_FREE(*pduType, structure);

    new_offset = DynamicBuffer.bytes_shifted + DynamicBuffer.offset;

    /*
     * Print a message and return failure only if not EOF,
     * unless this is our first PDU (empty file).
     */
    if(on_first_pdu
    || DynamicBuffer.length
    || new_offset - old_offset > ((iform == INP_XER)?sizeof("\r\n")-1:0)
    ) {

        DEBUG("ofp %d, no=%ld, oo=%ld, dbl=%ld",
            on_first_pdu, (long)new_offset, (long)old_offset,
            (long)DynamicBuffer.length);
        fprintf(stderr, "%s: "
            "Decode failed past byte %ld: %s\n",
            name, (long)new_offset,
            (rval.code == RC_WMORE)
                ? "Unexpected end of input"
                : "Input processing error");
#ifndef    ENOMSG
#define    ENOMSG EINVAL
#endif
#ifndef    EBADMSG
#define    EBADMSG EINVAL
#endif
        errno = (rval.code == RC_WMORE) ? ENOMSG : EBADMSG;
    } else {
        /* Got EOF after a few successful PDUs */
        errno = 0;
    }

    return 0;
}
