/*
 * Copyright (c) 2008-2010, Matroska (non-profit organisation)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LIBEBML2_H
#define LIBEBML2_H

#include <corec/node/node.h>
#include <corec/helpers/parser/parser.h>
#include "ebml2/ebml_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTEXT_LIBEBML_VERSION  0x400

#define EBML_MAX_VERSION    1
#define EBML_MAX_ID         4
#define EBML_MAX_SIZE       8

#define SCOPE_PARTIAL_DATA  0  // read all data, except inside some binary elements (useful for binary data with a (internal) header)
#define SCOPE_ALL_DATA      1
#define SCOPE_NO_DATA       2

#define EBML_ANY_PROFILE    0xFFFFFFFF

// base classes
#define EBML_ELEMENT_CLASS   FOURCC('E','B','E','L')
#define EBML_BINARY_CLASS    FOURCC('E','B','I','N')
#define EBML_DATE_CLASS      FOURCC('E','B','D','T')
#define EBML_INTEGER_CLASS   FOURCC('E','B','I','T')
#define EBML_SINTEGER_CLASS  FOURCC('E','B','S','I')
#define EBML_FLOAT_CLASS     FOURCC('E','B','F','L')
#define EBML_MASTER_CLASS    FOURCC('E','B','M','T')
#define EBML_STRING_CLASS    FOURCC('E','B','S','G')
#define EBML_UNISTRING_CLASS FOURCC('E','B','S','U')
#define EBML_BOOLEAN_CLASS   FOURCC('E','B','B','O')

// global classes
#define EBML_CRC_CLASS       FOURCC('E','B','C','R')
#define EBML_VOID_CLASS      FOURCC('E','B','V','D')

#define EBML_DUMMY_ID        FOURCC('E','B','D','U')

#define EBML_ELEMENT_INFINITESIZE   0x100
#define EBML_ELEMENT_OBJECT         0x101

#if defined(ebml2_EXPORTS)
#define EBML_DLL DLLEXPORT
#elif defined(ebml2_IMPORTS)
#define EBML_DLL DLLIMPORT
#else
#define EBML_DLL extern
#endif

typedef struct ebml_context ebml_context;
typedef struct ebml_parser_context ebml_parser_context;
typedef struct ebml_semantic ebml_semantic;
typedef struct ebml_element ebml_element;
typedef struct ebml_crc ebml_crc;

typedef struct ebml_master ebml_master;
typedef struct ebml_integer ebml_integer;
typedef struct ebml_string ebml_string;
typedef struct ebml_binary ebml_binary;
typedef struct ebml_integer ebml_date;
typedef struct ebml_float ebml_float;
typedef struct ebml_dummy ebml_dummy;

struct ebml_semantic
{
    bool_t Mandatory;
    bool_t Unique;
    const ebml_context *eClass;
    int DisabledProfile; // PROFILE_MATROSKA_V1 and others
};

struct ebml_parser_context
{
    const ebml_context *Context;
    const ebml_parser_context *UpContext;
    filepos_t EndPosition;
    int Profile;
};

typedef struct ebml_element_vmt
{
    nodetree_vmt Base;
    bool_t (*ValidateSize)(const void*);
    err_t (*ReadData)(void*, struct stream *Input, const ebml_parser_context *ParserContext, bool_t AllowDummyElt, int Scope, size_t DepthCheckCRC);
#if defined(CONFIG_EBML_WRITING)
    err_t (*RenderData)(void*, struct stream *Output, bool_t bForceWithoutMandatory, bool_t bWithDefault, int ForProfile, filepos_t *Rendered);
#endif
    bool_t (*IsDefaultValue)(const void*);
    bool_t (*DefaultIsSet)(const void*);
    filepos_t (*UpdateDataSize)(void*, bool_t bWithDefault, bool_t bForceWithoutMandatory, int ForProfile);
    bool_t (*NeedsDataSizeUpdate)(const void*, bool_t bWithDefault);
    int (*Cmp)(const void*, const void*);
    ebml_element *(*Copy)(const void*);

    // internal call only
    void (*PostCreate)(void*, bool_t SetDefault, int ForProfile);

} ebml_element_vmt;

#define EBML_ElementValidateSize(p)          VMT_FUNC(p,ebml_element_vmt)->ValidateSize(p)
#define EBML_ElementReadData(p,i,c,d,s,r)    VMT_FUNC(p,ebml_element_vmt)->ReadData(p,i,c,d,s,r)
#define EBML_ElementRenderData(p,s,f,k,i,r)  VMT_FUNC(p,ebml_element_vmt)->RenderData(p,s,f,k,i,r)
#define EBML_ElementIsDefaultValue(p)        VMT_FUNC(p,ebml_element_vmt)->IsDefaultValue(p)
#define EBML_ElementUpdateSize(p,k,f,i)      VMT_FUNC(p,ebml_element_vmt)->UpdateDataSize(p,k,f,i)
#define EBML_ElementNeedsDataSizeUpdate(p,d) VMT_FUNC(p,ebml_element_vmt)->NeedsDataSizeUpdate(p,d)
#define EBML_ElementCmp(p,e)                 VMT_FUNC(p,ebml_element_vmt)->Cmp(p,e)
#define EBML_ElementCopy(p)                  VMT_FUNC(p,ebml_element_vmt)->Copy(p)

EBML_DLL err_t EBML_Init(parsercontext *p);

EBML_DLL ebml_element *EBML_ElementCreate(anynode *Any, const ebml_context *Context, bool_t SetDefault, int ForProfile);

EBML_DLL ebml_element *EBML_FindNextId(struct stream *Input, const ebml_context *Context, size_t MaxDataSize);
EBML_DLL ebml_element *EBML_FindNextElement(struct stream *Input, const ebml_parser_context *Context, int *UpperLevels, bool_t AllowDummy);
EBML_DLL uint8_t EBML_CodedSizeLength(filepos_t Length, uint8_t SizeLength, bool_t bSizeIsFinite); // TODO: turn into a macro ?
EBML_DLL uint8_t EBML_CodedSizeLengthSigned(filepos_t Length, uint8_t SizeLength); // TODO: turn into a macro ?
EBML_DLL uint8_t EBML_CodedValueLength(filepos_t Length, size_t CodedSize, uint8_t *OutBuffer, bool_t bSizeIsFinite); // TODO: turn into a macro ?
EBML_DLL uint8_t EBML_CodedValueLengthSigned(filepos_t Length, size_t CodedSize, uint8_t * OutBuffer); // TODO: turn into a macro ?
EBML_DLL filepos_t EBML_ReadCodedSizeValue(const uint8_t *InBuffer, size_t *BufferSize, filepos_t *SizeUnknown);
EBML_DLL filepos_t EBML_ReadCodedSizeSignedValue(const uint8_t *InBuffer, size_t *BufferSize, filepos_t *SizeUnknown);

EBML_DLL void EBML_ElementGetName(const ebml_element *Element, tchar_t *Out, size_t OutLen);
EBML_DLL const char *EBML_ElementGetClassName(const ebml_element *Element);

EBML_DLL const ebml_context *EBML_ElementContext(const ebml_element *Element);

EBML_DLL ebml_element *EBML_ElementSkipData(ebml_element *Element, struct stream *Input, const ebml_parser_context *Context, ebml_element *TestReadElt, bool_t AllowDummy);
EBML_DLL bool_t EBML_ElementIsFiniteSize(const ebml_element *Element);
EBML_DLL void EBML_ElementSetInfiniteSize(const ebml_element *Element, bool_t Set);
EBML_DLL bool_t EBML_ElementIsDummy(const ebml_element *Element);

EBML_DLL fourcc_t EBML_ElementClassID(const ebml_element *Element);
EBML_DLL filepos_t EBML_ElementPosition(const ebml_element *Element);
EBML_DLL filepos_t EBML_ElementPositionData(const ebml_element *Element);
EBML_DLL filepos_t EBML_ElementPositionEnd(const ebml_element *Element);

EBML_DLL filepos_t EBML_ElementFullSize(const ebml_element *Element, bool_t bWithDefault);
EBML_DLL filepos_t EBML_ElementDataSize(const ebml_element *Element, bool_t bWithDefault);
EBML_DLL uint8_t EBML_ElementSizeLength(const ebml_element *Element);
EBML_DLL void EBML_ElementSetSizeLength(ebml_element *Element, uint8_t SizeLength); /// 0 (for auto) to EBML_MAX_SIZE

EBML_DLL bool_t EBML_ElementIsType(const ebml_element *Element, const ebml_context *Context);

#if defined(CONFIG_EBML_WRITING)
// TODO: replace the list of bools by flags ?
EBML_DLL err_t EBML_ElementRender(ebml_element *Element, struct stream *Output, bool_t bWithDefault, bool_t bKeepPosition, bool_t bForceWithoutMandatory, int ForProfile, filepos_t *Rendered);
EBML_DLL err_t EBML_ElementRenderHead(ebml_element *Element, struct stream *Output, bool_t bKeepPosition, filepos_t *Rendered);
#endif

#if defined(EBML2_UGLY_HACKS_API)
EBML_DLL void EBML_ElementForceContext(ebml_element *Element, const ebml_context *ForcedContext);
EBML_DLL bool_t EBML_ElementInfiniteForceSize(ebml_element *Element, filepos_t NewSize);
EBML_DLL void EBML_ElementForcePosition(ebml_element *Element, filepos_t Pos);
EBML_DLL void EBML_ElementForceDataSize(ebml_element *Element, filepos_t Size);
#endif // !EBML2_UGLY_HACKS_API

// type specific routines
EBML_DLL ebml_element *EBML_MasterFindFirstElt(ebml_master *Element, const ebml_context *Context, bool_t bCreateIfNull, bool_t SetDefault, int ForProfile);
EBML_DLL err_t EBML_MasterAppend(ebml_master *Element, ebml_element *Append);
EBML_DLL err_t EBML_MasterRemove(ebml_master *Element, ebml_element *Remove);
EBML_DLL ebml_element *EBML_MasterFindNextElt(ebml_master *Element, const ebml_element *Current, bool_t bCreateIfNull, bool_t SetDefault, int ForProfile);
EBML_DLL ebml_element *EBML_MasterAddElt(ebml_master *Element, const ebml_context *Context, bool_t SetDefault, int ForProfile);
EBML_DLL size_t EBML_MasterCount(const ebml_master *Element);
EBML_DLL void EBML_MasterClear(ebml_master *Element); // clear the list (the children and not freed)
EBML_DLL void EBML_MasterErase(ebml_master *Element);
EBML_DLL void EBML_MasterAddMandatory(ebml_master *Element, bool_t SetDefault, int ForProfile); // add the mandatory elements
EBML_DLL bool_t EBML_MasterCheckMandatory(const ebml_master *Element, bool_t bWithDefault, int ForProfile);
EBML_DLL void EBML_MasterSort(ebml_master *Element, arraycmp Cmp, const void* CmpParam);
EBML_DLL bool_t EBML_MasterUseChecksum(ebml_master *Element, bool_t Use);
EBML_DLL bool_t EBML_MasterIsChecksumValid(const ebml_master *Element);
#define EBML_MasterGetChild(e,c,p) EBML_MasterFindFirstElt(e,c,1,1,p)
#define EBML_MasterFindChild(e,c)  EBML_MasterFindFirstElt((ebml_master*)e,c,0,0,0)
#define EBML_MasterNextChild(e,c)  EBML_MasterFindNextElt((ebml_master*)e,(ebml_element*)c,0,0,0)
#define EBML_MasterChildren(p)     ((ebml_element*)NodeTree_Children(p))
#define EBML_MasterNext(p)         ((ebml_element*)NodeTree_Next(p))
#define EBML_ElementParent(p)      ((ebml_element*)NodeTree_Parent(p))

EBML_DLL int64_t EBML_IntegerValue(const ebml_integer *Element);
EBML_DLL void EBML_IntegerSetValue(ebml_integer *Element, int64_t Value);

EBML_DLL double EBML_FloatValue(const ebml_float *Element);
EBML_DLL void EBML_FloatSetValue(ebml_float *Element, double Value);

EBML_DLL err_t EBML_StringSetValue(ebml_string *Element,const char *Value);
EBML_DLL err_t EBML_UniStringSetValue(ebml_string *Element,const tchar_t *Value);
EBML_DLL void EBML_StringGet(ebml_string *Element,tchar_t *Out, size_t OutLen);

EBML_DLL datetime_t EBML_DateTime(const ebml_date *Element);
EBML_DLL err_t EBML_DateSetDateTime(ebml_date *Element, datetime_t Date);

EBML_DLL err_t EBML_BinarySetData(ebml_binary *Element, const uint8_t *Data, size_t DataSize);
EBML_DLL const uint8_t *EBML_BinaryGetData(ebml_binary *Element);

#if defined(CONFIG_EBML_WRITING)
EBML_DLL bool_t EBML_VoidSetFullSize(ebml_element *Void, filepos_t);
EBML_DLL filepos_t EBML_VoidReplaceWith(ebml_element *Void, ebml_element *Replaced, struct stream *Output, bool_t ComeBackAfterward, bool_t bWithDefault);
#endif
EBML_DLL size_t EBML_FillBufferID(uint8_t *Buffer, size_t BufSize, fourcc_t Id);
EBML_DLL size_t EBML_IdToString(tchar_t *Out, size_t OutLen, fourcc_t Id);
EBML_DLL fourcc_t EBML_BufferToID(const uint8_t *Buffer);

#define MASTER_CHECK_PROFILE_INVALID    0
#define MASTER_CHECK_MISSING_MANDATORY  1
#define MASTER_CHECK_MULTIPLE_UNIQUE    2

typedef bool_t (*ContextCallback)(void *cookie, int type, const tchar_t *ClassName, const ebml_element*);
EBML_DLL void EBML_MasterCheckContext(ebml_master *Element, int ProfileMask, ContextCallback callback, void *cookie);

#if defined(EBML_LEGACY_API)
#define CONTEXT_CONST
#else
#define CONTEXT_CONST const
#endif

// EBML contexts
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextHead(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextDummy(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextVersion(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextReadVersion(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextMaxIdLength(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextMaxSizeLength(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextDocType(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextDocTypeVersion(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextDocTypeReadVersion(void);

EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextEbmlVoid(void);
EBML_DLL CONTEXT_CONST ebml_context *EBML_getContextEbmlCrc32(void);

EBML_DLL const ebml_semantic * EBML_getSemanticGlobals(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBEBML2_H */
