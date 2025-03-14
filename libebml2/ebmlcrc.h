/*
 * Copyright (c) 2008-2010, Matroska (non-profit organisation)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __LIBEBML_CRC_H
#define __LIBEBML_CRC_H

extern bool_t EBML_CRCMatches(ebml_crc *CRC, const void *Buf, size_t Size);
extern void EBML_CRCAddBuffer(ebml_crc *CRC, const void *Buf, size_t Size);
extern void EBML_CRCFinalize(ebml_crc *CRC);

#endif /* __LIBEBML_CRC_H */
