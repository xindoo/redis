/* quicklist.h - A generic doubly linked quicklist implementation
 *
 * Copyright (c) 2014, Matt Stancliff <matt@genges.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this quicklist of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this quicklist of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h> // for UINTPTR_MAX

#ifndef __QUICKLIST_H__
#define __QUICKLIST_H__

/* Node, quicklist, and Iterator are the only data structures used currently. */

/* quicklistNode 中用了32个字节存储一个节点，三个指针总计24字节，加sz和后面按位用的几个int，总计32字节
 * 用了32个bit来保存ziplist的信息。 
 * count: 16位，最大65536(最大ziplist的大小是65k，所以实际上count小于32k)
 * encoding: 2位，RAW=1, LZF=2. 
 * container: 2位，NONE=1, ZIPLIST=2. 
 * recompress: 1位，bool类型，如果是true表示当前节点是临时使用的解压后的节点. 
 * attempted_compress: 1位， boolean类型，基本是在测试中使用  
 * extra: 用剩下的10位，暂时没有用到，留给之后的feature */
typedef struct quicklistNode {
    struct quicklistNode *prev;
    struct quicklistNode *next;
    unsigned char *zl;           /* quicklist节点对应的ziplist */
    unsigned int sz;             /* ziplist的字节数 */
    unsigned int count : 16;     /* ziplist的item数*/
    unsigned int encoding : 2;   /* 数据类型，RAW==1 or LZF==2 */
    unsigned int container : 2;  /* NONE==1 or ZIPLIST==2 */
    unsigned int recompress : 1; /* 这个节点以前压缩过吗？ */
    unsigned int attempted_compress : 1; /* node can't compress; too small */
    unsigned int extra : 10; /* 未使用到的10位 */
} quicklistNode;

/* quicklistLZF is a 4+N byte struct holding 'sz' followed by 'compressed'.
 * 'sz' is byte length of 'compressed' field.
 * 'compressed' is LZF data with total (compressed) length 'sz'
 * NOTE: uncompressed length is stored in quicklistNode->sz.
 * When quicklistNode->zl is compressed, node->zl points to a quicklistLZF */
typedef struct quicklistLZF {
    unsigned int sz; /* LZF size in bytes*/
    char compressed[];
} quicklistLZF;

/* Bookmarks are padded with realloc at the end of of the quicklist struct.
 * They should only be used for very big lists if thousands of nodes were the
 * excess memory usage is negligible, and there's a real need to iterate on them
 * in portions.
 * When not used, they don't add any memory overhead, but when used and then
 * deleted, some overhead remains (to avoid resonance).
 * The number of bookmarks used should be kept to minimum since it also adds
 * overhead on node deletion (searching for a bookmark to update). */
typedef struct quicklistBookmark {
    quicklistNode *node;
    char *name;
} quicklistBookmark;

#if UINTPTR_MAX == 0xffffffff
/* 32-bit */
#   define QL_FILL_BITS 14
#   define QL_COMP_BITS 14
#   define QL_BM_BITS 4
#elif UINTPTR_MAX == 0xffffffffffffffff
/* 64-bit */
#   define QL_FILL_BITS 16
#   define QL_COMP_BITS 16
#   define QL_BM_BITS 4 /* we can encode more, but we rather limit the user since they cause performance degradation. */
#else
#   error unknown arch bits count
#endif

/* quicklist是一个40字节的结构体(在64位系统中)，具体字段如下： 
 * count: quicklist中所有数据项的个数
 * len：quicklist中的节点数  
 * compress: quicklist的压缩深度，0表示不压缩，否则就表示从两端开始有多少个节点不压缩
 * bookmarks是一个可选字段，用来quicklist重新分配内存空间时使用，不使用时不占用空间 */
typedef struct quicklist {
    quicklistNode *head;        /* 头结点 */
    quicklistNode *tail;        /* 尾结点 */
    unsigned long count;        /* 在所有的ziplist中的entry总数 */
    unsigned long len;          /* quicklist节点总数 */
    int fill : QL_FILL_BITS;              /* 16位，每个节点的最大容量*/
    unsigned int compress : QL_COMP_BITS; /* 16位，quicklist的压缩深度，0表示所有节点都不压缩，否则就表示从两端开始有多少个节点不压缩 */
    unsigned int bookmark_count: QL_BM_BITS;  /*4位，bookmarks数组的大小，bookmarks是一个可选字段，用来quicklist重新分配内存空间时使用，不使用时不占用空间*/
    quicklistBookmark bookmarks[];
} quicklist;

typedef struct quicklistIter {
    const quicklist *quicklist;
    quicklistNode *current;
    unsigned char *zi;
    long offset; /* offset in current ziplist */
    int direction;
} quicklistIter;

