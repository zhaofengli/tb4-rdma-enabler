#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSIterator.h>

#define LOG(fmt, ...) IOLog("RDMAEnabler: " fmt "\n", ##__VA_ARGS__)

#define kIOThunderboltPortClass         "IOThunderboltPort"
#define kIOThunderboltLocalNodeClass    "IOThunderboltLocalNode"
#define kAppleThunderboltRDMAPortClass  "AppleThunderboltRDMAPort"
#define kSupportedLinkSpeedKey          "Supported Link Speed"

#define SPEED_GEN2 (1 << 3)     // 10 Gb/s per lane
#define SPEED_GEN3 (1 << 2)     // 20 Gb/s per lane, TB4
#define SPEED_GEN4 (1 << 1)     // 40 Gb/s per lane, TB5

#define super IOService

class RDMAEnabler : public IOService {
	OSDeclareDefaultStructors(RDMAEnabler)

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;

private:
	IONotifier *fPortNotifier {nullptr};

	static bool portPublished(void *target, void *refCon,
	    IOService *service, IONotifier *notifier);
	bool patchPort(IOService *port);
	void reregister(const char *className);
	void reregisterProviders(void);
};

OSDefineMetaClassAndStructors(RDMAEnabler, IOService)

bool
RDMAEnabler::patchPort(IOService *port)
{
	OSNumber *num = OSDynamicCast(OSNumber, port->getProperty(kSupportedLinkSpeedKey));
	if (!num) {
		return false;
	}

	uint32_t speed = num->unsigned32BitValue();
	if (speed & SPEED_GEN4) {
		return false;
	}

	port->setProperty(kSupportedLinkSpeedKey, speed | SPEED_GEN4, 32);
	LOG("%s: %s %u -> %u", port->getName(), kSupportedLinkSpeedKey,
	    speed, speed | SPEED_GEN4);
	return true;
}

void
RDMAEnabler::reregisterProviders(void)
{
	// 27.x AppleThunderboltRDMAPort::start (provider IOThunderboltLocalNode)
	reregister(kIOThunderboltLocalNodeClass);

	// 26.x AppleThunderboltRDMAInterface::start (provider AppleThunderboltRDMAPort)
	reregister(kAppleThunderboltRDMAPortClass);
}

void
RDMAEnabler::reregister(const char *className)
{
	OSDictionary *matching = IOService::serviceMatching(className);
	OSIterator *it = IOService::getMatchingServices(matching);
	OSSafeReleaseNULL(matching);
	if (!it) {
		return;
	}

	IOService *node;
	unsigned n = 0;
	while ((node = OSDynamicCast(IOService, it->getNextObject()))) {
		node->registerService();
		n++;
	}
	OSSafeReleaseNULL(it);
	LOG("re-registered %u %s", n, className);
}

bool
RDMAEnabler::portPublished(void *target, void *refCon,
    IOService *service, IONotifier *notifier)
{
	RDMAEnabler *self = (RDMAEnabler *)target;
	if (self->patchPort(service)) {
		self->reregisterProviders();
	}
	return true;
}

bool
RDMAEnabler::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	OSDictionary *matching = IOService::serviceMatching(kIOThunderboltPortClass);
	fPortNotifier = IOService::addMatchingNotification(
		gIOPublishNotification,
		matching,
		&RDMAEnabler::portPublished,
		this);
	OSSafeReleaseNULL(matching);

	if (!fPortNotifier) {
		LOG("could not register the %s notification", kIOThunderboltPortClass);
		return false;
	}

	LOG("watching for %s", kIOThunderboltPortClass);
	registerService();
	return true;
}

void
RDMAEnabler::stop(IOService *provider)
{
	if (fPortNotifier) {
		fPortNotifier->remove();
		fPortNotifier = nullptr;
	}
	super::stop(provider);
}

extern "C" {
#include <mach/mach_types.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default")))
KMOD_EXPLICIT_DECL(li.zhaofeng.RDMAEnabler, "1.0", _start, _stop)

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
}
