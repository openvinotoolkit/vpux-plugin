// {% copyright %}
///
/// @file      PlgOTTypes.h
/// 

#ifndef __PLG_OTTYPES_H__
#define __PLG_OTTYPES_H__
#include <vector>


namespace vpuot
{

struct Rect
{
  public:
    int32_t x, y, width, height, area;
    Rect() {
        x = y = width = height = area = 0;
    }
    Rect(int32_t x, int32_t y, int32_t width, int32_t height) : x(x), y(y), width(width), height(height) {
        width = std::max((int32_t) 0, width);
        height = std::max((int32_t) 0, height);
        area = width * height;
    }
};

enum TrackType
{
    SHORT_TERM                  = 0,    // Not supported on VPU/ARM
    SHORT_TERM_KCFVAR           = 1,    // Not supported on VPU/ARM
    SHORT_TERM_SIAMRPN          = 2,    // Not supported on VPU/ARM
    SHORT_TERM_IMAGELESS        = 3,
    ZERO_TERM                   = 4,    // Not supported on VPU/ARM
    ZERO_TERM_IMAGELESS         = 5,
    ZERO_TERM_COLOR_HISTOGRAM   = 6
};

inline const char* ToString(TrackType v)
{
    switch (v)
    {
        case SHORT_TERM:                return "SHORT_TERM";
        case SHORT_TERM_KCFVAR:         return "SHORT_TERM_KCFVAR";
        case SHORT_TERM_SIAMRPN:        return "SHORT_TERM_SIAMRPN";
        case SHORT_TERM_IMAGELESS:      return "SHORT_TERM_IMAGELESS";
        case ZERO_TERM:                 return "ZERO_TERM";
        case ZERO_TERM_IMAGELESS:       return "ZERO_TERM_IMAGELESS";
        case ZERO_TERM_COLOR_HISTOGRAM: return "ZERO_TERM_COLOR_HISTOGRAM";
        default:      return "[Unknown TrackType]";
    }
}

typedef enum {
    /**
     *  This is the status to indicate successful completion.
     */
    OT_SUCCESSFUL                                       = 0,
    /**
     *  This is the status to indicate that the specified tracking type is not supported.
     */
    OT_CREATE_NOT_SUPPORTED_TRACKING_TYPE               = 1,
    /**
     *  This is the status to indicate that "svuInit" failed.
     */
    OT_CREATE_SVU_INIT_ERROR                            = 2,
    /**
     *  This is the status to indicate that "svuOpenShave" returned an error.
     */
    OT_CREATE_SVU_OPEN_ERROR                            = 3,
    /**
     *  This is the status to indicate that "svuSolveShaveRelAddr" returned an error.
     */
    OT_CREATE_SVU_SOLVE_SHV_REAL_ADDR_ERROR             = 4,
    /**
     *  This is the status to indicate that "svuGetContextDataOfShaveApp" returned an error.
     */
    OT_CREATE_SVU_GET_CONTEXT_DATA_OF_SHVAPP_ERROR      = 5,
    /**
     *  This is the status to indicate that "svuCfgShaveAppStackSize" returned an error.
     */
    OT_CREATE_SVU_CFG_SHVAPP_STACK_SIZE_ERROR           = 6,
    /**
     *  This is the status to indicate that "svuCfgShaveAppHeapSize" returned an error.
     */
    OT_CREATE_SVU_CFG_SHVAPP_HEAP_SIZE_ERROR            = 7,
    /**
     *  This is the status to indicate that "svuInstantiateShvAppContextData" returned an error.
     */
    OT_CREATE_SVU_INSTANTIATE_SHVAPP_CONTEXTDATA_ERROR  = 8,
    /**
     *  This is the status to indicate that "OsDrvResMgrAllocate" returned an error.
     */
    OT_CREATE_DRV_RESMGR_ALLOC_ERROR                    = 9,
    /**
     *  This is the status to indicate that the shave index allocated from resource manager was invalid.
     */
    OT_CREATE_DRV_RESMGR_INVALID_SHV_ID                 = 10,
    /**
     *  This is the status to indicate that the number of allocated shaves was invalid.
     */
    OT_CREATE_INVALID_NUM_ALLOC_SHAVES                  = 11,
    /**
     *  This is the status to indicate that there is no enough space for stack within CMX slice size.
     */
    OT_CREATE_OUT_OF_BOUND_CMX_SLICE_SIZE               = 12,
    /**
     *  This is the status to indicate that allocating text buffer in DDR from memory manager failed.
     */
    OT_CREATE_MMGR_ALLOC_SVU_TEXT_BUF_ERROR             = 13,
    /**
     *  This is the status to indicate that allocating data buffer in CMX slice from memory manager failed.
     */
    OT_CREATE_MMGR_ALLOC_SVU_DATA_BUF_ERROR             = 14,
    /**
     *  This is the status to indicate that allocating memory for DDR heap from memory manager failed.
     */
    OT_CREATE_MMGR_ALLOC_SVU_DDR_HEAP_ERROR             = 15,
    /**
     *  This is the status to indicate that converting windows addresses to physical addresses failed.
     */
    OT_CREATE_SVU_SOLVE_SHV_REL_ADDR_ERROR              = 16,
    /**
     *  This is the status to indicate that retrieving entry point address from entry point name failed.
     */
    OT_EXECUTE_SVU_GET_ENTRY_ADDR_ERROR                 = 17,
    /**
     *  This is the status to indicate that retrieving configured shave window address failed.
     */
    OT_EXECUTE_SVU_GET_SHV_WINDOW_ERROR                 = 18,
    /**
     *  This is the status to indicate that "svuStartShaveCC" returned an error.
     */
    OT_EXECUTE_SVU_START_SHV_ERROR                      = 19,
    /**
     *  This is the status to indicate that "svuWaitShave" returned an error.
     */
    OT_EXECUTE_SVU_WAIT_SHV_ERROR                       = 20,
    /**
     *  This is the status to indicate that "svuCloseShave" returned an error.
     */
    OT_RELEASE_SVU_CLOSE_SHV_ERROR                      = 21,
    /**
     *  This is the status to indicate that "svuDeInit" returned an error.
     */
    OT_RELEASE_SVU_DEINIT_ERROR                         = 22,
    /**
     *  This is the status to indicate that releasing text buffer in DDR from memory manager failed.
     */
    OT_RELEASE_MMGR_FREE_TEXT_BUF_ERROR                 = 23,
    /**
     *  This is the status to indicate that releasing data buffer in CMX slice from memory manager failed.
     */
    OT_RELEASE_MMGR_FREE_DATA_BUF_ERROR                 = 24,
    /**
     *  This is the status to indicate that releasing memory for DDR heap from memory manager failed.
     */
    OT_RELEASE_MMGR_FREE_DDR_HEAP_ERROR                 = 25,
    /**
     *  This is the status to indicate that "ResMgrRelease" returned an error.
     */
    OT_RELEASE_DRV_RESMGR_RELEASE_ERROR                 = 26,
    /**
     *  This is the status to indicate that "svuUninstantiateShvApp" returned an error.
     */
    OT_RELEASE_SVU_UNINST_SHV_APP_ERROR                 = 27
} ot_status_code;

/**
 * @enum TrackingStatus
 *
 * Tracking status.
 */
enum struct TrackingStatus
{
    NEW,         /**< The object is newly added. */
    TRACKED,     /**< The object is being tracked. */
    LOST         /**< The object gets lost now. The object can be tracked again automatically(long term tracking) or by specifying detected object manually(short term and zero term tracking). */
};

/**
 * @class DetectedObject
 * @brief Represents an input object.
 *
 * In order to track an object, detected object should be added one or more times to ObjectTracker.
 * When an object is required to be added to ObjectTracker, you can create an instance of this class and fill its values.
 */

class DetectedObject
{
public:
    DetectedObject(const Rect& input_rect, int32_t input_class_label) : rect(input_rect), class_label(input_class_label) {}

