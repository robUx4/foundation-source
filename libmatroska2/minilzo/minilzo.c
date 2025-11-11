// SPDX-License-Identifier: MIT
// Copyright (c) 2017, Bianco Veigel
// ported to C.  2025, Steve Lhomme

#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define public
#define private static
#define protected static
typedef long var;

typedef enum
{
    /// <summary>
    /// last instruction did not copy any literal
    /// </summary>
    ZeroCopy = 0,
    /// <summary>
    /// last instruction used to copy between 1 literal
    /// </summary>
    SmallCopy1 = 1,
    /// <summary>
    /// last instruction used to copy between 2 literals
    /// </summary>
    SmallCopy2 = 2,
    /// <summary>
    /// last instruction used to copy between 3 literals
    /// </summary>
    SmallCopy3 = 3,
    /// <summary>
    /// last instruction used to copy 4 or more literals
    /// </summary>
    LargeCopy = 4
} LzoState;

typedef struct
{
    unsigned char *data;
    size_t        Length;
} byteArray;

typedef struct
{
    byteArray     _buffer;
    long          _position;
    long          _size;
} RingBuffer;


typedef struct
{
    const unsigned char *data;
    size_t        size;
    size_t        offset;
    long          _length;
    long          OutputPosition;
    int           Instruction;
    LzoState      State;

    byteArray     DecodedBuffer;
    RingBuffer    ringBuffer;
} Stream;

static byteArray byteArrayNew(size_t len)
{
    byteArray b;
    b.data = malloc(len);
    if (b.data == NULL)
        return (byteArray){0};
    b.Length = len;
    return b;
}

static void byteArrayDelete(byteArray *b)
{
    free(b->data);
    b->data = NULL;
}

static RingBuffer RingBufferNew(int size)
{
    RingBuffer r = {0};
    r._buffer = byteArrayNew(size);
    if (r._buffer.data == NULL)
        return (RingBuffer){0};
    r._size = size;
    return r;
}

static void RingBufferDelete(RingBuffer * r)
{
    byteArrayDelete(&r->_buffer);
}

static inline uint8_t ReadByte(Stream * s)
{
    return s->data[s->offset++];
}

protected int Decode(Stream *s, byteArray * buffer, int offset, int count);

int Buffer_BlockCopy(byteArray *src, int srcOffset, byteArray *dst, int dstOffset, size_t count)
{
    if (src->Length < srcOffset + count)
        return -1;
    if (dst->Length < dstOffset + count)
        return -1;
    memcpy(&dst->data[dstOffset], &src->data[srcOffset], count);
    return 0;
}

/// <summary>
/// writes a sequence of bytes to the RingBuffer and advances the current position within this RingBuffer by the number of bytes written
/// </summary>
/// <param name="buffer">An array of bytes. This method copies count bytes from buffer to the RingBuffer.</param>
/// <param name="offset">The zero-based byte offset in buffer at which to begin copying bytes to the RingBuffer.</param>
/// <param name="count">The number of bytes to be written to the RingBuffer.</param>
int RingBuffer_Write(RingBuffer *r, byteArray * buffer, int offset, long count)
{
    if (count < 10 && (r->_position + count) < r->_size)
    {
        do
        {
            r->_buffer.data[r->_position++] = buffer->data[offset++];
        } while (--count > 0);
    }
    else
    {
        while (count > 0)
        {
            var cnt = r->_size - r->_position;
            if (cnt > count)
            {
                Buffer_BlockCopy(buffer, offset, &r->_buffer, r->_position, count);
                r->_position += count;
                return 0;
            }
            Buffer_BlockCopy(buffer, offset, &r->_buffer, r->_position, cnt);
            r->_position = 0;
            offset += cnt;
            count -= cnt;
        }
    }
    return 0;
}

/// <summary>
/// set the position relative to the current position
/// </summary>
/// <remarks>wraps the position of the end is reached</remarks>
/// <param name="offset">relative offset</param>
int RingBuffer_Seek(RingBuffer *r, int offset)
{
    r->_position += offset;
    if (r->_position > r->_size)
    {
        do
        {
            r->_position -= r->_size;
        } while (r->_position > r->_size);
        return 0;
    }
    while (r->_position < 0)
    {
        r->_position += r->_size;
    }
    return 0;
}

