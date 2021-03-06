// {% copyright %}
/*
SPDX-License-Identifier: MIT
*/

#ifndef _VPUMGR_H_
#define _VPUMGR_H_
#include <stddef.h>
#include <stdint.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CSRAM_SLICE_ID specifies a special vpumgr device which has only a
 * CSRAM memory-region resource but no VPU hardware.
 * So only dmabuf allocation API is supposed to work for CSRAM_SLICE_ID,
 * but the dmabuf fd allocated from it can be imported and used by any other
 * slice with real underlying VPU hardware.
 */
#define CSRAM_SLICE_ID 32
#define PPR_SLICE_ID 64
#define CSRAM_SLICE0 (CSRAM_SLICE_ID)
#define CSRAM_SLICE1 (CSRAM_SLICE_ID + 1)
#define CSRAM_SLICE2 (CSRAM_SLICE_ID + 2)
#define CSRAM_SLICE3 (CSRAM_SLICE_ID + 3)
#define PPR_SLICE0 (PPR_SLICE_ID)
#define PPR_SLICE1 (PPR_SLICE_ID + 1)
#define PPR_SLICE2 (PPR_SLICE_ID + 2)
#define PPR_SLICE3 (PPR_SLICE_ID + 3)

/** vpu-shared memory type
 */
enum VPUSMMType {
    VPUSMMTYPE_COHERENT = 0x0000,
    /**< allocate coherent memory, it's the default
         - cache coherent
         - using non-cachable(writecombine) mapping on CPU side
         - no explicit cache flush/invalidate is required
         - low performance for CPU side access
    */
    VPUSMMTYPE_NON_COHERENT = 0x0001,
    /**< allocate non-coherent memory
         - cache non-coherent
         - using cachable mapping on CPU side
         - explicit cache flush/invalidate is required
         - high performance for CPU side access
    */
};

enum VPUAccessType {
    VPU_DEFAULT = 0x00, // this has the same meaning as VPU_RW
    VPU_READ = 0x01,
    VPU_WRITE = 0x02,
    VPU_RW = 0x03, // VPU_READ | VPU_WRITE
    VPU_WR = 0x03, // VPU_READ | VPU_WRITE
};

#define NUM_OF_PLANE 3

struct _VIV_VIDMEM_METADATA {
    __u32 magic;        // __FOURCC('v', 'i', 'v', 'm')
    __u32 dmabuf_size;  // DMABUF buffer size in byte (Maximum 4GB)
    __u32 time_stamp;   // time stamp for the DMABUF buffer
    __u32 image_format; // viv image format, e.g. IMAGE_NV12 or IMAGE_P010
    __u32 compressed;   // if DMABUF buffer is compressed by DEC400
                        // 1 = compressed, 0 = not compressed
    struct {
        __u32 offset;          // plane buffer address offset from DMABUF address
        __u32 stride;          // pitch in byte
        __u32 width;           // width in pixels
        __u32 height;          // height in pixels
        __u32 tile_format;     // uncompressed tile format
        __u32 compress_format; // tile mode for DEC400
        __u32 ts_offset;       // tile status buffer offset within this plane buffer
        __s32 ts_fd;           // fd of separate tile status buffer of the plane buffer
        __s32 ts_fd2;          // valid fd of the ts buffer in consumer side
        __s32 ts_vaddr;        // the vpu virtual address for this ts data buffer
        __u32 fc_enabled;      // gpu fastclear enabled for the plane buffer
        __u32 fc_value_lower;  // gpu fastclear color value (lower 32 bits)
                               // for the plane buffer
        __u32 fc_value_upper;  // gpu fastclear color value (upper 32 bits)
                               // for the plane buffer
    } plane[NUM_OF_PLANE];
};

struct _VPU_IP_METADATA {
    __u32 compression;                   // __FOURCC('n', 'o', 'n', 'e'): no compression
                                         // __FOURCC('d', 'e', 'c', '4'): DEC400 compression
    __u32 dmabuf_size;                   // DMABUF buffer size in bytes (maximum 4 GB)
    __u32 image_format;                  // viv image format, e.g. IMAGE_NV12 or IMAGE_P010
    __u32 timestamp;                     // time stamp for the DMABUF buffer
    __u32 stride;                        // pitch in bytes
    __u32 width;                         // width in pixels
    __u32 height;                        // height in pixels
    __u32 offset[NUM_OF_PLANE];          // plane buffer address offset from main buffer address
    __s32 ts_fd[NUM_OF_PLANE];           // tile status buffer fd for planes 1,2,3,
                                         // the plugin shall do import and
                                         // call vpusmm to convert fd to vptr.
    __u32 ts_offset[NUM_OF_PLANE];       // tile status buffer offset from
                                         // ts_fd start address for planes 1,2,3.
    __u32 tile_format[NUM_OF_PLANE];     // uncompressed tile format for planes 1,2,3
    __u32 compress_format[NUM_OF_PLANE]; // DEC400 tile mode for planes 1,2,3
};

