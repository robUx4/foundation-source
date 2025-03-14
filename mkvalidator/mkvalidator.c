/*
 * Copyright (c) 2010-2015, Matroska (non-profit organisation)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include "matroska2/matroska.h"
#include "matroska2/matroska_sem.h"
#include "mkvalidator_project.h"
#include <corec/helpers/file/streams.h>
#include <corec/str/str.h>
#include <corec/helpers/parser/parser.h>

/*!
 * \todo verify the track timestamp scale is not null
 * \todo verify that the size of frames inside a lace is legit (ie the remaining size for the last must be > 0)
 * \todo verify that items with a limited set of values don't use other values
 * \todo verify that timestamps for each track are increasing (for keyframes and p frames)
 * \todo optionally show the use of deprecated elements
 * \todo support concatenated segments
 */

static textwriter *StdErr = NULL;
static ebml_master *RSegmentInfo = NULL, *RTrackInfo = NULL, *RChapters = NULL, *RTags = NULL, *RCues = NULL, *RAttachments = NULL, *RSeekHead = NULL, *RSeekHead2 = NULL;
static array RClusters;
static array Tracks;
static size_t TrackMax=0;
static bool_t Warnings = 1;
static bool_t Live = 0;
static bool_t Details = 0;
static bool_t DivX = 0;
static bool_t Quiet = 0;
static bool_t QuickExit = 0;
static mkv_timestamp_t MinTime = INVALID_TIMESTAMP_T, MaxTime = INVALID_TIMESTAMP_T;
static mkv_timestamp_t ClusterTime = INVALID_TIMESTAMP_T;

// some macros for code readability
#define EL_Pos(elt)         EBML_ElementPosition((const ebml_element*)elt)
#define EL_Int(elt)         EBML_IntegerValue((const ebml_integer*)elt)
#define EL_Type(elt, type)  EBML_ElementIsType((const ebml_element*)elt, type)
#define EL_DataSize(elt)    EBML_ElementDataSize((const ebml_element*)elt, 1)

typedef struct track_info
{
    int Num;
    int Kind;
    filepos_t DataLength;
    ebml_string *CodecID;

} track_info;

#ifdef TARGET_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void DebugMessage(const tchar_t* Msg,...)
{
#if !defined(NDEBUG) || defined(LOGFILE) || defined(LOGTIME)
    va_list Args;
    tchar_t Buffer[1024],*s=Buffer;

    va_start(Args,Msg);
    vstprintf_s(Buffer,TSIZEOF(Buffer), Msg, Args);
    va_end(Args);
    tcscat_s(Buffer,TSIZEOF(Buffer),T("\r\n"));
#endif

#ifdef LOGTIME
    {
        tchar_t timed[1024];
        SysTickToString(timed,TSIZEOF(timed),GetTimeTick(),1);
        stcatprintf_s(timed,TSIZEOF(timed),T(" %s"),s);
        s = timed;
    }
#endif

#if !defined(NDEBUG)
    OutputDebugString(s);
#endif

#if defined(LOGFILE)
{
    static FILE* f=NULL;
    static char s8[1024];
    size_t i;
    if (!f)
        f=fopen("\\corelog.txt","a+b");
    for (i=0;s[i];++i)
        s8[i]=(char)s[i];
    s8[i]=0;
    fputs(s8,f);
    fflush(f);
}
#endif
}
#else
#include <stdio.h>
void DebugMessage(const tchar_t* Msg,...)
{
    va_list Args;
    tchar_t Buffer[1024];

    va_start(Args,Msg);
    vstprintf_s(Buffer,TSIZEOF(Buffer), Msg, Args);
    va_end(Args);
    tcscat_s(Buffer,TSIZEOF(Buffer),T("\r\n"));

#ifdef UNICODE
    fprintf(stderr, "%ls", Buffer);
#else
    fprintf(stderr, "%s", Buffer);
#endif
}
#endif

static const tchar_t *GetProfileName(size_t ProfileNum)
{
static const tchar_t *Profile[8] = {T("unknown"), T("matroska v1"), T("matroska v2"), T("matroska v3"), T("webm"), T("matroska+DivX"), T("matroska v4"), T("matroska v5")};
    switch (ProfileNum)
    {
    default:                  return Profile[0];
    case PROFILE_MATROSKA_V1: return Profile[1];
    case PROFILE_MATROSKA_V2: return Profile[2];
    case PROFILE_MATROSKA_V3: return Profile[3];
    case PROFILE_WEBM:        return Profile[4];
    case PROFILE_DIVX:        return Profile[5];
    case PROFILE_MATROSKA_V4: return Profile[6];
    case PROFILE_MATROSKA_V5: return Profile[7];
    }
}

static int OutputError(int ErrCode, const tchar_t *ErrString, ...)
{
    tchar_t Buffer[MAXLINE];
    va_list Args;
    va_start(Args,ErrString);
    vstprintf_s(Buffer,TSIZEOF(Buffer), ErrString, Args);
    va_end(Args);
    TextPrintf(StdErr,T("\rERR%03X: %s\r\n"),ErrCode,Buffer);
    if (QuickExit)
        exit(-ErrCode);
    return -ErrCode;
}

static void OutputWarning(int ErrCode, const tchar_t *ErrString, ...)
{
    if (Warnings)
    {
        tchar_t Buffer[MAXLINE];
        va_list Args;
        va_start(Args,ErrString);
        vstprintf_s(Buffer,TSIZEOF(Buffer), ErrString, Args);
        va_end(Args);
        TextPrintf(StdErr,T("\rWRN%03X: %s\r\n"),ErrCode,Buffer);
        if (QuickExit)
            exit(-ErrCode);
    }
}

static filepos_t CheckUnknownElements(ebml_element *Elt)
{
    tchar_t IdStr[32], String[MAXPATH];
    ebml_element *SubElt;
    filepos_t VoidAmount = 0;
    for (SubElt = EBML_MasterChildren(Elt); SubElt; SubElt = EBML_MasterNext(SubElt))
    {
        if (Node_IsPartOf(SubElt,EBML_DUMMY_ID))
        {
            EBML_ElementGetName(Elt,String,TSIZEOF(String));
            EBML_IdToString(IdStr,TSIZEOF(IdStr),EBML_ElementClassID(SubElt));
            OutputWarning(12,T("Unknown element in %s %s at %") TPRId64 T(" (size %") TPRId64 T(" total %") TPRId64 T(")"),String,IdStr,EL_Pos(SubElt),EL_DataSize(SubElt), EBML_ElementFullSize(SubElt, 0));
        }
        else if (Node_IsPartOf(SubElt,EBML_VOID_CLASS))
        {
            VoidAmount = EBML_ElementFullSize(SubElt,0);
        }
        else if (Node_IsPartOf(SubElt,EBML_MASTER_CLASS))
        {
            VoidAmount += CheckUnknownElements(SubElt);
        }
    }
    return VoidAmount;
}

static int64_t gcd(int64_t a, int64_t b)
{
    for (;;)
    {
        int64_t c = a % b;
        if(!c) return b;
        a = b;
        b = c;
    }
}