typedef struct quicklistEntry {
    const quicklist *quicklist;
    quicklistNode *node;
    unsigned char *zi;
    unsigned char *value;
    long long longval;
    unsigned int sz;
    int offset;
} quicklistEntry;

#define QUICKLIST_HEAD 0
#define QUICKLIST_TAIL -1

/* quicklist node encodings */
#define QUICKLIST_NODE_ENCODING_RAW 1
#define QUICKLIST_NODE_ENCODING_LZF 2

/* quicklist compression disable */
#define QUICKLIST_NOCOMPRESS 0

/* quicklist container formats */
#define QUICKLIST_NODE_CONTAINER_NONE 1
#define QUICKLIST_NODE_CONTAINER_ZIPLIST 2

#define quicklistNodeIsCompressed(node)                                        \
    ((node)->encoding == QUICKLIST_NODE_ENCODING_LZF)

/* Prototypes */
quicklist *quicklistCreate(void);  // 创建quicklist 
quicklist *quicklistNew(int fill, int compress);  // 用一些指定参数创建一个新的quicklist
void quicklistSetCompressDepth(quicklist *quicklist, int depth);  // 设置压缩深度 
void quicklistSetFill(quicklist *quicklist, int fill); // 设置容量上限 
void quicklistSetOptions(quicklist *quicklist, int fill, int depth); 
void quicklistRelease(quicklist *quicklist); // 释放quicklist
int quicklistPushHead(quicklist *quicklist, void *value, const size_t sz);  // 头部插入
int quicklistPushTail(quicklist *quicklist, void *value, const size_t sz);  // 尾部插入
void quicklistPush(quicklist *quicklist, void *value, const size_t sz, 
                   int where); // 指定头部或者尾部插入  
void quicklistAppendZiplist(quicklist *quicklist, unsigned char *zl); // 把一个ziplist放到quicklist中
quicklist *quicklistAppendValuesFromZiplist(quicklist *quicklist,   
                                            unsigned char *zl); // 把ziplist中的所有数据放到quicklist中
quicklist *quicklistCreateFromZiplist(int fill, int compress,
                                      unsigned char *zl);  // 从ziplist生成一个quicklist  
void quicklistInsertAfter(quicklist *quicklist, quicklistEntry *node,
                          void *value, const size_t sz);  
void quicklistInsertBefore(quicklist *quicklist, quicklistEntry *node,
                           void *value, const size_t sz);
void quicklistDelEntry(quicklistIter *iter, quicklistEntry *entry); // 数据删除 
int quicklistReplaceAtIndex(quicklist *quicklist, long index, void *data,
                            int sz);   // 数据替换 
int quicklistDelRange(quicklist *quicklist, const long start, const long stop);  // 范围删除  
quicklistIter *quicklistGetIterator(const quicklist *quicklist, int direction);  // 迭代器 
quicklistIter *quicklistGetIteratorAtIdx(const quicklist *quicklist,
                                         int direction, const long long idx);  // 从指定位置开始的迭代器  
int quicklistNext(quicklistIter *iter, quicklistEntry *node);   // 迭代器下一个位置  
void quicklistReleaseIterator(quicklistIter *iter);  // 释放迭代器  
quicklist *quicklistDup(quicklist *orig);  // 去重  
int quicklistIndex(const quicklist *quicklist, const long long index,
                   quicklistEntry *entry);  // 找到entry的下标索引 
void quicklistRewind(quicklist *quicklist, quicklistIter *li);
void quicklistRewindTail(quicklist *quicklist, quicklistIter *li);
void quicklistRotate(quicklist *quicklist);  // 选择quicklist  
int quicklistPopCustom(quicklist *quicklist, int where, unsigned char **data,
                       unsigned int *sz, long long *sval,
                       void *(*saver)(unsigned char *data, unsigned int sz)); 
int quicklistPop(quicklist *quicklist, int where, unsigned char **data, 
                 unsigned int *sz, long long *slong); // 数据pop 
unsigned long quicklistCount(const quicklist *ql);
int quicklistCompare(unsigned char *p1, unsigned char *p2, int p2_len); // 比较大小  
size_t quicklistGetLzf(const quicklistNode *node, void **data);  // LZF节点  

/* bookmarks */
int quicklistBookmarkCreate(quicklist **ql_ref, const char *name, quicklistNode *node);
int quicklistBookmarkDelete(quicklist *ql, const char *name);
quicklistNode *quicklistBookmarkFind(quicklist *ql, const char *name);
void quicklistBookmarksClear(quicklist *ql);

#ifdef REDIS_TEST
int quicklistTest(int argc, char *argv[]);
#endif

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __QUICKLIST_H__ */