    Rect rect;
    int32_t class_label = -1;
};
typedef std::vector<DetectedObject> DetectedObjects;

/**
 * @struct Object
 * @brief Represents tracking result of a target object.
 *
 * It contains tracking information of a target object.
 * ObjectTracker generates an instance of this class per tracked object when Track method is called.
*/

class Object {
public:
    /**

     * Object rectangle.
     */
    Object() : rect(Rect()), tracking_id (-1), class_label(-1), status(TrackingStatus::LOST), association_idx(-1) {}
    Object(const Rect& rect, uint64_t tid, int32_t class_label, TrackingStatus status, int32_t idx = -1) : rect(rect), tracking_id(tid), class_label(class_label), status(status), association_idx(idx) {}
    Rect rect;

    /**
     * Tracking ID.
     */
    uint64_t tracking_id;

    /**
     * Class label.
     * It is the value specified in DetectedObject.
     */
    int32_t class_label;

    /**
     * Tracking status.
     */
    TrackingStatus status;

    /**
     * Index in the DetectedObject vector.
     * If the Object was not in detection input, then it will be -1.
     */
    int32_t association_idx;
};

template<typename TrackingStatus>
std::ostream& operator<<(typename std::enable_if<std::is_enum<TrackingStatus>::value, std::ostream>::type& stream, const TrackingStatus& e)
{
    return stream << static_cast<typename std::underlying_type<TrackingStatus>::type>(e);
}

typedef std::vector<Object> Objects;
struct OutObjects {} ;
#if 0
struct OutObjects : public PoBuf {
    public:
        Object *objects;
        uint32_t nObjects;
        uint32_t channelID;
        OutObjects() {
            channelID=0;
            objects = NULL;
            nObjects = 0;
        }
};
#endif
typedef PoPtr<OutObjects> OutObjectsPtr;

}  // namespace vpuot

#endif  // __PLG_OTTYPES_H__
