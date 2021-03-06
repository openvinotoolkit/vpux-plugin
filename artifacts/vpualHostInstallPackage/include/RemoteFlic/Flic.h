// {% copyright %}
///
/// @file      Flic.h
/// @copyright All code copyright Movidius Ltd 2018, all rights reserved.
///            For License Warranty see: common/license.txt
///
/// @brief     Header for Host side FLIC usage over VPUAL.

#ifndef __FLIC_H__
#define __FLIC_H__

#include <string>
#include <stdint.h>
#include <list>

#include "VpualDispatcher.h"

#define MAX_PLUGS_PER_PIPE (32)

/** Forward declarations. */
class Message;
class PluginStub;

/**
 * Pipeline Stub Class.
 * This object looks like the real FLIC pipeline object on the device, but it is
 * a stub and all method calls are remote.
 */
class Pipeline : private VpualStub
{
  public:
    Pipeline(int maxPlugs = MAX_PLUGS_PER_PIPE, uint32_t device_id = 0);

    virtual void Add(PluginStub *plg, const char *name = NULL);

    virtual void Delete();
    virtual void Start ();
    virtual void Stop  ();
    virtual void Wait  ();
    int          Has   (PluginStub *plg);

  private:
    std::list<PluginStub *> plugins;
};

/**
 * Base class for all Plugin Stubs.
 * Similar to ThreadedPlugin or IPlugin in the FLIC framework.
 */
class PluginStub : public VpualStub
{
  private:
    uint32_t io_count = 0;

  public:
    /** Constructor just invokes the parent constructor. */
    PluginStub(std::string type, uint32_t device_id = 0) : VpualStub(type, device_id){};

    virtual void Stop  () {};   // By default do nothing.
    virtual void Delete() {};   // By default do nothing.
    virtual void Wait  () {};   // By default do nothing.

  protected:
    /** Add Messages to the Plugin (in "Create" function typically). */
    void Add(Message *s);
    void ClearMessages();
};

#endif // __FLIC_H__