/** allocate a VPU-shared buffer object for multi slice
 * @param size [IN]: size in bytes, must be multiple of page size, otherwise mmap syscall may fail
 * @param type [IN]: vpu-shared memory type
 * @param slice_idx [IN]: index of slice, starting from 0. It indicates from which slice VPU shared buffer will be
 * allocated
 * @return: the file descriptor of the underlying vpu-shared buffer object
 *          or <0 if failed
 *
 * user is supposed to call standard UNIX API close() on the fd
 * when not using it anymore, failing doing so will cause leakage
 * during runtime but will be cleaned when application process
 * ceased execution.
 *
 */
int vpurm_alloc_dmabuf(unsigned long size, enum VPUSMMType type, int slice_idx);

/**  Import an external DMABuf object into VPU device for accessing on VPU side for multi slice
* @param dmabuf_fd [IN] : the DMABuf fd exported by other driver(codec/camera/...) or our driver
* @param vpu_access [IN] : how VPU is going to access the buffer (readonly or writeonly or readwrite)
*                          application should provide this param as accurate as possible, use VPU_DEFAULT
*                          when not sure, this mainly affect cache coherency
* @param slice_idx [IN]: index of slice, starting from 0. It indicates from which slice VPU shared buffer the external
                         DMABuf will be imported.
* @return a non-zero VPU-side address corresponds to the DMABuf which suitable for VPU to access.
*         zero if the DMABuf is not suitable for VPU to access (for example, due to not physical contiguous)
*         in this case, user should allocate another DMABuf with VPUSMM and do copy by CPU
*
* import operation is vital for VPU to access the buffer because:
*   1. necessary mapping resource is setup and maintained during import for VPU to access the buffer
*   2. the physical pages is pinned during import so no page fault will happen
*   3. kernel rb-tree is setup and maintained so virt-addr can be translated into device address
*/
unsigned long vpurm_import_dmabuf(int dmabuf_fd, enum VPUAccessType vpu_access, int slice_idx);

/**  Unimport an imported external DMABuf when VPU side no longer accesses it from multi slice
* @param dmabuf_fd [IN] : the DMABuf fd to be unimported
* @param slice_idx [IN]: index of slice, starting from 0. It indicates from which slice VPU shared buffer the external
                         DMABuf will be unimported.
* after this function call, the reference to the DMABuf will be freed and the VPU address should not
* be considered to be valid any more, so call this function only after VPU-side access has done.
*/
void vpurm_unimport_dmabuf(int dmabuf_fd, int slice_idx);

/**  get VPU accessible address from any valid virtual address
* @param ptr [IN] : the valid (possibly offseted) virtual address inside
*                   a valid allocated mem-mapped DMABuf.
*                   the corresponding DMABuf fd should have been imported by vpusmm_import_dmabuf()
*@param slice_idx [IN]: index of slice, starting from 0. It indicates from which slice VPU shared buffer to get
                    VPU accessible address.
* @return VPU-side address corresponds to the input virtual address
*         or zero if the input pointer is not within a valid mem-mapped
*         buffer object.
*/
unsigned long vpurm_ptr_to_vpu(void *ptr, int slice_idx);

/**  get VPU accessible address from any valid virtual address
 * @param dmabuf_fd [IN] : the DMABuf fd representing a pixel buffer which contain DEC400 meta data
 *						in its priv field.
 * @param slice_index [IN]: index of slice, starting from 0. It indicates DEC400 meta data will be gotten
 *						from which VPU slice.
 * @param struct __VPU_IP_METADATA *meta_info[out]: get the meta data content from this structure pointer.
 * @return Return 0 to indicate Success, -1 to indicate Failure.
 */

int vpurm_fetch_meta_from_fd(int dmabuf_fd, int slice_idx, struct _VPU_IP_METADATA *meta_info);
/*
 * following APIs are to be compatible with old VPUSMM API, works on slice 0 only
 */
int vpusmm_alloc_dmabuf(unsigned long size, enum VPUSMMType type);
unsigned long vpusmm_import_dmabuf(int dmabuf_fd, enum VPUAccessType vpu_access);
void vpusmm_unimport_dmabuf(int dmabuf_fd);
unsigned long vpusmm_ptr_to_vpu(void *ptr);

/*
 * VPU Context manager is a light-weight framework to
 * 1. automatically create vpu side context object when first vpurm API is invoked on specified vpu slice;
 * 2. automatically destroy vpu side context object when application exit or crashes;
 * 3. provide both synchronous (call) and asynchronous (submit/wait) API for communicating with
 *    corresponding vpu side context object;
 */
int vpurm_vpu_call(int slice_idx, int cmd, const void *in, int in_len, void *out, int *p_out_len);

int vpurm_vpu_submit(int slice_idx, int cmd, const void *in, int in_len);
int vpurm_vpu_wait(int slice_idx, int submit_id, void *out, int *p_out_len, unsigned long timeout_ms);

int vpurm_open_vpu(int slice_idx);
void vpurm_close_vpu(int slice_idx);

#ifdef __cplusplus
}
#endif

#endif