static int CheckVideoTrack(ebml_master *Track, int TrackNum, int ProfileNum)
{
    int Result = 0;
    ebml_element *Elt, *PixelW, *PixelH;
    ebml_integer *Unit;
    ebml_master *Video;
    Video = (ebml_master*)EBML_MasterFindChild(Track,MATROSKA_getContextVideo());
    if (!Video)
        Result = OutputError(0xE0,T("Video track at %") TPRId64 T(" is missing a Video element"),EL_Pos(Track));
    // check the DisplayWidth and DisplayHeight are correct
    else
    {
        int64_t DisplayW = 0,DisplayH = 0;
        PixelW = EBML_MasterGetChild(Video,MATROSKA_getContextPixelWidth(), ProfileNum);
        if (!PixelW)
            Result |= OutputError(0xE1,T("Video track #%d at %") TPRId64 T(" has no pixel width"),TrackNum,EL_Pos(Track));
        PixelH = EBML_MasterGetChild(Video,MATROSKA_getContextPixelHeight(), ProfileNum);
        if (!PixelH)
            Result |= OutputError(0xE2,T("Video track #%d at %") TPRId64 T(" has no pixel height"),TrackNum,EL_Pos(Track));

        Unit = (ebml_integer*)EBML_MasterGetChild(Video,MATROSKA_getContextDisplayUnit(), ProfileNum);
        assert(Unit!=NULL);

        Elt = EBML_MasterFindChild(Video,MATROSKA_getContextDisplayWidth());
        if (Elt)
            DisplayW = EL_Int(Elt);
        else if (EL_Int(Unit)!=MATROSKA_DISPLAY_UNIT_PIXELS)
            Result |= OutputError(0xE2,T("Video track #%d at %") TPRId64 T(" has an implied non pixel width"),TrackNum,EL_Pos(Track));
        else if (PixelW)
            DisplayW = EL_Int(PixelW);

        Elt = EBML_MasterFindChild(Video,MATROSKA_getContextDisplayHeight());
        if (Elt)
            DisplayH = EL_Int(Elt);
        else if (EL_Int(Unit)!=MATROSKA_DISPLAY_UNIT_PIXELS)
            Result |= OutputError(0xE2,T("Video track #%d at %") TPRId64 T(" has an implied non pixel height"),TrackNum,EL_Pos(Track));
        else if (PixelH)
            DisplayH = EL_Int(PixelH);

        if (DisplayH==0)
            Result |= OutputError(0xE7,T("Video track #%d at %") TPRId64 T(" has a null display height"),TrackNum,EL_Pos(Track));
        if (DisplayW==0)
            Result |= OutputError(0xE7,T("Video track #%d at %") TPRId64 T(" has a null display width"),TrackNum,EL_Pos(Track));

        if (EL_Int(Unit)==MATROSKA_DISPLAY_UNIT_PIXELS && PixelW && PixelH)
        {
            // check if the pixel sizes appear valid
            if (DisplayW < EL_Int(PixelW) && DisplayH < EL_Int(PixelH))
            {
                int Serious = gcd(DisplayW,DisplayH)==1; // the DAR values were reduced as much as possible
                if (DisplayW*EL_Int(PixelH) == DisplayH*EL_Int(PixelW))
                    Serious++; // same aspect ratio as the source
                if (8*DisplayW <= EL_Int(PixelW) && 8*DisplayH <= EL_Int(PixelH))
                    Serious+=2; // too much shrinking compared to the original pixels
                if (ProfileNum!=PROFILE_WEBM)
                    --Serious; // in Matroska it's tolerated as it's been operating like that for a while

                if (Serious>2)
                    Result |= OutputError(0xE3,T("The output pixels for Video track #%d seem wrong %") TPRId64 T("x%") TPRId64 T("px from %") TPRId64 T("x%") TPRId64,TrackNum,DisplayW,DisplayH,EL_Int(PixelW),EL_Int(PixelH));
                else if (Serious)
                    OutputWarning(0xE3,T("The output pixels for Video track #%d seem wrong %") TPRId64 T("x%") TPRId64 T("px from %") TPRId64 T("x%") TPRId64,TrackNum,DisplayW,DisplayH,EL_Int(PixelW),EL_Int(PixelH));
            }
        }

        if (EL_Int(Unit)==MATROSKA_DISPLAY_UNIT_DISPLAYASPECTRATIO)
        {
            // crop values should never exist
            Elt = EBML_MasterFindChild(Video,MATROSKA_getContextPixelCropTop());
            if (Elt)
                Result |= OutputError(0xE4,T("Video track #%d is using unconstrained aspect ratio and has top crop at %") TPRId64,TrackNum,EL_Pos(Elt));
            Elt = EBML_MasterFindChild(Video,MATROSKA_getContextPixelCropBottom());
            if (Elt)
                Result |= OutputError(0xE4,T("Video track #%d is using unconstrained aspect ratio and has bottom crop at %") TPRId64,TrackNum,EL_Pos(Elt));
            Elt = EBML_MasterFindChild(Video,MATROSKA_getContextPixelCropLeft());
            if (Elt)
                Result |= OutputError(0xE4,T("Video track #%d is using unconstrained aspect ratio and has left crop at %") TPRId64,TrackNum,EL_Pos(Elt));
            Elt = EBML_MasterFindChild(Video,MATROSKA_getContextPixelCropRight());
            if (Elt)
                Result |= OutputError(0xE4,T("Video track #%d is using unconstrained aspect ratio and has right crop at %") TPRId64,TrackNum,EL_Pos(Elt));

            if (PixelW && DisplayW == EL_Int(PixelW))
                OutputWarning(0xE7,T("DisplayUnit seems to be pixels not aspect-ratio for Video track #%d %") TPRId64 T("px width from %") TPRId64,TrackNum,DisplayW,EL_Int(PixelW));
            if (PixelH && DisplayH == EL_Int(PixelH))
                OutputWarning(0xE7,T("DisplayUnit seems to be pixels not aspect-ratio for Video track #%d %") TPRId64 T("px height from %") TPRId64,TrackNum,DisplayH,EL_Int(PixelH));
        }
        else
        {
            // crop values should be less than the extended value
            PixelW = EBML_MasterGetChild(Video,MATROSKA_getContextPixelCropTop(), ProfileNum);
            PixelH = EBML_MasterGetChild(Video,MATROSKA_getContextPixelCropBottom(), ProfileNum);
            if (EL_Int(PixelW) + EL_Int(PixelH) >= DisplayH)
                Result |= OutputError(0xE5,T("Video track #%d is cropping too many vertical pixels %") TPRId64 T(" vs %") TPRId64 T(" + %") TPRId64,TrackNum, DisplayH, EL_Int(PixelW), EL_Int(PixelH));

            PixelW = EBML_MasterGetChild(Video,MATROSKA_getContextPixelCropLeft(), ProfileNum);
            PixelH = EBML_MasterGetChild(Video,MATROSKA_getContextPixelCropRight(), ProfileNum);
            if (EL_Int(PixelW) + EL_Int(PixelH) >= DisplayW)
                Result |= OutputError(0xE6,T("Video track #%d is cropping too many horizontal pixels %") TPRId64 T(" vs %") TPRId64 T(" + %") TPRId64,TrackNum, DisplayW, EL_Int(PixelW), EL_Int(PixelH));
        }
    }
    return Result;
}

