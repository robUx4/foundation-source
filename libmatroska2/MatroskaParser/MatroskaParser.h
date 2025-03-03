/*
 * Copyright (c) 2008-2010, Matroska (non-profit organisation)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MATROSKA_PARSER_H
#define MATROSKA_PARSER_H

#include "matroska2/matroska.h"

typedef int64_t longlong;
typedef uint64_t ulonglong;

typedef struct InputStream
{
	int (*read)(struct InputStream *cc, filepos_t pos, void *buffer, size_t count);
	filepos_t (*scan)(struct InputStream *cc, filepos_t start, unsigned signature);
	size_t (*getcachesize)(struct InputStream *cc);
	filepos_t (*getfilesize)(struct InputStream *cc);
	const char *(*geterror)(struct InputStream *cc);
	int (*progress)(struct InputStream *cc, filepos_t cur, filepos_t max);

    int (*ioreadch)(struct InputStream *inf);
    int (*ioread)(struct InputStream *inf,void *buffer,int count);
    void (*ioseek)(struct InputStream *inf,longlong where,int how);
    filepos_t (*iotell)(struct InputStream *inf);
    void* (*makeref)(struct InputStream *inf,int count);
	void (*releaseref)(struct InputStream *inf,void* ref);

	void *(*memalloc)(struct InputStream *cc,size_t size);
	void *(*memrealloc)(struct InputStream *cc,void *mem,size_t newsize);
	void (*memfree)(struct InputStream *cc,void *mem);
} InputStream;

typedef struct TrackInfo
{
	int Number;
	uint8_t Type;
	uint64_t UID;

	uint8_t *CodecPrivate;
	size_t CodecPrivateSize;
	mkv_timestamp_t DefaultDuration;
	char *CodecID;
	char *Name;
	char Language[4];

	bool_t Enabled;
	bool_t Default;
	bool_t Lacing;
	bool_t DecodeAll;
	float TimestampScale;

	int TrackOverlay;
	uint8_t MinCache;
	size_t MaxCache;
	size_t MaxBlockAdditionID;

  union {
    struct {
      uint8_t   StereoMode;
      uint8_t   DisplayUnit;
      uint8_t   AspectRatioType;
      uint32_t    PixelWidth;
      uint32_t    PixelHeight;
      uint32_t    DisplayWidth;
      uint32_t    DisplayHeight;
      uint32_t    CropL, CropT, CropR, CropB;
      unsigned int    ColourSpace;
      float           GammaValue;
      //struct {
    unsigned int  Interlaced:1;
      //};
    } Video;
    struct {
      float     SamplingFreq;
      float     OutputSamplingFreq;
      uint8_t   Channels;
      uint8_t   BitDepth;
    } Audio;
  } AV;

} TrackInfo;

typedef struct Attachment
{
	filepos_t Position;
	filepos_t Length;
	uint64_t UID;
	char* Name;
	char* Description;
	char* MimeType;

} Attachment;

typedef struct ChapterDisplay
{
	char *String;
	char Language[4];
	char Country[4];

} ChapterDisplay;

typedef struct Chapter
{
	size_t nChildren;
	struct Chapter *Children;
	size_t nDisplay;
	struct ChapterDisplay *Display;

	mkv_timestamp_t Start;
	mkv_timestamp_t End;
	uint64_t UID;

	bool_t Enabled;
	bool_t Ordered;
	bool_t Default;
	bool_t Hidden;

	array aChildren; // Chapter
	array aDisplays;  // ChapterDisplay

} Chapter;

typedef struct Target {
  uint64_t UID;
  uint8_t  Type;
  uint8_t  Level;
};
// Tag Target types
#define TARGET_TRACK      0
#define TARGET_CHAPTER    1
#define TARGET_ATTACHMENT 2
#define TARGET_EDITION    3

typedef struct SimpleTag
{
	char *Name;
	char *Value;
    char Language[4];
    bool_t Default;

} SimpleTag;

typedef struct Tag
{
	size_t nSimpleTags;
	SimpleTag *SimpleTags;

	size_t nTargets;
	struct Target *Targets;

	array aTargets; // Target
	array aSimpleTags; // SimpleTag

} Tag;

typedef struct SegmentInfo
{
	uint8_t UID[16];
	uint8_t PrevUID[16];
	uint8_t NextUID[16];
	char *Filename;
	char *PrevFilename;
	char *NextFilename;
	char *Title;
	char *MuxingApp;
	char *WritingApp;

	mkv_timestamp_t TimestampScale;
	mkv_timestamp_t Duration;

	datetime_t DateUTC;

} SegmentInfo;

typedef struct MatroskaFile MatroskaFile;

#define MKVF_AVOID_SEEKS    1 /* use sequential reading only */

MatroskaFile *mkv_Open(InputStream *io, char *err_msg, size_t err_msgSize);
void mkv_Close(MatroskaFile *File);

SegmentInfo *mkv_GetFileInfo(MatroskaFile *File);
size_t mkv_GetNumTracks(MatroskaFile *File);
TrackInfo *mkv_GetTrackInfo(MatroskaFile *File, size_t n);
void mkv_SetTrackMask(MatroskaFile *File, int Mask);

#define FRAME_UNKNOWN_START  0x00000001
#define FRAME_UNKNOWN_END    0x00000002
#define FRAME_KF             0x00000004

int mkv_ReadFrame(MatroskaFile *File, int mask, unsigned int *track, ulonglong *StartTime, ulonglong *EndTime, ulonglong *FilePos, unsigned int *FrameSize,
                void** FrameRef, unsigned int *FrameFlags);

#define MKVF_SEEK_TO_PREV_KEYFRAME  1

void mkv_Seek(MatroskaFile *File, mkv_timestamp_t timestamp, int flags);

void mkv_GetTags(MatroskaFile *File, Tag **, unsigned *Count);
void mkv_GetAttachments(MatroskaFile *File, Attachment **, unsigned *Count);
void mkv_GetChapters(MatroskaFile *File, Chapter **, unsigned *Count);

int mkv_TruncFloat(float f);

#endif /* MATROSKA_PARSER_H */
