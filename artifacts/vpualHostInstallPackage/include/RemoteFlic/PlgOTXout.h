// {% copyright %}
///
/// @file      PlgOTXout.h
/// @copyright All code copyright Movidius Ltd 2018, all rights reserved.
///            For License Warranty see: common/license.txt
///
/// @brief     Header for PlgOTXout Host FLIC plugin stub using VPUAL.
///
#ifndef __PLG_XLINK_OT_OUT_H__
#define __PLG_XLINK_OT_OUT_H__

#include <stdint.h>

#include "Flic.h"
#include "Message.h"
#include "PlgOT.h"
#ifndef XLINK_INVALID_CHANNEL_ID
#define XLINK_INVALID_CHANNEL_ID (0)
#endif

/**
 * Xlink Output FLIC Plugin Stub Class.
 * This object creates the real plugin on the device and links to it with an
 * XLink stream.
 */
class PlgOTXout : public PluginStub
{
  private:
    uint16_t channelID;

  public:
    /** Input message (this is a sink plugin). */
    SReceiver<vpuot::OutObjectsPtr> in;

    /** Constructor. */
    PlgOTXout(uint32_t device_id = 0) : PluginStub("PlgOTXout", device_id),
                    channelID(XLINK_INVALID_CHANNEL_ID)
                    {};

    /** Destructor. */
    ~PlgOTXout();

    /**
     * Plugin Create method.
     *
     * @param maxSz maximum size of the XLink Stream.
     * @param chanId_unused not used, just for API to be backward-compatible..
     */
    int Create(uint32_t maxSz, uint32_t chanId_unused);

    /**
     * Plugin Create method.
     *
     * @param maxSz maximum size of the XLink Stream.
     * @param chanId_unused not used, just for API to be backward-compatible..
     * @param streamId - stream_id is the unique id for the input video stream
     */
    int Create(uint32_t maxSz, uint32_t chanId_unused, int32_t streamId);

    /**
     * Plugin Delete method.
     *
     * Close the XLink stream.
     */
    virtual void Delete();

    /**
     * Pull a frame from the plugin.
     * This is not a remote method, it simply performs a blocking read on the
     * plugin's XLink stream.
     *
     * @param pAddr - Physical address of received frame.
     * @param nObjects - The number of tracked objects
     */
    int Pull(uint32_t *pAddr, uint32_t *nObjects);

    /**
     * Pull a frame from the plugin.
     * This is not a remote method, it simply performs a blocking read on the
     * plugin's XLink stream.
     *
     * @param pAddr - Physical address of received frame.
     * @param nObjects - The number of tracked objects
     */
    int Pull(uint32_t *pAddr, uint8_t *nObjects);
};

#endif // __PLG_XLINK_OUT_H__