static int CheckTracks(ebml_master *TrackEntries, int ProfileNum)
{
    ebml_master *Track;
    ebml_element *TrackType, *TrackNum, *Elt, *Elt2;
    ebml_string *CodecID;
    tchar_t CodecName[MAXPATH],String[MAXPATH];
    int Result = 0;
    Track = (ebml_master*)EBML_MasterFindChild(TrackEntries, MATROSKA_getContextTrackEntry());
    while (Track)
    {
        // check if the codec is valid for the profile
        TrackNum = EBML_MasterGetChild(Track, MATROSKA_getContextTrackNumber(), ProfileNum);
        if (TrackNum)
        {
            TrackType = EBML_MasterGetChild(Track, MATROSKA_getContextTrackType(), ProfileNum);
            CodecID = (ebml_string*)EBML_MasterGetChild(Track, MATROSKA_getContextCodecID(), ProfileNum);
            if (!CodecID)
                Result |= OutputError(0x300,T("Track #%d has no CodecID defined"),(int)EL_Int(TrackNum));
            else if (!TrackType)
                Result |= OutputError(0x301,T("Track #%d has no type defined"),(int)EL_Int(TrackNum));
            else
            {
                EBML_StringGet(CodecID,CodecName,TSIZEOF(CodecName));
                tcscpy_s(String,TSIZEOF(String),CodecName);
                if (tcscmp(tcsupr(String),CodecName)!=0)
                    OutputWarning(0x307,T("Track #%d codec '%s' should be uppercase"),(int)EL_Int(TrackNum),CodecName);
                if (tcslen(String)<3 || String[1]!='_' || (String[0]!='A' && String[0]!='V' && String[0]!='S' && String[0]!='B'))
                    OutputWarning(0x308,T("Track #%d codec '%s' doesn't appear to be valid"),(int)EL_Int(TrackNum),String);

                // check that the audio frequencies are not 0
                if (EL_Int(TrackType) == MATROSKA_TRACK_TYPE_AUDIO)
                {
                    Elt = EBML_MasterGetChild(Track, MATROSKA_getContextAudio(), ProfileNum);
                    if (Elt==NULL)
                        Result |= OutputError(0x309,T("Audio Track #%d has no audio settings"),(int)EL_Int(TrackNum));
                    else
                    {
                        Elt2 = EBML_MasterFindChild(Elt, MATROSKA_getContextOutputSamplingFrequency());
                        if (Elt2 && EBML_FloatValue((ebml_float*)Elt2)==0)
                            Result |= OutputError(0x30A,T("Audio Track #%d has a null output sampling frequency"),(int)EL_Int(TrackNum));
                        Elt2 = EBML_MasterFindChild(Elt, MATROSKA_getContextSamplingFrequency());
                        if (Elt2 && EBML_FloatValue((ebml_float*)Elt2)==0)
                            Result |= OutputError(0x30A,T("Audio Track #%d has a null sampling frequency"),(int)EL_Int(TrackNum));
                    }
                }

                if (ProfileNum==PROFILE_WEBM)
                {
                    if (EL_Int(TrackType) != MATROSKA_TRACK_TYPE_AUDIO && EL_Int(TrackType) != MATROSKA_TRACK_TYPE_VIDEO)
                        Result |= OutputError(0x302,T("Track #%d type %d not supported for profile '%s'"),(int)EL_Int(TrackNum),(int)EL_Int(TrackType),GetProfileName(ProfileNum));
                    if (CodecID)
                    {
                        if (EL_Int(TrackType) == MATROSKA_TRACK_TYPE_AUDIO)
                        {
                            if (!tcsisame_ascii(CodecName,T("A_VORBIS")) && !tcsisame_ascii(CodecName,T("A_OPUS")))
                                Result |= OutputError(0x303,T("Track #%d codec %s not supported for profile '%s'"),(int)EL_Int(TrackNum),CodecName,GetProfileName(ProfileNum));
                        }
                        else if (EL_Int(TrackType) == MATROSKA_TRACK_TYPE_VIDEO)
                        {
                            if (!tcsisame_ascii(CodecName,T("V_VP8")) && !tcsisame_ascii(CodecName,T("V_VP9")))
                                Result |= OutputError(0x304,T("Track #%d codec %s not supported for profile '%s'"),(int)EL_Int(TrackNum),CodecName,GetProfileName(ProfileNum));
                        }
                    }
                }
            }
        }

        // check if the AttachmentLink values match existing attachments
        TrackType = EBML_MasterFindChild(Track, MATROSKA_getContextAttachmentLink());
        while (TrackType)
        {
            if (!RAttachments)
            {
                if (TrackNum)
                    Result |= OutputError(0x305,T("Track #%d has attachment links but not attachments in the file"),(int)EL_Int(TrackNum));
                else
                    Result |= OutputError(0x305,T("Track at %") TPRId64 T(" has attachment links but not attachments in the file"),EL_Pos(Track));
                break;
            }

            for (Elt=EBML_MasterChildren(RAttachments);Elt;Elt=EBML_MasterNext(Elt))
            {
                if (EL_Type(Elt, MATROSKA_getContextAttachedFile()))
                {
                    Elt2 = EBML_MasterFindChild((ebml_master*)Elt, MATROSKA_getContextFileUID());
                    if (Elt2 && EL_Int(Elt2) == EL_Int(TrackType))
                        break;
                }
            }
            if (!Elt)
            {
                if (TrackNum)
                    Result |= OutputError(0x306,T("Track #%d attachment link UID 0x%") TPRIx64 T(" not found in attachments"),(int)EL_Int(TrackNum),EL_Int(TrackType));
                else
                    Result |= OutputError(0x306,T("Track at %") TPRId64 T(" attachment link UID 0x%") TPRIx64 T(" not found in attachments"),EL_Pos(Track),EL_Int(TrackType));
            }

            TrackType = EBML_MasterNextChild(Track, TrackType);
        }

        Track = (ebml_master*)EBML_MasterNextChild(TrackEntries, Track);
    }
    return Result;
}

struct profile_check
{
    int *Result;
    const ebml_element *Parent;
    const tchar_t *EltName;
    int ProfileMask;
};

static bool_t ProfileCallback(void *opaque, int type, const tchar_t *ClassName, const ebml_element* Elt)
{
    struct profile_check *check = opaque;
    if (type==MASTER_CHECK_PROFILE_INVALID)
        *check->Result |= OutputError(0x201,T("Invalid '%s' for profile '%s' in %s at %") TPRId64,ClassName,GetProfileName(check->ProfileMask),check->EltName,EL_Pos(check->Parent));
    else if (type==MASTER_CHECK_MISSING_MANDATORY)
        *check->Result |= OutputError(0x200,T("Missing element '%s' in %s at %") TPRId64, ClassName,check->EltName,EL_Pos(check->Parent));
    else if (type==MASTER_CHECK_MULTIPLE_UNIQUE)
        *check->Result |= OutputError(0x202,T("Unique element '%s' in %s at %") TPRId64 T(" found more than once at %") TPRId64, ClassName,check->EltName,EL_Pos(check->Parent),EL_Pos(Elt));
    return 0; // don't remove anything
}

static int CheckProfileViolation(ebml_element *Elt, int ProfileMask)
{
    int Result = 0;
    tchar_t String[MAXPATH];
    ebml_element *SubElt;
    struct profile_check Checker;

    if (Node_IsPartOf(Elt,EBML_MASTER_CLASS))
    {
        ebml_master *Master = (ebml_master*)Elt;
        EBML_ElementGetName(Elt,String,TSIZEOF(String));
        if (!EBML_MasterIsChecksumValid(Master))
            Result |= OutputError(0x203,T("Invalid checksum for element '%s' at %") TPRId64,String,EL_Pos(Elt));

        Checker.EltName = String;
        Checker.ProfileMask = ProfileMask;
        Checker.Parent = Elt;
        Checker.Result = &Result;
        EBML_MasterCheckContext(Master, ProfileMask, ProfileCallback, &Checker);

        for (SubElt = EBML_MasterChildren(Elt); SubElt; SubElt = EBML_MasterNext(SubElt))
            if (Node_IsPartOf(SubElt,EBML_MASTER_CLASS))
                Result |= CheckProfileViolation(SubElt,ProfileMask);
    }

    return Result;
}