/// <summary>
/// reads a sequence of bytes from the RingBuffer and advances the position within the RingBuffer by the number of bytes read
/// </summary>
/// <param name="buffer">An array of bytes. When this method returns, the buffer contains the specified byte array with the values between offset and (offset + count - 1) replaced by the bytes read from the RingBuffer</param>
/// <param name="offset">The zero-based byte offset in buffer at which to begin storing the data read from the RingBuffer</param>
/// <param name="count">The maximum number of bytes to be read from the RingBuffer</param>
int RingBuffer_Read(RingBuffer *r, byteArray * buffer, int offset, int count)
{
    if (count < 10 && (r->_position + count) < r->_size)
    {
        do
        {
            buffer->data[offset++] = r->_buffer.data[r->_position++];
        } while (--count > 0);
    }
    else
    {
        while (count > 0)
        {
            var copy = r->_size - r->_position;
            if (copy > count)
            {
                Buffer_BlockCopy(&r->_buffer, r->_position, buffer, offset, count);
                r->_position += count;
                break;
            }
            Buffer_BlockCopy(&r->_buffer, r->_position, buffer, offset, copy);
            r->_position = 0;
            count -= copy;
            offset += copy;
        }
    }
    return 0;
}

/// <summary>
/// copies as sequence of bytes from the Ringbuffer at the specified distance into the buffer and also the RingBuffer itself
/// </summary>
/// <param name="buffer">An array of bytes. When this method returns, the buffer contains the specified byte array with the values between offset and (offset + count - 1) replaced by the bytes read from the RingBuffer</param>
/// <param name="offset">The zero-based byte offset in buffer at which to begin storing the data read from the RingBuffer</param>
/// <param name="distance">The distance to seek backwards before starting to copy</param>
/// <param name="count">The maximum number of bytes to be read from the RingBuffer</param>
int RingBuffer_Copy(RingBuffer *r, byteArray * buffer, int offset, int distance, int count)
{
    if (r->_position > distance && (r->_position + count) < r->_size)
    {
        if (count < 10)
        {
            do
            {
                var value = r->_buffer.data[r->_position - distance];
                r->_buffer.data[r->_position++] = value;
                buffer->data[offset++] = value;
            } while (--count > 0);
        }
        else
        {
            Buffer_BlockCopy(&r->_buffer, r->_position - distance, buffer, offset, count);
            Buffer_BlockCopy(buffer, offset, &r->_buffer, r->_position, count);
            r->_position += count;
        }
    }
    else
    {
        RingBuffer_Seek(r, -distance);
        RingBuffer_Read(r, buffer, offset, count);
        RingBuffer_Seek(r, distance - count);
        RingBuffer_Write(r, buffer, offset, count);
    }
    return 0;
}

private int ReadInternal(Stream *s, byteArray * buffer, int offset, int count)
{
    assert(count > 0);
    if (s->_length != -1 && s->OutputPosition >= s->_length)
        return -1;
    int read;
    if (s->DecodedBuffer.data == NULL)
    {
        if ((read = Decode(s, buffer, offset, count)) >= 0) return read;
        s->_length = s->OutputPosition;
        return -1;
    }
    var decodedLength = s->DecodedBuffer.Length;
    if (count > decodedLength)
    {
        Buffer_BlockCopy(&s->DecodedBuffer, 0, buffer, offset, decodedLength);
        // byteArrayDelete(&s->DecodedBuffer); / DecodedBuffer = null FIXME leaks ?
        s->OutputPosition += decodedLength; // FIXME seek ?
        return decodedLength;
    }
    Buffer_BlockCopy(&s->DecodedBuffer, 0, buffer, offset, count);
    if (decodedLength > count)
    {
        byteArray remaining = byteArrayNew(decodedLength - count);
        Buffer_BlockCopy(&s->DecodedBuffer, count, &remaining, 0, remaining.Length);
        byteArrayDelete(&s->DecodedBuffer);
        s->DecodedBuffer = remaining;
    }
    else
    {
        byteArrayDelete(&s->DecodedBuffer); // DecodedBuffer = null FIXME leaks ?
    }
    s->OutputPosition += count; // FIXME seek ?
    return count;
}

