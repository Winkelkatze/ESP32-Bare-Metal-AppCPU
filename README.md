# ESP32-Bare-Metal-AppCPU
Experimental code to launch the start the second core of a ESP32 manually so it runs without scheduler. See [https://hackaday.io/project/174521-bare-metal-second-core-on-esp32].

## !! Highly experimental !!
This code is highly experimental, I can't guarantee it works as intended. I also can't guarantee it won't corrupt the data of the code running on the PRO CPU. The whole approach is somewhat hacky so even if it works for now, there may be problems with future versions of the SDK / the ROM.

## Setup
First of all, you will need to build the SDK in single core mode. Add `CONFIG_FREERTOS_UNICORE` to the `sdkconfig.h` or configure this in the config menu.

Because of some weirdness of the (internal) startup of the APP CPU, it corrupts the heap when started. The SDK solves this by starting the APP CPU before the heap is initialized. I couldn't find a workaround for that, so in order to use this code, you need to modify the SDK!

In the SDK function
```
call_start_cpu0()
in the file
components/esp32/cpu_start.c
```
add
```
init_app_cpu_baremetal()
```
immediately before
```
heap_caps_init()
```
is called.

## Usage
When `start_app_cpu()` is called, the App CPU will execute the `app_cpu_main`. After the main finishes / returns, the App core is halted and can be started again by calling `start_app_cpu()` again.

## Limitations
### No flash access at all!
- No code that is not in the IRAM. So all called functions need to have the `IRAM_ATTR` attribute.
- No access to constants that are stored in the flash (like strings).
- Most library functions are also not in the IRAM.

### No synchronization
FreeRTOS synchronization things won't work either!

### Best don't call any library functions at all!