static int CheckSeekHead(ebml_master *SeekHead)
{
    int Result = 0;
    matroska_seekpoint *RLevel1 = (matroska_seekpoint*)EBML_MasterFindChild(SeekHead, MATROSKA_getContextSeek());
    bool_t BSegmentInfo = 0, BTrackInfo = 0, BCues = 0, BTags = 0, BChapters = 0, BAttachments = 0, BSecondSeek = 0;
    while (RLevel1)
    {
        filepos_t Pos = MATROSKA_MetaSeekAbsolutePos(RLevel1);
        fourcc_t SeekId = MATROSKA_MetaSeekID(RLevel1);
        tchar_t IdString[32];

        EBML_IdToString(IdString,TSIZEOF(IdString),SeekId);
        if (Pos == INVALID_FILEPOS_T)
            Result |= OutputError(0x60,T("The SeekPoint at %") TPRId64 T(" has an unknown position (ID %s)"),EL_Pos(RLevel1),IdString);
        else if (SeekId==0)
            Result |= OutputError(0x61,T("The SeekPoint at %") TPRId64 T(" has no ID defined (position %") TPRId64 T(")"),EL_Pos(RLevel1),Pos);
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextInfo()))
        {
            if (!RSegmentInfo)
                Result |= OutputError(0x62,T("The SeekPoint at %") TPRId64 T(" references an unknown SegmentInfo at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RSegmentInfo) != Pos)
                Result |= OutputError(0x63,T("The SeekPoint at %") TPRId64 T(" references a SegmentInfo at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RSegmentInfo));
            BSegmentInfo = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextTracks()))
        {
            if (!RTrackInfo)
                Result |= OutputError(0x64,T("The SeekPoint at %") TPRId64 T(" references an unknown TrackInfo at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RTrackInfo) != Pos)
                Result |= OutputError(0x65,T("The SeekPoint at %") TPRId64 T(" references a TrackInfo at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RTrackInfo));
            BTrackInfo = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextCues()))
        {
            if (!RCues)
                Result |= OutputError(0x66,T("The SeekPoint at %") TPRId64 T(" references an unknown Cues at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RCues) != Pos)
                Result |= OutputError(0x67,T("The SeekPoint at %") TPRId64 T(" references a Cues at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RCues));
            BCues = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextTags()))
        {
            if (!RTags)
                Result |= OutputError(0x68,T("The SeekPoint at %") TPRId64 T(" references an unknown Tags at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RTags) != Pos)
                Result |= OutputError(0x69,T("The SeekPoint at %") TPRId64 T(" references a Tags at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RTags));
            BTags = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextChapters()))
        {
            if (!RChapters)
                Result |= OutputError(0x6A,T("The SeekPoint at %") TPRId64 T(" references an unknown Chapters at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RChapters) != Pos)
                Result |= OutputError(0x6B,T("The SeekPoint at %") TPRId64 T(" references a Chapters at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RChapters));
            BChapters = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextAttachments()))
        {
            if (!RAttachments)
                Result |= OutputError(0x6C,T("The SeekPoint at %") TPRId64 T(" references an unknown Attachments at %") TPRId64,EL_Pos(RLevel1),Pos);
            else if (EL_Pos(RAttachments) != Pos)
                Result |= OutputError(0x6D,T("The SeekPoint at %") TPRId64 T(" references a Attachments at wrong position %") TPRId64 T(" (real %") TPRId64 T(")"),EL_Pos(RLevel1),Pos,EL_Pos(RAttachments));
            BAttachments = 1;
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextSeekHead()))
        {
            if (EL_Pos(SeekHead) == Pos)
                Result |= OutputError(0x6E,T("The SeekPoint at %") TPRId64 T(" references references its own SeekHead"),EL_Pos(RLevel1));
            else if (SeekHead == RSeekHead)
            {
                if (!RSeekHead2)
                    Result |= OutputError(0x6F,T("The SeekPoint at %") TPRId64 T(" references an unknown secondary SeekHead at %") TPRId64,EL_Pos(RLevel1),Pos);
                BSecondSeek = 1;
            }
            else if (SeekHead == RSeekHead2 && Pos!=EL_Pos(RSeekHead))
                Result |= OutputError(0x70,T("The SeekPoint at %") TPRId64 T(" references an unknown extra SeekHead at %") TPRId64,EL_Pos(RLevel1),Pos);
        }
        else if (MATROSKA_MetaSeekIsClass(RLevel1, MATROSKA_getContextCluster()))
        {
            ebml_element **Cluster;
            for (Cluster = ARRAYBEGIN(RClusters,ebml_element*);Cluster != ARRAYEND(RClusters,ebml_element*); ++Cluster)
            {
                if (EL_Pos(*Cluster) == Pos)
                    break;
            }
            if (Cluster == ARRAYEND(RClusters,ebml_element*) && Cluster != ARRAYBEGIN(RClusters,ebml_element*))
                Result |= OutputError(0x71,T("The SeekPoint at %") TPRId64 T(" references a Cluster not found at %") TPRId64,EL_Pos(RLevel1),Pos);
        }
        else
            OutputWarning(0x860,T("The SeekPoint at %") TPRId64 T(" references an element that is not a known level 1 ID %s at %") TPRId64 T(")"),EL_Pos(RLevel1),IdString,Pos);
        RLevel1 = (matroska_seekpoint*)EBML_MasterNextChild(SeekHead, RLevel1);
    }
    if (SeekHead == RSeekHead)
    {
        if (!BSegmentInfo && RSegmentInfo)
            OutputWarning(0x861,T("The SegmentInfo is not referenced in the main SeekHead"));
        if (!BTrackInfo && RTrackInfo)
            OutputWarning(0x861,T("The TrackInfo is not referenced in the main SeekHead"));
        if (!BCues && RCues)
            OutputWarning(0x861,T("The Cues is not referenced in the main SeekHead"));
        if (!BTags && RTags)
            OutputWarning(0x861,T("The Tags is not referenced in the main SeekHead"));
        if (!BChapters && RChapters)
            OutputWarning(0x861,T("The Chapters is not referenced in the main SeekHead"));
        if (!BAttachments && RAttachments)
            OutputWarning(0x861,T("The Attachments is not referenced in the main SeekHead"));
        if (!BSecondSeek && RSeekHead2)
            OutputWarning(0x861,T("The secondary SeekHead is not referenced in the main SeekHead"));
    }
    return Result;
}

static void LinkClusterBlocks(int ProfileNum)
{
    matroska_cluster **Cluster;
    for (Cluster=ARRAYBEGIN(RClusters,matroska_cluster*);Cluster!=ARRAYEND(RClusters,matroska_cluster*);++Cluster)
        MATROSKA_LinkClusterBlocks(*Cluster, RSegmentInfo, RTrackInfo, 1, ProfileNum);
}

static bool_t TrackIsLaced(int16_t TrackNum, int ProfileNum)
{
    ebml_element *TrackData;
    ebml_master *Track = (ebml_master*)EBML_MasterFindChild(RTrackInfo, MATROSKA_getContextTrackEntry());
    while (Track)
    {
        TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextTrackNumber(), ProfileNum);
        if (EL_Int(TrackData) == TrackNum)
        {
            TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextFlagLacing(), ProfileNum);
            return EL_Int(TrackData) != 0;
        }
        Track = (ebml_master*)EBML_MasterNextChild(RTrackInfo, Track);
    }
    return 1;
}

static bool_t TrackIsVideo(int16_t TrackNum, int ProfileNum)
{
    ebml_element *TrackData;
    ebml_master *Track = (ebml_master*)EBML_MasterFindChild(RTrackInfo, MATROSKA_getContextTrackEntry());
    while (Track)
    {
        TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextTrackNumber(), ProfileNum);
        if (EL_Int(TrackData) == TrackNum)
        {
            TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextTrackType(), ProfileNum);
            return EL_Int(TrackData) == MATROSKA_TRACK_TYPE_VIDEO;
        }
        // look for TrackNum in the next Track
        Track = (ebml_master*)EBML_MasterNextChild(RTrackInfo, Track);
    }
    return 0;
}

static bool_t TrackNeedsKeyframe(int16_t TrackNum, int ProfileNum)
{
    ebml_element *TrackData;
    ebml_master *Track = (ebml_master*)EBML_MasterFindChild(RTrackInfo, MATROSKA_getContextTrackEntry());
    while (Track)
    {
        TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextTrackNumber(), ProfileNum);
        if (EL_Int(TrackData) == TrackNum)
        {
            TrackData = EBML_MasterGetChild(Track, MATROSKA_getContextTrackType(), ProfileNum);
            switch (EL_Int(TrackData))
            {
            case MATROSKA_TRACK_TYPE_VIDEO:
                return 0;
            case MATROSKA_TRACK_TYPE_AUDIO:
                {
                    tchar_t CodecName[MAXDATA];
                    ebml_string *CodecID = (ebml_string*)EBML_MasterGetChild(Track, MATROSKA_getContextCodecID(), ProfileNum);
                    EBML_StringGet(CodecID,CodecName,TSIZEOF(CodecName));
                    return !tcsisame_ascii(CodecName,T("A_TRUEHD"));
                }
            default: return 1;
            }
        }
        // look for TrackNum in the next Track
        Track = (ebml_master*)EBML_MasterNextChild(RTrackInfo, Track);
    }
    return 0;
}

static int CheckVideoStart(int ProfileNum)
{
    int Result = 0;
    ebml_master **Cluster;
    ebml_element *Block, *GBlock;
    uint16_t BlockNum;
    mkv_timestamp_t ClusterTimestamp;
    array TrackKeyframe;
    array TrackFirstKeyframePos;

    for (Cluster=ARRAYBEGIN(RClusters,ebml_master*);Cluster!=ARRAYEND(RClusters,ebml_master*);++Cluster)
    {
        ArrayInit(&TrackKeyframe);
        ArrayResize(&TrackKeyframe,sizeof(bool_t)*(TrackMax+1),256);
        ArrayZero(&TrackKeyframe);
        ArrayInit(&TrackFirstKeyframePos);
        ArrayResize(&TrackFirstKeyframePos,sizeof(filepos_t)*(TrackMax+1),256);
        ArrayZero(&TrackFirstKeyframePos);

        ClusterTimestamp = MATROSKA_ClusterTimestamp((matroska_cluster*)*Cluster);
        if (ClusterTimestamp==INVALID_TIMESTAMP_T)
            Result |= OutputError(0xC1,T("The Cluster at %") TPRId64 T(" has no timestamp"),EL_Pos(*Cluster));
        else if (ClusterTime!=INVALID_TIMESTAMP_T && ClusterTime >= ClusterTimestamp)
            OutputWarning(0xC2,T("The timestamp of the Cluster at %") TPRId64 T(" is not incrementing (may be intentional)"),EL_Pos(*Cluster));
        ClusterTime = ClusterTimestamp;

        for (Block = EBML_MasterChildren(*Cluster);Block;Block=EBML_MasterNext(Block))
        {
            if (EL_Type(Block, MATROSKA_getContextBlockGroup()))
            {
                for (GBlock = EBML_MasterChildren(Block);GBlock;GBlock=EBML_MasterNext(GBlock))
                {
                    if (EL_Type(GBlock, MATROSKA_getContextBlock()))
                    {
                        BlockNum = MATROSKA_BlockTrackNum((matroska_block*)GBlock);
                        if (BlockNum > ARRAYCOUNT(TrackKeyframe,bool_t))
                            OutputError(0xC3,T("Unknown track #%d in Cluster at %") TPRId64 T(" in Block at %") TPRId64,(int)BlockNum,EL_Pos(*Cluster),EL_Pos(GBlock));
                        else if (TrackIsVideo(BlockNum, ProfileNum))
                        {
                            if (!ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] && MATROSKA_BlockKeyframe((matroska_block*)GBlock))
                                ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] = 1;
                            if (!ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] && ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum]==0)
                                ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum] = EL_Pos(*Cluster);
                        }
                        break;
                    }
                }
            }
            else if (EL_Type(Block, MATROSKA_getContextSimpleBlock()))
            {
                BlockNum = MATROSKA_BlockTrackNum((matroska_block*)Block);
                if (BlockNum > ARRAYCOUNT(TrackKeyframe,bool_t))
                    OutputError(0xC3,T("Unknown track #%d in Cluster at %") TPRId64 T(" in SimpleBlock at %") TPRId64,(int)BlockNum,EL_Pos(*Cluster),EL_Pos(Block));
                else if (TrackIsVideo(BlockNum, ProfileNum))
                {
                    if (!ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] && MATROSKA_BlockKeyframe((matroska_block*)Block))
                        ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] = 1;
                    if (!ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] && ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum]==0)
                        ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum] = EL_Pos(*Cluster);
                }
            }
        }
        for (BlockNum=0;BlockNum<ARRAYCOUNT(TrackKeyframe,bool_t);++BlockNum)
        {
            if (ARRAYBEGIN(TrackKeyframe,bool_t)[BlockNum] && ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum]!=0)
                OutputWarning(0xC0,T("First Block for video track #%d in Cluster at %") TPRId64 T(" is not a keyframe"),(int)BlockNum,ARRAYBEGIN(TrackFirstKeyframePos,filepos_t)[BlockNum]);
        }
        ArrayClear(&TrackKeyframe);
        ArrayClear(&TrackFirstKeyframePos);
    }
    return Result;
}