public int Read(Stream *s, byteArray * buffer, int offset, int count)
{
    if (s->_length != -1 && s->OutputPosition >= s->_length)
        return 0;
    var result = 0;
    while (count > 0)
    {
        var read = ReadInternal(s, buffer, offset, count);
        if (read == -1)
            return result;
        result += read;
        offset += read;
        count -= read;
    }
    return result;
}

private void Copy(Stream *s, byteArray * buffer, int offset, int count)
{
    assert(count > 0);
    do
    {
        var read = Read(s, buffer, offset, count);
        if (read == 0)
        {
            // FIXME errno = EOF;
            return;
        }
        RingBuffer_Write(&s->ringBuffer, buffer, offset, read);
        offset += read;
        count -= read;
    } while (count > 0);
}

private int CopyFromRingBuffer(Stream *s, byteArray * buffer, int offset, int count, int distance, int copy, int state)
{
    assert(copy >= 0);
    var result = copy + state;
    s->State = (LzoState)state;
    if (count >= result)
    {
        var size = copy;
        if (copy > distance)
        {
            size = distance;
            RingBuffer_Copy(&s->ringBuffer, buffer, offset, distance, size);
            copy -= size;
            var copies = copy / distance;
            for (int i = 0; i < copies; i++)
            {
                Buffer_BlockCopy(buffer, offset, buffer, offset + size, size);
                offset += size;
                copy -= size;
            }
            if (copies > 0)
            {
                var length = size * copies;
                RingBuffer_Write(&s->ringBuffer, buffer, offset - length, length);
            }
            offset += size;
        }
        if (copy > 0)
        {
            if (copy < size)
                size = copy;
            RingBuffer_Copy(&s->ringBuffer, buffer, offset, distance, size);
            offset += size;
        }
        if (state > 0)
        {
            Copy(s, buffer, offset, state);
        }
        return result;
    }

    if (count <= copy)
    {
        CopyFromRingBuffer(s, buffer, offset, count, distance, count, 0);
        byteArrayDelete(&s->DecodedBuffer);
        s->DecodedBuffer = byteArrayNew(result - count);
        CopyFromRingBuffer(s, &s->DecodedBuffer, 0, s->DecodedBuffer.Length, distance, copy - count, state);
        return count;
    }
    CopyFromRingBuffer(s, buffer, offset, count, distance, copy, 0);
    var remaining = count - copy;
    byteArrayDelete(&s->DecodedBuffer);
    s->DecodedBuffer = byteArrayNew(state - remaining);
    Copy(s, buffer, offset + copy, remaining);
    Copy(s, &s->DecodedBuffer, 0, state - remaining);
    return count;
}

private int SmallCopy(Stream *s, byteArray * buffer, int offset, int count)
{
    /*
        * the instruction is a copy of a
        * 2-byte block from the dictionary within a 1kB distance. It is worth
        * noting that this instruction provides little savings since it uses 2
        * bytes to encode a copy of 2 other bytes but it encodes the number of
        * following literals for free. It must be interpreted like this :
        *
        * 0 0 0 0 D D S S  (0..15)  : copy 2 bytes from <= 1kB distance
        * length = 2
        * state = S (copy S literals after this block)
        * Always followed by exactly one byte : H H H H H H H H
        * distance = (H << 2) + D + 1
        */
    var h = ReadByte(s);
    if (h != -1)
    {
        var distance = (h << 2) + ((s->Instruction & 0xc) >> 2) + 1;

        return CopyFromRingBuffer(s, buffer, offset, count, distance, 2, s->Instruction & 0x3);
    }

    // errno = EOF;
    return -1;
}

private int DoLargeCopy(Stream *s, byteArray * buffer, int offset, int count)
{
    /*
        *the instruction becomes a copy of a 3-byte block from the
        * dictionary from a 2..3kB distance, and must be interpreted like this :
        * 0 0 0 0 D D S S  (0..15)  : copy 3 bytes from 2..3 kB distance
        * length = 3
        * state = S (copy S literals after this block)
        * Always followed by exactly one byte : H H H H H H H H
        * distance = (H << 2) + D + 2049
        */
    var result = ReadByte(s);
    if (result != -1)
    {
        var distance = (result << 2) + ((s->Instruction & 0xc) >> 2) + 2049;

        return CopyFromRingBuffer(s, buffer, offset, count, distance, 3, s->Instruction & 0x3);
    }
    // errno = EOF;
    return -1;
}

