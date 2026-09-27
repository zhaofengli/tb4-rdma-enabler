# RDMA Enabler for Thunderbolt 4 Macs

Apple only officially supports [RDMA over Thunderbolt 5](https://developer.apple.com/documentation/technotes/tn3205-low-latency-communication-with-rdma-over-thunderbolt), which is available on some Pro/Max/Ultra variants of Apple silicon. The functionality is physically present on the Thunderbolt 4 controller but is intentionally disabled in AppleThunderboltRDMA.kext. This repo explains how to enable RDMA on those Macs at 40Gbps. Tested on macOS 27.2.

> [!WARNING]
> This is unsupported and will weaken the security of your system!

## Prerequisites

```console
# Kext signing must be disabled
$ csrutil status | grep "Kext"
	Kext Signing: disabled

# Security mode must be Permissive and AuxKC must be enabled
$ sudo bputil -d | grep "smb[012]"
Security Mode:               Permissive (smb0 && smb1): 1
3rd Party Kexts Status:      Enabled    (smb2): 1

# RDMA must be enabled (normally has no effect on TB4 Macs)
$ rdma_ctl status
enabled

# ... and yes, please get Xcode
$ xcode-select -p
/Applications/Xcode.app/Contents/Developer
```

If not, the following needs to be done in recoveryOS:

1. Enter recoveryOS: Power off Mac > Press and hold the power button > Options
2. Allow unsigned kernel extensions: Utilities > Terminal > `csrutil enable --without kext`
3. Enable kernel extensions: Utilities > Startup Security Utility
    - Make sure "Permissive Security" is selected
    - Check "Allow user management of kernel extensions from identified developers"
4. Enable RDMA: Utilities > Terminal > `rdma_ctl enable`

## Guide

### 1. Build and install RDMAEnabler

```bash
make install

# ... or if you prefer doing this manually
make
sudo cp -R ./build/RDMAEnabler.kext /Library/Extensions/
sudo chown -R root:wheel /Library/Extensions/RDMAEnabler.kext

kmutil print-diagnostics -p /Library/Extensions/RDMAEnabler.kext
```

`Bad code signature` is okay (reminder that you have to disable kext signing), but if you see the following, follow the next step:

```
Error: Unable to resolve dependencies: 'li.zhaofeng.RDMAEnabler' names a dependency on 'com.apple.iokit.IORDMAFamily', which was not found.
```

### 2. (Certain Macs Only) Install AppleThunderboltRDMA and IORDMAFamily

Some TB4 Macs (e.g., M4 Mac mini) don't ship AppleThunderboltRDMA or IORDMAFamily in the kernelcache, so you will need to pull them from a KDK:

```bash
# Check whether AppleThunderboltRDMA or IORDMAFamily are in the BootKC
# If so, skip the rest of this
kmutil showloaded --collection boot | grep -i rdma

# Get the corresponding KDK for your build:
# - https://github.com/dortania/KdkSupportPkg/releases
# - https://developer.apple.com/download/all/?q=Kernel%20Debug%20Kit
cd /Library/Developer/KDKs/KDK_27.2_26B5091g.kdk/System/Library/Extensions

sudo cp -R AppleThunderboltRDMA.kext IORDMAFamily.kext /Library/Extensions/

# com.apple. kexts have to opt into being included in the AuxKC
sudo plutil -replace OSBundleRequired -string Auxiliary /Library/Extensions/AppleThunderboltRDMA.kext/Contents/Info.plist
sudo plutil -replace OSBundleRequired -string Auxiliary /Library/Extensions/IORDMAFamily.kext/Contents/Info.plist

sudo codesign -f -s - /Library/Extensions/AppleThunderboltRDMA.kext /Library/Extensions/IORDMAFamily.kext
```

### 3. Rebuild AuxKC and reboot

Note that if you skipped step 3, only `li.zhaofeng.RDMAEnabler` will listed.

```console
$ sudo kmutil rebuild
Checking the auxiliary kernel collection...
Attempting to add the following extensions (3):
[+] com.apple.driver.AppleThunderboltRDMA	0.0.1	/Library/Extensions/AppleThunderboltRDMA.kext
[+] li.zhaofeng.RDMAEnabler	1.0	/Library/Extensions/RDMAEnabler.kext
[+] com.apple.iokit.IORDMAFamily	1.0	/Library/Extensions/IORDMAFamily.kext
Requesting user approval (times out in 60 seconds)...
A system reboot is required for the operation to take effect
```

### 4. Verify

Connect a Thunderbolt 5 cable between two Macs. `ibv_devinfo -v` will report the unconnected ports as having `8X` and `10.0 Gbps`, but the connected ones will correctly report 40Gbps:

```console
$ ibv_devinfo -v | grep -E "hca_id|state|active_width|active_speed"
hca_id:    rdma_en2
                        state:                     PORT_DOWN (1)
                        active_width:              8X (4)
                        active_speed:              10.0 Gbps (4)
hca_id:    rdma_en3
                        state:                     PORT_DOWN (1)
                        active_width:              8X (4)
                        active_speed:              10.0 Gbps (4)
hca_id:    rdma_en4
                        state:                     PORT_ACTIVE (4)
                        active_width:              4X (2)
                        active_speed:              10.0 Gbps (4)
```

On the other Mac, find the corresponding port and start a `ibv_uc_pingpong` server (macOS set up a Thunderbolt bridge by default):

```console
$ ibv_devinfo -v | grep -E "hca_id|state|active_width|active_speed"
[...]
hca_id: rdma_en9
                        state:                  PORT_ACTIVE (4)
                        active_width:           4X (2)
                        active_speed:           10.0 Gbps (4)
[...]

$ ifconfig bridge0 | grep inet6
        inet6 fe80::8ad:a6c6:6857:65f6%bridge0 prefixlen 64 secured scopeid 0x11

# Latency
$ ibv_uc_pingpong -d rdma_en9 --size=128 --iters=1000000
  local address:  LID 0x0002, QPN 0x000910, PSN 0xc01924, GID ::
  
# Throughput
$ ibv_uc_pingpong -d rdma_en4 'fe80::8ad:a6c6:6857:65f6%bridge0' --size=4190208 --iters=512 --rx-depth=1
```

On the TB4 Mac, connect with the same parameters (`--size`, `--iters`, `--rx-depth`):

```
# Latency
$ ibv_uc_pingpong -d rdma_en4 'fe80::8ad:a6c6:6857:65f6%bridge0' --size=128 --iters=1000000
  local address:  LID 0x0004, QPN 0x000930, PSN 0xc57b11, GID ::
  remote address: LID 0x0002, QPN 0x000910, PSN 0xc01924, GID ::
256000000 bytes in 5.97 seconds = 342.97 Mbit/sec
1000000 iters in 5.97 seconds = 5.97 usec/iter

# Throughput
$ ibv_uc_pingpong -d rdma_en4 'fe80::8ad:a6c6:6857:65f6%bridge0' --size=4190208 --iters=512 --rx-depth=1
  local address:  LID 0x0004, QPN 0x000930, PSN 0xc8013d, GID ::
  remote address: LID 0x0002, QPN 0x000910, PSN 0x804b16, GID ::
4290772992 bytes in 0.92 seconds = 37403.20 Mbit/sec
512 iters in 0.92 seconds = 1792.45 usec/iter
```

For reference, here's what an officially supported configuration gets over Thunderbolt 5. I got the following between an M4 Pro MacBook Pro (client) and an M5 Max Mac Studio (server):

```
# For comparison only: The following are TB5 numbers!

$ ibv_uc_pingpong -d rdma_en1 'fe80::8ad:a6c6:6857:65f6%bridge0' --size=128 --iters=1000000
  local address:  LID 0x0001, QPN 0x000900, PSN 0xc0c475, GID ::
  remote address: LID 0x0002, QPN 0x000910, PSN 0x2fc97f, GID ::
256000000 bytes in 6.50 seconds = 315.00 Mbit/sec
1000000 iters in 6.50 seconds = 6.50 usec/iter

$ ibv_uc_pingpong -d rdma_en1 'fe80::8ad:a6c6:6857:65f6%bridge0' --size=4190208 --iters=512 --rx-depth=1
  local address:  LID 0x0001, QPN 0x000900, PSN 0xd75347, GID ::
  remote address: LID 0x0002, QPN 0x000910, PSN 0xbf4855, GID ::
4290772992 bytes in 0.47 seconds = 72807.18 Mbit/sec
512 iters in 0.47 seconds = 920.83 usec/iter
```

## Explanation

AppleThunderboltRDMA effectively does the following check on the "Supported Link Speed" property and bails if the port does not support Gen4/80Gbps:

```c
#define SPEED_GEN2 (1 << 3)
#define SPEED_GEN3 (1 << 2)
#define SPEED_GEN4 (1 << 1)

#define kSupportedLinkSpeedKey "Supported Link Speed"

bool
isSupportedThunderboltSpeed(unsigned int speed)
{
	bool supported = isDarwinOSBootEnvironment() || ((speed & SPEED_GEN4) != 0);
	if (!supported) {
		os_log(OS_LOG_DEFAULT, "AppleThunderboltRDMA::isSupportedThunderboltSpeed(%d) - Thunderbolt controller does not support Thunderbolt 5, RDMA not supported. Exiting..\n", speed);
	}
	return supported;
}

bool AppleThunderboltRDMAPort::start(IOService *provider)
{
	// ...
	OSNumber *speed = OSDynamicCast(OSNumber, port->getProperty(kSupportedLinkSpeedKey));
	if (speed) {
		if (isSupportedThunderboltSpeed(speed->unsigned16BitValue())) {
			// ...
		}
		// ...
	}
	// ...
}
```

Instead of patching AppleThunderboltRDMA itself, I made a simple kext, RDMAEnabler, to set the "Supported Link Speed" property and re-trigger AppleThunderboltRDMA initialization.