static int CheckPosSize(const ebml_element *RSegment)
{
    int Result = 0;
    ebml_element **Cluster,*PrevCluster=NULL;
    ebml_element *Elt;

    for (Cluster=ARRAYBEGIN(RClusters,ebml_element*);Cluster!=ARRAYEND(RClusters,ebml_element*);++Cluster)
    {
        Elt = EBML_MasterFindChild((ebml_master*)*Cluster,MATROSKA_getContextPrevSize());
        if (Elt)
        {
            if (PrevCluster==NULL)
                Result |= OutputError(0xA0,T("The PrevSize %") TPRId64 T(" was set on the first Cluster at %") TPRId64,EL_Int(Elt),EL_Pos(Elt));
            else if (EL_Int(Elt) != EL_Pos(*Cluster) - EL_Pos(PrevCluster))
                Result |= OutputError(0xA1,T("The Cluster PrevSize %") TPRId64 T(" at %") TPRId64 T(" should be %") TPRId64,EL_Int(Elt),EL_Pos(Elt),EL_Pos(*Cluster) - EL_Pos(PrevCluster));
        }
        Elt = EBML_MasterFindChild((ebml_master*)*Cluster,MATROSKA_getContextPosition());
        if (Elt)
        {
            if (EL_Int(Elt) != EL_Pos(*Cluster) - EBML_ElementPositionData(RSegment))
                Result |= OutputError(0xA2,T("The Cluster position %") TPRId64 T(" at %") TPRId64 T(" should be %") TPRId64,EL_Int(Elt),EL_Pos(Elt),EL_Pos(*Cluster) - EBML_ElementPositionData(RSegment));
        }
        PrevCluster = *Cluster;
    }
    return Result;
}

static int CheckLacingKeyframe(int ProfileNum)
{
    int Result = 0;
    matroska_cluster **Cluster;
    ebml_element *Block, *GBlock;
    int16_t BlockNum;
    mkv_timestamp_t BlockTime;
    size_t Frame,TrackIdx;

    for (Cluster=ARRAYBEGIN(RClusters,matroska_cluster*);Cluster!=ARRAYEND(RClusters,matroska_cluster*);++Cluster)
    {
        for (Block = EBML_MasterChildren(*Cluster);Block;Block=EBML_MasterNext(Block))
        {
            if (EL_Type(Block, MATROSKA_getContextBlockGroup()))
            {
                for (GBlock = EBML_MasterChildren(Block);GBlock;GBlock=EBML_MasterNext(GBlock))
                {
                    if (EL_Type(GBlock, MATROSKA_getContextBlock()))
                    {
                        //MATROSKA_ContextFlagLacing
                        BlockNum = MATROSKA_BlockTrackNum((matroska_block*)GBlock);
                        for (TrackIdx=0; TrackIdx<ARRAYCOUNT(Tracks,track_info); ++TrackIdx)
                            if (ARRAYBEGIN(Tracks,track_info)[TrackIdx].Num == BlockNum)
                                break;

                        if (TrackIdx==ARRAYCOUNT(Tracks,track_info))
                            Result |= OutputError(0xB2,T("Block at %") TPRId64 T(" is using an unknown track #%d"),EL_Pos(GBlock),(int)BlockNum);
                        else
                        {
                            if (MATROSKA_BlockLaced((matroska_block*)GBlock) && !TrackIsLaced(BlockNum, ProfileNum))
                                Result |= OutputError(0xB0,T("Block at %") TPRId64 T(" track #%d is laced but the track is not"),EL_Pos(GBlock),(int)BlockNum);
                            if (!MATROSKA_BlockKeyframe((matroska_block*)GBlock) && TrackNeedsKeyframe(BlockNum, ProfileNum))
                                Result |= OutputError(0xB1,T("Block at %") TPRId64 T(" track #%d is not a keyframe"),EL_Pos(GBlock),(int)BlockNum);

                            for (Frame=0; Frame<MATROSKA_BlockGetFrameCount((matroska_block*)GBlock); ++Frame)
                                ARRAYBEGIN(Tracks,track_info)[TrackIdx].DataLength += MATROSKA_BlockGetLength((matroska_block*)GBlock,Frame);
                            if (Details)
                            {
                                BlockTime = MATROSKA_BlockTimestamp((matroska_block*)GBlock);
                                if (MinTime==INVALID_TIMESTAMP_T || MinTime>BlockTime)
                                    MinTime = BlockTime;
                                if (MaxTime==INVALID_TIMESTAMP_T || MaxTime<BlockTime)
                                    MaxTime = BlockTime;
                            }
                        }
                        break;
                    }
                }
            }
            else if (EL_Type(Block, MATROSKA_getContextSimpleBlock()))
            {
                BlockNum = MATROSKA_BlockTrackNum((matroska_block*)Block);
                for (TrackIdx=0; TrackIdx<ARRAYCOUNT(Tracks,track_info); ++TrackIdx)
                    if (ARRAYBEGIN(Tracks,track_info)[TrackIdx].Num == BlockNum)
                        break;

                if (TrackIdx==ARRAYCOUNT(Tracks,track_info))
                    Result |= OutputError(0xB2,T("Block at %") TPRId64 T(" is using an unknown track #%d"),EL_Pos(Block),(int)BlockNum);
                else
                {
                    if (MATROSKA_BlockLaced((matroska_block*)Block) && !TrackIsLaced(BlockNum, ProfileNum))
                        Result |= OutputError(0xB0,T("SimpleBlock at %") TPRId64 T(" track #%d is laced but the track is not"),EL_Pos(Block),(int)BlockNum);
                    if (!MATROSKA_BlockKeyframe((matroska_block*)Block) && TrackNeedsKeyframe(BlockNum, ProfileNum))
                        Result |= OutputError(0xB1,T("SimpleBlock at %") TPRId64 T(" track #%d is not a keyframe"),EL_Pos(Block),(int)BlockNum);
                    for (Frame=0; Frame<MATROSKA_BlockGetFrameCount((matroska_block*)Block); ++Frame)
                        ARRAYBEGIN(Tracks,track_info)[TrackIdx].DataLength += MATROSKA_BlockGetLength((matroska_block*)Block,Frame);
                    if (Details)
                    {
                        BlockTime = MATROSKA_BlockTimestamp((matroska_block*)Block);
                        if (MinTime==INVALID_TIMESTAMP_T || MinTime>BlockTime)
                            MinTime = BlockTime;
                        if (MaxTime==INVALID_TIMESTAMP_T || MaxTime<BlockTime)
                            MaxTime = BlockTime;
                    }
                }
            }
        }
    }
    return Result;
}