private int ReadLength(Stream *s)
{
    int b;
    int length = 0;
    while ((b = ReadByte(s)) == 0)
    {
        if (length >= INT32_MAX - 1000)
        {
            // errno = ERANGE;
            return -1;
        }
        length += 255;
    }
    if (b != -1) return length + b;
    // errno = EOF;
    return -1;
}

protected int Decode(Stream *s, byteArray * buffer, int offset, int count)
{
    assert(count > 0);
    assert(s->DecodedBuffer.data == NULL);
    int read;
    var i = s->Instruction >> 4;
    switch (i)
    {
        case 0://Instruction <= 15
        {
            /*
                * Depends on the number of literals copied by the last instruction.
                */
            switch (s->State)
            {
                case ZeroCopy:
                {
                    /*
                        * this encoding will be a copy of 4 or more literal, and must be interpreted
                        * like this :                         *
                        * 0 0 0 0 L L L L  (0..15)  : copy long literal string
                        * length = 3 + (L ?: 15 + (zero_bytes * 255) + non_zero_byte)
                        * state = 4  (no extra literals are copied)
                        */
                    var length = 3;
                    if (s->Instruction != 0)
                    {
                        length += s->Instruction;
                    }
                    else
                    {
                        length += 15 + ReadLength(s);
                    }
                    s->State = LargeCopy;
                    if (length <= count)
                    {
                        Copy(s, buffer, offset, length);
                        read = length;
                    }
                    else
                    {
                        Copy(s, buffer, offset, count);
                        byteArrayDelete(&s->DecodedBuffer);
                        s->DecodedBuffer = byteArrayNew(length - count);
                        Copy(s, &s->DecodedBuffer, 0, length - count);
                        read = count;
                    }
                    break;
                }
                case SmallCopy1:
                case SmallCopy2:
                case SmallCopy3:
                    read = SmallCopy(s, buffer, offset, count);
                    break;
                case LargeCopy:
                    read = DoLargeCopy(s, buffer, offset, count);
                    break;
                default:
                    // errno = ERANGE;
                    return -1;
            }
            break;
        }
        case 1://Instruction < 32
        {
            /*
                * 0 0 0 1 H L L L  (16..31)
                * Copy of a block within 16..48kB distance (preferably less than 10B)
                * length = 2 + (L ?: 7 + (zero_bytes * 255) + non_zero_byte)
                * Always followed by exactly one LE16 :  D D D D D D D D : D D D D D D S S
                * distance = 16384 + (H << 14) + D
                * state = S (copy S literals after this block)
                * End of stream is reached if distance == 16384
                */
            int length = (s->Instruction & 0x7) + 2;
            if (length == 2)
            {
                length += 7 + ReadLength(s);
            }
            var ds = ReadByte(s);
            var d = ReadByte(s);
            if (ds != -1 && d != -1)
            {
                d = ((d << 8) | ds) >> 2;
                var distance = 16384 + ((s->Instruction & 0x8) << 11) | d;
                if (distance == 16384)
                    return -1;

                read = CopyFromRingBuffer(s, buffer, offset, count, distance, length, ds & 0x3);
                break;
            }
            // errno = EOF;
            return -1;
        }
        case 2://Instruction < 48
        case 3://Instruction < 64
        {
            /*
                * 0 0 1 L L L L L  (32..63)
                * Copy of small block within 16kB distance (preferably less than 34B)
                * length = 2 + (L ?: 31 + (zero_bytes * 255) + non_zero_byte)
                * Always followed by exactly one LE16 :  D D D D D D D D : D D D D D D S S
                * distance = D + 1
                * state = S (copy S literals after this block)
                */
            int length = (s->Instruction & 0x1f) + 2;
            if (length == 2)
            {
                length += 31 + ReadLength(s);
            }
            var ds = ReadByte(s);
            var d = ReadByte(s);
            if (ds != -1 && d != -1)
            {
                d = ((d << 8) | ds) >> 2;
                var distance = d + 1;

                read = CopyFromRingBuffer(s, buffer, offset, count, distance, length, ds & 0x3);
                break;
            }
            // errno = EOF;
            return -1;
        }
        case 4://Instruction < 80
        case 5://Instruction < 96
        case 6://Instruction < 112
        case 7://Instruction < 128
        {
            /*
                * 0 1 L D D D S S  (64..127)
                * Copy 3-4 bytes from block within 2kB distance
                * state = S (copy S literals after this block)
                * length = 3 + L
                * Always followed by exactly one byte : H H H H H H H H
                * distance = (H << 3) + D + 1
                */
            var length = 3 + ((s->Instruction >> 5) & 0x1);
            var result = ReadByte(s);
            if (result != -1)
            {
                var distance = (result << 3) + ((s->Instruction >> 2) & 0x7) + 1;

                read = CopyFromRingBuffer(s, buffer, offset, count, distance, length, s->Instruction & 0x3);
                break;
            }
            // errno = EOF;
            return -1;
        }
        case 9:
        {
            /* first byte copy literal string */
            var length = ReadByte(s) - 17;
            // s->State = LargeCopy;
            read = CopyFromRingBuffer(s, buffer, offset, count, 0, length, s->Instruction & 0x3);
            break;
        }
        default:
        {
            /*
                * 1 L L D D D S S  (128..255)
                * Copy 5-8 bytes from block within 2kB distance
                * state = S (copy S literals after this block)
                * length = 5 + L
                * Always followed by exactly one byte : H H H H H H H H
                * distance = (H << 3) + D + 1
                */
            var length = 5 + ((s->Instruction >> 5) & 0x3);
            var result = ReadByte(s);
            if (result != -1)
            {
                var distance = (result << 3) + ((s->Instruction & 0x1c) >> 2) + 1;

                read = CopyFromRingBuffer(s, buffer, offset, count, distance, length, s->Instruction & 0x3);
                break;
            }
            // errno = EOF;
            return -1;
        }
    }
    s->Instruction = ReadByte(s);
    if (s->Instruction != -1)
    {
        s->OutputPosition += read; // FIXME seek ?
        return read;
    }
    // errno = EOF;
    return -1;
}

