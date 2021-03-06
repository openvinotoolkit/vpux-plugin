// {% copyright %}
///
/// @file      TraceProfiling.h
/// @copyright All code copyright Movidius Ltd 2019, all rights reserved.
///            For License Warranty see: common/license.txt
///
/// @brief     Header for Host side Trace Profiling usage over VPUAL.
///
#ifndef __TRACE_PROFILING__
#define __TRACE_PROFILING__

#include "VpualDispatcher.h"
#include <atomic>
#include <thread>

#define MINIMUM_PROFILING_BUFFER_SIZE (2490368)

#ifndef XLINK_INVALID_CHANNEL_ID
#define XLINK_INVALID_CHANNEL_ID (0xFFFF)
#endif

#define TRACE_PROFILER_XLINK_CHAN_SIZE (100)

/**
 * Trace Profiling API.
 */
// TODO - determine whether other profiling types are to be supported and update Profile Type as necessary.
enum class ProfileType {
    TRACE_PROFILER    = 2
};

enum class Components: uint32_t {
    ProfilerTest = 0,
    VPUAL,
    NN_PERFORMANCE,
    SIPP,
    M2I,
    OBJECT_TRACKER,
    FLIC,
    OSD
};

enum class ProfileLevel: int {
    LOG_LEVEL_FATAL   = 1,
    LOG_LEVEL_ERROR   = 2,
    LOG_LEVEL_WARNING = 3,
    LOG_LEVEL_INFO    = 4,
    LOG_LEVEL_DEBUG   = 5,
    LOG_LEVEL_TRACE   = 6
};

enum class ProfilingEvent{
    BUFFER_0_FULL = 0,
    BUFFER_1_FULL
};

/**
 * System clock type.
 */
enum class ClockType {
    REALTIME,
    MONOTONIC_RAW,
    MONOTONIC_RAW_START
};

typedef void (*profilingEventCallback_t)(
    ProfilingEvent event
);

struct buffer_t {
    uint32_t paddr;
    uint8_t* vaddr;
    uint32_t size;
};

enum class sm_event : uint8_t {
    ALLOCATION,
    RELEASE
};

struct statistics {
    float utilisation;
    int32_t timestamp;
    int32_t num_active_resources;
    sm_event last_action;
};

struct UtilisationStatistics {
    statistics nn_dpu_cluster;
    statistics upa_shave;
    statistics hw_filter;
    statistics cmx;
};

/**
 * Number of system resources.
 */
struct SystemResources {
    uint32_t num_dpu_cluster;
    uint32_t num_shave;
    uint32_t num_ff_accel;
};

/**
 * Trace Profiling Stub Class.
 */
class TraceProfiling : private VpualStub {
  private:
    uint16_t profilingChannelId = XLINK_INVALID_CHANNEL_ID;
    bool sm_enabled;
    uint32_t device_id;

  public:
    TraceProfiling(uint32_t device_id=0) : VpualStub("TraceProfiling", device_id), sm_enabled(false), device_id(device_id) {};

    void Create(uint32_t pBaseAddr0, uint32_t size, uint32_t pBaseAddr1, uint32_t size1, uint8_t *vAddr0, uint8_t *vAddr1, uint32_t alignment = 64);
    void Delete();

    // TODO - check return types
    /**
     * Check if specified profiler type is enabled.
     *
     * @param [ProfilerType] type - The profiler type to be enabled.
     *
     * @return      [description]
     */
    int is_enabled(ProfileType type) const;

    /**
     * Enable specified profiling type.
     *
     * @param [ProfilerType] type - The profiling type to enable.
     */
    void set_enabled(ProfileType type) const;

    /**
     * Disable specified profiling type.
     *
     * @param [ProfilerType] type - The profiling type to disable.
     */
    void set_disabled(ProfileType type) const;

    // TODO - need to better specify the the components and levels - should have enumerations for each.
    /**
     * Set trace level for the secified component.
     *
     * @param component - Component to set profiling level for.
     * @param level     - Profiling level.
     */
    void set_profiler_component_trace_level(Components component, ProfileLevel level) const;

    // TODO - not sure if this one is needed. May replace with a simple function to read the buffers.
    //void get_profiler_current_buffer_fill_level(size_t *buffer_filled) const;

    /**
     * Get xlink channel ID, can be used for debugging purposes.
     *
     * @param component - Component to set profiling level for.
     * @param level     - Profiling level.
     */
    uint16_t get_profiler_xlink_channel_id(void);

    /**
     * Sync system time with VPU.
     *
     * @param type - System clock with which to sync.
     *
     * @return [int]
     */
    int syncSystemTime(ClockType type);

    /**
     * StatisticsMonitor API
     */

    /**
     * Enable monitoring of VPU utilisation.
     *
     * @return [int] - 0 on success, failure otherwise.
     */
    int StatisticsMonitorEnable();

    /**
     * Disable monitoring of VPU utilisation.
     *
     * @return [int] - 0 on success, failure otherwise.
     */
    int StatisticsMonitorDisable();

    /**
     * Check whether statistics monitoring has been enabled.
     *
     * @return [bool] - true if enabled, false if not enabled.
     */
    bool StatisticsMonitorIsEnabled();

    /**
     * Set the length of the window over which utilisation statistics will be
     * calculated. The default window length is 1000ms.
     *
     * @param length_ms - Window length in ms.
     */
    void StatisticsMonitorSetWindowLength(uint64_t length_ms);

    /**
     * Return the current VPU utilisation statistics.
     *
     * @return [UtilisationStatistics] - Structure containing VPU utilisation statistics.
     */
    UtilisationStatistics StatisticsMonitorGetCurrentStatistics();

    /**
     * Return information on the number of DPUs, shaves and FF HW accelerators in the system.
     *
     * @return [SystemResources] - Structure containing system resource information.
     */
    SystemResources StatisticsMonitorGetResources();

  private:

    buffer_t buffer0 = {0,0,0};
    buffer_t buffer1 = {0,0,0};

    std::thread thread_object;
    std::atomic_bool alive {true};

    /**
     * Read and action XLink messages from host on dedicated profiling channel.
     */
    void CheckXlinkMessageFunc();

    /**
     * Save profiling data contained in specified buffer to file.
     *
     * @param address - Address of profiling buffer.
     */
    int save_profiler_data_to_file(unsigned char* address, uint32_t size);
};

#endif /* __TRACE_PROFILING__ */