static int CheckCueEntries(ebml_master *Cues)
{
    int Result = 0;
    mkv_timestamp_t TimestampEntry, PrevTimestamp = INVALID_TIMESTAMP_T;
    uint16_t TrackNumEntry;
    matroska_cluster **Cluster;
    matroska_block *Block;
    int ClustNum = 0;

    if (!RSegmentInfo)
        Result |= OutputError(0x310,T("A Cues (index) is defined but no SegmentInfo was found"));
    else if (ARRAYCOUNT(RClusters,matroska_cluster*))
    {
        matroska_cuepoint *CuePoint = (matroska_cuepoint*)EBML_MasterFindChild(Cues, MATROSKA_getContextCuePoint());
        while (CuePoint)
        {
            if (!Quiet && ClustNum++ % 24 == 0)
                TextWrite(StdErr,T("."));
            MATROSKA_LinkCueSegmentInfo(CuePoint,RSegmentInfo);
            TimestampEntry = MATROSKA_CueTimestamp(CuePoint);
            TrackNumEntry = MATROSKA_CueTrackNum(CuePoint);

            if (TimestampEntry < PrevTimestamp && PrevTimestamp != INVALID_TIMESTAMP_T)
                OutputWarning(0x311,T("The Cues entry for timestamp %") TPRId64 T(" ms is listed after entry %") TPRId64 T(" ms"),Scale64(TimestampEntry,1,1000000),Scale64(PrevTimestamp,1,1000000));

            // find a matching Block
            for (Cluster = ARRAYBEGIN(RClusters,matroska_cluster*);Cluster != ARRAYEND(RClusters,matroska_cluster*); ++Cluster)
            {
                Block = MATROSKA_GetBlockForTimestamp(*Cluster, TimestampEntry, TrackNumEntry);
                if (Block)
                    break;
            }
            if (Cluster == ARRAYEND(RClusters,matroska_cluster*))
                Result |= OutputError(0x312,T("CueEntry Track #%d and timestamp %") TPRId64 T(" ms not found"),(int)TrackNumEntry,Scale64(TimestampEntry,1,1000000));
            PrevTimestamp = TimestampEntry;
            CuePoint = (matroska_cuepoint*)EBML_MasterNextChild(Cues, CuePoint);
        }
    }
    return Result;
}