int lzo1x_decompress_safe( const unsigned char* src, unsigned int  src_len,
                                unsigned char* dst, unsigned int* dst_len,
                                void* wrkmem /* NOT USED */ )
{
    (void)wrkmem;
    unsigned char version = 0;
    Stream Source = {
        .data = src,
        .size = src_len,
        ._length = -1,
    };
    Source.Instruction = ReadByte(&Source);
    if (Source.Instruction == -1)
    {
        // errno = EOF;
        return -1;
    }
    if (Source.Instruction > 15 && Source.Instruction <= 17)
    {
        return -1;
    }

    // first byte
    if (Source.Instruction == 17) // bitstream version
    {
        if (src_len >= 5)
        {
            version = ReadByte(&Source);
            if (version != 0) // unsupported
            {
                return -1;
            }
        }
    }
    else if (Source.Instruction >= 18 && Source.Instruction <= 21)
    {
        Source.State = Source.Instruction - 17;
        ReadByte(&Source); // skip byte
    }
    else if (Source.Instruction >= 22 && Source.Instruction <= 255)
    {
        // *dst_len = ReadByte(&Source) - 17; // skip byte
        Source.Instruction = 9 << 4;
        // Source.State = 4;
    }

#define MaxWindowSize  ((1 << 14) + ((255 & 8) << 11) + (255 << 6) + (255 >> 2))
    Source.ringBuffer = RingBufferNew(MaxWindowSize);
    if (Source.ringBuffer._buffer.data == NULL)
        return -1;

    byteArray out = {
        .data = dst,
        .Length = *dst_len,
    };
    int err = Read(&Source, &out, 0, src_len);
    *dst_len = err;
    byteArrayDelete(&Source.DecodedBuffer);
    RingBufferDelete(&Source.ringBuffer);
    return 0;
}