#if defined(TARGET_WIN) && defined(UNICODE)
int wmain(int argc, const wchar_t *argv[])
#else
int main(int argc, const char *argv[])
#endif
{
    int Result = 0;
    int ShowUsage = 0;
    int ShowVersion = 0;
    parsercontext p;
    textwriter _StdErr;
    struct stream *Input = NULL;
    tchar_t Path[MAXPATHFULL];
    tchar_t String[MAXLINE];
    ebml_master *EbmlHead = NULL, *RSegment = NULL, *RLevel1 = NULL, *Prev, *RLevelX, **Cluster;
    ebml_element *EbmlDocVer, *EbmlReadDocVer;
    ebml_string *LibName, *AppName;
    ebml_parser_context RContext;
    ebml_parser_context RSegmentContext;
    int i,UpperElement;
    int MatroskaProfile = 0;
    bool_t HasVideo = 0;
    int DotCount;
    track_info *TI;
    filepos_t VoidAmount = 0;

    // Core-C init phase
    ParserContext_Init(&p,NULL,NULL,NULL);
    Node_SetData(&p.Base.Base.Base,NODECONTEXT_PROJECT_VENDOR,TYPE_STRING,"Matroska");
    Node_SetData(&p.Base.Base.Base,NODECONTEXT_PROJECT_VERSION,TYPE_STRING,PROJECT_VERSION);
    Node_SetData(&p.Base.Base.Base,NODECONTEXT_PROJECT_NAME,TYPE_STRING,PROJECT_NAME);

    // EBML & Matroska Init
    MATROSKA_Init(&p);

    ArrayInit(&RClusters);
    ArrayInit(&Tracks);

    StdErr = &_StdErr;
    memset(StdErr,0,sizeof(_StdErr));
    StdErr->Stream = (struct stream*)NodeSingleton(&p,STDERR_ID);
    assert(StdErr->Stream!=NULL);

    for (i=1;i<argc;++i)
    {
#if defined(TARGET_WIN) && defined(UNICODE)
        Node_FromWcs(&p,Path,TSIZEOF(Path),argv[i]);
#else
        Node_FromStr(&p,Path,TSIZEOF(Path),argv[i]);
#endif
        if (tcsisame_ascii(Path,T("--no-warn"))) Warnings = 0;
        else if (tcsisame_ascii(Path,T("--live"))) Live = 1;
        else if (tcsisame_ascii(Path,T("--details"))) Details = 1;
        else if (tcsisame_ascii(Path,T("--divx"))) DivX = 1;
        else if (tcsisame_ascii(Path,T("--version"))) ShowVersion = 1;
        else if (tcsisame_ascii(Path,T("--quiet"))) Quiet = 1;
        else if (tcsisame_ascii(Path,T("--quick"))) QuickExit = 1;
        else if (tcsisame_ascii(Path,T("--help"))) {ShowVersion = 1; ShowUsage = 1;}
        else if (i<argc-1) TextPrintf(StdErr,T("Unknown parameter '%s'\r\n"),Path);
    }

    if (argc < 2 || ShowVersion)
    {
        TextWrite(StdErr,PROJECT_NAME T(" v") PROJECT_VERSION T(", Copyright (c) 2010-2020 Matroska Foundation\r\n"));
        if (argc < 2 || ShowUsage)
        {
            Result = OutputError(1,T("Usage: ") PROJECT_NAME T(" [options] <matroska_src>"));
            TextWrite(StdErr,T("Options:\r\n"));
            TextWrite(StdErr,T("  --no-warn   only output errors, no warnings\r\n"));
            TextWrite(StdErr,T("  --live      only output errors/warnings relevant to live streams\r\n"));
            TextWrite(StdErr,T("  --details   show details for valid files\r\n"));
            TextWrite(StdErr,T("  --divx      assume the file is using DivX specific extensions\r\n"));
            TextWrite(StdErr,T("  --quick     exit after the first error or warning\r\n"));
            TextWrite(StdErr,T("  --quiet     don't ouput progress and file info\r\n"));
            TextWrite(StdErr,T("  --version   show the version of ") PROJECT_NAME T("\r\n"));
            TextWrite(StdErr,T("  --help      show this screen\r\n"));
        }
        goto exit;
    }

#if defined(TARGET_WIN) && defined(UNICODE)
    Node_FromWcs(&p,Path,TSIZEOF(Path),argv[argc-1]);
#else
    Node_FromStr(&p,Path,TSIZEOF(Path),argv[argc-1]);
#endif
    Input = StreamOpen(&p,Path,SFLAG_RDONLY/*|SFLAG_BUFFERED*/);
    if (!Input)
    {
        TextPrintf(StdErr,T("Could not open file \"%s\" for reading\r\n"),Path);
        Result = -2;
        goto exit;
    }

    // parse the source file to determine if it's a Matroska file and determine the location of the key parts
    RContext.Context = MATROSKA_getContextStream();
    RContext.EndPosition = INVALID_FILEPOS_T;
    RContext.UpContext = NULL;
    RContext.Profile = EBML_ANY_PROFILE;
    EbmlHead = (ebml_master*)EBML_FindNextElement(Input, &RContext, &UpperElement, 0);
    if (!EbmlHead || !EL_Type(EbmlHead, EBML_getContextHead()))
    {
        Result = OutputError(3,T("EBML head not found! Are you sure it's a matroska/webm file?"));
        goto exit;
    }

    if (!Quiet) TextWrite(StdErr,T("."));

    if (EBML_ElementReadData(EbmlHead,Input,&RContext,0,SCOPE_ALL_DATA, 1)!=ERR_NONE)
    {
        Result = OutputError(4,T("Could not read the EBML head"));
        goto exit;
    }
    if (!EBML_MasterIsChecksumValid(EbmlHead))
    {
        Result = OutputError(12,T("The EBML header is damaged (invalid CheckSum)"));
        goto exit;
    }

    VoidAmount += CheckUnknownElements((ebml_element*)EbmlHead);

    RLevel1 = (ebml_master*)EBML_MasterGetChild(EbmlHead,EBML_getContextReadVersion(), EBML_ANY_PROFILE);
    if (EL_Int(RLevel1) > EBML_MAX_VERSION)
        OutputError(5,T("The EBML read version is not supported: %d"),(int)EL_Int(RLevel1));

    RLevel1 = (ebml_master*)EBML_MasterGetChild(EbmlHead,EBML_getContextMaxIdLength(), EBML_ANY_PROFILE);
    if (EL_Int(RLevel1) > EBML_MAX_ID)
        OutputError(6,T("The EBML max ID length is not supported: %d"),(int)EL_Int(RLevel1));

    RLevel1 = (ebml_master*)EBML_MasterGetChild(EbmlHead,EBML_getContextMaxSizeLength(), EBML_ANY_PROFILE);
    if (EL_Int(RLevel1) > EBML_MAX_SIZE)
        OutputError(7,T("The EBML max size length is not supported: %d"),(int)EL_Int(RLevel1));

    RLevel1 = (ebml_master*)EBML_MasterGetChild(EbmlHead,EBML_getContextDocType(), EBML_ANY_PROFILE);
    EBML_StringGet((ebml_string*)RLevel1,String,TSIZEOF(String));
    if (tcscmp(String,T("matroska"))!=0 && tcscmp(String,T("webm"))!=0)
    {
        Result = OutputError(8,T("The EBML doctype is not supported: %s"),String);
        goto exit;
    }

    EbmlDocVer = EBML_MasterGetChild(EbmlHead,EBML_getContextDocTypeVersion(), EBML_ANY_PROFILE);
    EbmlReadDocVer = EBML_MasterGetChild(EbmlHead,EBML_getContextDocTypeReadVersion(), EBML_ANY_PROFILE);

    if (EL_Int(EbmlReadDocVer) > EL_Int(EbmlDocVer))
        OutputError(9,T("The DocType version %d is higher than the read Doctype version %d"),(int)EL_Int(EbmlDocVer),(int)EL_Int(EbmlReadDocVer));

    if (tcscmp(String,T("matroska"))==0)
    {
        if (DivX)
            MatroskaProfile = PROFILE_DIVX;
        else if (EL_Int(EbmlDocVer)==5)
            MatroskaProfile = PROFILE_MATROSKA_V5;
        else if (EL_Int(EbmlDocVer)==4)
            MatroskaProfile = PROFILE_MATROSKA_V4;
        else if (EL_Int(EbmlDocVer)==3)
            MatroskaProfile = PROFILE_MATROSKA_V3;
        else if (EL_Int(EbmlDocVer)==2)
            MatroskaProfile = PROFILE_MATROSKA_V2;
        else if (EL_Int(EbmlDocVer)==1)
            MatroskaProfile = PROFILE_MATROSKA_V1;
        else
            OutputError(10,T("Unknown Matroska profile %d/%d"),(int)EL_Int(EbmlDocVer),(int)EL_Int(EbmlReadDocVer));
    }
    else if (tcscmp(String,T("webm"))==0)
        MatroskaProfile = PROFILE_WEBM;

    if (!Quiet) TextWrite(StdErr,T("."));

    // find the segment
    RSegment = (ebml_master*)EBML_FindNextElement(Input, &RContext, &UpperElement, 1);
    if (RSegment == NULL)
    {
        Result = OutputError(0x20, T("No Segment found"));
        goto exit;
    }
    RSegmentContext.Context = MATROSKA_getContextSegment();
    RSegmentContext.EndPosition = EBML_ElementPositionEnd((ebml_element*)RSegment);
    RSegmentContext.UpContext = &RContext;
    RSegmentContext.Profile = MatroskaProfile;

    RContext.EndPosition = EBML_ElementPositionEnd((ebml_element*)RSegment);

    UpperElement = 0;
    DotCount = 0;
    Prev = NULL;
    RLevel1 = (ebml_master*)EBML_FindNextElement(Input, &RSegmentContext, &UpperElement, 1);
    while (RLevel1)
    {
        RLevelX = NULL;
        if (EL_Type(RLevel1, MATROSKA_getContextCluster()))
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,0,SCOPE_PARTIAL_DATA,4)==ERR_NONE)
            {
                ArrayAppend(&RClusters,&RLevel1,sizeof(RLevel1),256);
                NodeTree_SetParent(RLevel1, RSegment, NULL);
                VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
                RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
            }
            else
            {
                Result = OutputError(0x180,T("Failed to read the Cluster at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextSeekHead()))
        {
            if (Live)
            {
                OutputWarning(0x170,T("The live stream has a SeekHead at %") TPRId64,EL_Pos(RLevel1));
                RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
                NodeDelete((node*)RLevel1);
                RLevel1 = NULL;
            }
            else if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,2)==ERR_NONE)
            {
                if (!RSeekHead)
                    RSeekHead = RLevel1;
                else if (!RSeekHead2)
                {
                    OutputWarning(0x103,T("Unnecessary secondary SeekHead was found at %") TPRId64,EL_Pos(RLevel1));
                    RSeekHead2 = RLevel1;
                }
                else
                    OutputWarning(0x101,T("Extra SeekHead found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                NodeTree_SetParent(RLevel1, RSegment, NULL);
                VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
            }
            else
            {
                Result = OutputError(0x100,T("Failed to read the SeekHead at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextInfo()))
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,1)==ERR_NONE)
            {
                if (RSegmentInfo != NULL)
                    OutputWarning(0x110,T("Extra SegmentInfo found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    RSegmentInfo = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);

                    if (Live)
                    {
                        ebml_master *Elt = (ebml_master*)EBML_MasterFindChild(RLevel1,MATROSKA_getContextDuration());
                        if (Elt)
                            OutputWarning(0x112,T("The live Segment has a duration set at %") TPRId64,EL_Pos(Elt));
                    }
                }
            }
            else
            {
                Result = OutputError(0x111,T("Failed to read the SegmentInfo at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextTracks()))
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,4)==ERR_NONE)
            {
                if (RTrackInfo != NULL)
                    OutputWarning(0x120,T("Extra TrackInfo found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    size_t TrackCount;
                    ebml_master *Elt;

                    RTrackInfo = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);

                    Elt = (ebml_master*)EBML_MasterFindChild(RTrackInfo,MATROSKA_getContextTrackEntry());
                    TrackCount = 0;
                    while (Elt)
                    {
                        Elt = (ebml_master*)EBML_MasterNextChild(RTrackInfo,Elt);
                        ++TrackCount;
                    }

                    ArrayResize(&Tracks,TrackCount*sizeof(track_info),256);
                    ArrayZero(&Tracks);

                    Elt = (ebml_master*)EBML_MasterFindChild(RTrackInfo,MATROSKA_getContextTrackEntry());
                    TrackCount = 0;
                    while (Elt)
                    {
                        EbmlDocVer = EBML_MasterFindChild(Elt,MATROSKA_getContextTrackNumber());
                        assert(EbmlDocVer!=NULL);
                        if (EbmlDocVer)
                        {
                            TrackMax = MAX(TrackMax,(size_t)EL_Int(EbmlDocVer));
                            ARRAYBEGIN(Tracks,track_info)[TrackCount].Num = (int)EL_Int(EbmlDocVer);
                        }
                        EbmlDocVer = EBML_MasterFindChild(Elt,MATROSKA_getContextTrackType());
                        assert(EbmlDocVer!=NULL);
                        if (EbmlDocVer)
                        {
                            if (EL_Int(EbmlDocVer)==MATROSKA_TRACK_TYPE_VIDEO)
                            {
                                Result |= CheckVideoTrack(Elt, ARRAYBEGIN(Tracks,track_info)[TrackCount].Num, MatroskaProfile);
                                HasVideo = 1;
                            }
                            ARRAYBEGIN(Tracks,track_info)[TrackCount].Kind = (int)EL_Int(EbmlDocVer);
                        }
                        ARRAYBEGIN(Tracks,track_info)[TrackCount].CodecID = (ebml_string*)EBML_MasterFindChild(Elt,MATROSKA_getContextCodecID());
                        Elt = (ebml_master*)EBML_MasterNextChild(RTrackInfo,Elt);
                        ++TrackCount;
                    }
                    EbmlDocVer = NULL;
                    Elt = NULL;
                }
            }
            else
            {
                Result = OutputError(0x121,T("Failed to read the TrackInfo at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextCues()))
        {
            if (Live)
            {
                OutputWarning(0x171,T("The live stream has Cues at %") TPRId64,EL_Pos(RLevel1));
                RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
                NodeDelete((node*)RLevel1);
                RLevel1 = NULL;
            }
            else if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,3)==ERR_NONE)
            {
                if (RCues != NULL)
                    OutputWarning(0x130,T("Extra Cues found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    RCues = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
                }
            }
            else
            {
                Result = OutputError(0x131,T("Failed to read the Cues at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextChapters()))
        {
            if (Live)
            {
                Result |= OutputError(0x172,T("The live stream has Chapters at %") TPRId64,EL_Pos(RLevel1));
                RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
                NodeDelete((node*)RLevel1);
                RLevel1 = NULL;
            }
            else if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,16)==ERR_NONE)
            {
                if (RChapters != NULL)
                    OutputWarning(0x140,T("Extra Chapters found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    RChapters = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
                }
            }
            else
            {
                Result = OutputError(0x141,T("Failed to read the Chapters at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextTags()))
        {
            if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,4)==ERR_NONE)
            {
                if (RTags != NULL)
                    Result |= OutputError(0x150,T("Extra Tags found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    RTags = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
                }
            }
            else
            {
                Result = OutputError(0x151,T("Failed to read the Tags at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else if (EL_Type(RLevel1, MATROSKA_getContextAttachments()))
        {
            if (Live)
            {
                Result |= OutputError(0x173,T("The live stream has a Attachments at %") TPRId64,EL_Pos(RLevel1));
                RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
                NodeDelete((node*)RLevel1);
                RLevel1 = NULL;
            }
            else if (EBML_ElementReadData(RLevel1,Input,&RSegmentContext,1,SCOPE_ALL_DATA,3)==ERR_NONE)
            {
                if (RAttachments != NULL)
                    Result |= OutputError(0x160,T("Extra Attachments found at %") TPRId64 T(" (size %") TPRId64 T(")"),EL_Pos(RLevel1),EL_DataSize(RLevel1));
                else
                {
                    RAttachments = RLevel1;
                    NodeTree_SetParent(RLevel1, RSegment, NULL);
                    VoidAmount += CheckUnknownElements((ebml_element*)RLevel1);
                    Result |= CheckProfileViolation((ebml_element*)RLevel1, MatroskaProfile);
                }
            }
            else
            {
                Result = OutputError(0x161,T("Failed to read the Attachments at %") TPRId64 T(" size %") TPRId64,EL_Pos(RLevel1),EL_DataSize(RLevel1));
                goto exit;
            }
        }
        else
        {
            if (Node_IsPartOf(RLevel1,EBML_DUMMY_ID))
            {
                tchar_t Id[32];
                EBML_IdToString(Id,TSIZEOF(Id),EBML_ElementClassID((ebml_element*)RLevel1));
                OutputWarning(0x80,T("Unknown element %s at %") TPRId64 T(" size %") TPRId64,Id,EL_Pos(RLevel1),EL_DataSize(RLevel1));
            }
            if (Node_IsPartOf(RLevel1,EBML_VOID_CLASS))
            {
                VoidAmount += EBML_ElementFullSize((ebml_element*)RLevel1,0);
            }
            RLevelX = (ebml_master*)EBML_ElementSkipData((ebml_element*)RLevel1, Input, &RSegmentContext, NULL, 1);
            NodeDelete((node*)RLevel1);
            RLevel1 = NULL;
        }
        if (!Quiet) {
            TextWrite(StdErr,T(".")); ++DotCount;
            if (!(DotCount % 60))
                TextWrite(StdErr,T("\r                                                              \r"));
        }

        Prev = RLevel1;
        if (RLevelX)
            RLevel1 = RLevelX;
        else
            RLevel1 = (ebml_master*)EBML_FindNextElement(Input, &RSegmentContext, &UpperElement, 1);
    }

    if (!RSegmentInfo)
    {
        Result = OutputError(0x40,T("The segment is missing a SegmentInfo"));
        goto exit;
    }

    if (Prev)
    {
        if (EBML_ElementPositionEnd((ebml_element*)RSegment)!=INVALID_FILEPOS_T && EBML_ElementPositionEnd((ebml_element*)RSegment)!=EBML_ElementPositionEnd((ebml_element*)Prev))
            Result |= OutputError(0x42,T("The segment's size %") TPRId64 T(" doesn't match the position where it ends %") TPRId64,EBML_ElementPositionEnd((ebml_element*)RSegment),EBML_ElementPositionEnd((ebml_element*)Prev));
    }

    if (!RSeekHead)
    {
        if (!Live)
            OutputWarning(0x801,T("The segment has no SeekHead section"));
    }
    else
        Result |= CheckSeekHead(RSeekHead);
    if (RSeekHead2)
        Result |= CheckSeekHead(RSeekHead2);

    if (ARRAYCOUNT(RClusters,ebml_element*))
    {
        if (!Quiet) TextWrite(StdErr,T("."));
        LinkClusterBlocks(MatroskaProfile);

        if (HasVideo)
            Result |= CheckVideoStart(MatroskaProfile);
        Result |= CheckLacingKeyframe(MatroskaProfile);
        Result |= CheckPosSize((ebml_element*)RSegment);
        if (!RCues)
        {
            if (!Live && ARRAYCOUNT(RClusters,ebml_element*)>1)
                OutputWarning(0x800,T("The segment has Clusters but no Cues section (bad for seeking)"));
        }
        else
            Result |= CheckCueEntries(RCues);
        if (!RTrackInfo)
        {
            Result = OutputError(0x41,T("The segment has Clusters but no TrackInfo section"));
            goto exit;
        }
    }

    if (!Quiet) TextWrite(StdErr,T("."));
    if (RTrackInfo)
        Result |= CheckTracks(RTrackInfo, MatroskaProfile);

    for (TI=ARRAYBEGIN(Tracks,track_info); TI!=ARRAYEND(Tracks,track_info); ++TI)
    {
        if (TI->DataLength==0)
            OutputWarning(0xB8,T("Track #%d is defined but has no frame"),TI->Num);
    }

    if (VoidAmount > 4*1024)
        OutputWarning(0xD0,T("There are %") TPRId64 T(" bytes of void data\r\n"),VoidAmount);

    if (!Quiet && Result==0)
    {
        TextWrite(StdErr,T("\r") PROJECT_NAME T(" ") PROJECT_VERSION T(": the file appears to be valid\r\n"));
        if (Details)
        {
            for (TI=ARRAYBEGIN(Tracks,track_info); TI!=ARRAYEND(Tracks,track_info); ++TI)
            {
                EBML_StringGet(TI->CodecID,String,TSIZEOF(String));
                TextPrintf(StdErr,T("Track #%d %18s %") TPRId64 T(" bits/s\r\n"),TI->Num,String,Scale64(TI->DataLength,8000000, (MaxTime-MinTime)/1000));
            }
        }
    }

exit:
    if (!Quiet)
    {
        TextPrintf(StdErr, T("\r\tfile \"%s\"\r\n"), Path);
        if (RSegmentInfo)
        {
            tchar_t App[MAXPATH];
            App[0] = 0;
            LibName = (ebml_string*)EBML_MasterFindChild(RSegmentInfo, MATROSKA_getContextMuxingApp());
            AppName = (ebml_string*)EBML_MasterFindChild(RSegmentInfo, MATROSKA_getContextWritingApp());
            if (LibName)
            {
                EBML_StringGet(LibName, String, TSIZEOF(String));
                tcscat_s(App, TSIZEOF(App), String);
            }
            if (AppName)
            {
                EBML_StringGet(AppName, String, TSIZEOF(String));
                if (App[0])
                    tcscat_s(App, TSIZEOF(App), T(" / "));
                tcscat_s(App, TSIZEOF(App), String);
            }
            if (App[0] == 0)
                tcscat_s(App, TSIZEOF(App), T("<unknown>"));
            TextPrintf(StdErr, T("\r\tcreated with %s\r\n"), App);
        }
    }

    for (Cluster = ARRAYBEGIN(RClusters,ebml_master*);Cluster != ARRAYEND(RClusters,ebml_master*); ++Cluster)
        NodeDelete((node*)*Cluster);
    ArrayClear(&RClusters);
    if (RAttachments)
        NodeDelete((node*)RAttachments);
    if (RTags)
        NodeDelete((node*)RTags);
    if (RCues)
        NodeDelete((node*)RCues);
    if (RChapters)
        NodeDelete((node*)RChapters);
    if (RTrackInfo)
        NodeDelete((node*)RTrackInfo);
    if (RSegmentInfo)
        NodeDelete((node*)RSegmentInfo);
    if (RLevel1)
        NodeDelete((node*)RLevel1);
    if (RSegment)
        NodeDelete((node*)RSegment);
    if (EbmlHead)
        NodeDelete((node*)EbmlHead);
    ArrayClear(&Tracks);
    if (Input)
        StreamClose(Input);

    // Core-C ending
    ParserContext_Done(&p);

    return Result;
}
