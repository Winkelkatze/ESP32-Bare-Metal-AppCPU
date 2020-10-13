#include <soc/dport_reg.h>
#include <soc/cpu.h>
#include <esp_heap_caps.h>
#include <esp32/rom/ets_sys.h>
#include <esp32/rom/uart.h>
#include <soc/soc_memory_layout.h>
#include <stdio.h>
#include "esp32/rom/cache.h"

#define BAREMETAL_APP_CPU_DEBUG 0

// Reserve static data for built-in ROM functions
#define APP_CPU_RESERVE_ROM_DATA 1

#ifndef APP_CPU_STACK_SIZE
#define APP_CPU_STACK_SIZE 1024
#endif

#ifndef CONFIG_FREERTOS_UNICORE
#error Bare metal app core use requires UNICORE build
#endif

// Interrupt vector, this comes from the SDK
extern int _init_start;

static volatile uint32_t app_cpu_stack_ptr;
static volatile uint8_t app_cpu_initial_start;

#ifdef APP_CPU_RESERVE_ROM_DATA
SOC_RESERVE_MEMORY_REGION(0x3ffe3f20, 0x3ffe4350, rom_app_data);
#endif



#ifdef APP_CPU_RESERVE_ROM_DATA
// Can't load from flash, therefore the string needs to be placed in the RAM
static char hello_world[] = "Hello World!\n";
#endif

// APP CPU cache is part of the main memory pool and we can't get the caches to work easily anyway because cache loads need to be synchronized.
// So, for now the app core can only execute from IRAM (and internal ROM).
// Also, the APP CPU CANNOT load data from the flash!
static void IRAM_ATTR app_cpu_main()
{
	// User code goes here
#ifdef APP_CPU_RESERVE_ROM_DATA
	ets_printf(hello_world);
#endif
}

// The main MUST NOT be inlined!
// Otherwise, it will cause mayhem on the stack since we are modifying the stack pointer before the main is called.
// Having a volatile pointer around should force the compiler to behave.
static void (* volatile app_cpu_main_ptr)() = &app_cpu_main;

static void IRAM_ATTR app_cpu_init()
{
	// init interrupt handler
	asm volatile (\
		"wsr    %0, vecbase\n" \
		::"r"(&_init_start));

    cpu_configure_region_protection();
    cpu_init_memctl();

#ifdef APP_CPU_RESERVE_ROM_DATA
	uartAttach();
    ets_install_uart_printf();
    uart_tx_switch(CONFIG_ESP_CONSOLE_UART_NUM);
#endif

	app_cpu_initial_start = 1;
	while(1)
	{
		// This will halt the CPU until it is needed
		DPORT_REG_CLR_BIT(DPORT_APPCPU_CTRL_B_REG, DPORT_APPCPU_CLKGATE_EN);

		// clock cpu will still execute 1 instruction after the clock gate is turned off.
		// so just have some NOPs here, so the stack pointer will be correct
		asm volatile (						\
			"nop\n"							\
			"nop\n"							\
			"nop\n"							\
			"nop\n"							\
			"nop\n"							\
	    );

		// load the new stack pointer for our main
		// this is VERY important, since the initial stack pointer now points somewhere in the heap!
		asm volatile (						\
			"l32i a1, %0, 0\n"				\
			::"r"(&app_cpu_stack_ptr));

		app_cpu_main();
	}
}


bool start_app_cpu()
{
#if BAREMETAL_APP_CPU_DEBUG
	printf("App main at %08X\n", (uint32_t)&app_cpu_main);
	printf("App init at %08X\n", (uint32_t)&app_cpu_init);
	printf("APP_CPU RESET: %u\n", DPORT_READ_PERI_REG(DPORT_APPCPU_CTRL_A_REG));
	printf("APP_CPU CLKGATE: %u\n", DPORT_READ_PERI_REG(DPORT_APPCPU_CTRL_B_REG));
	printf("APP_CPU STALL: %u\n", DPORT_READ_PERI_REG(DPORT_APPCPU_CTRL_C_REG));
#endif

	if (!app_cpu_initial_start)
	{
		printf("APP CPU was not initialized!\n");
		return false;
	}

	if (DPORT_REG_GET_BIT(DPORT_APPCPU_CTRL_B_REG, DPORT_APPCPU_CLKGATE_EN))
	{
		printf("APP CPU is already running!\n");
		return false;
	}

	if (!app_cpu_stack_ptr)
	{
		app_cpu_stack_ptr = (uint32_t)heap_caps_malloc(APP_CPU_STACK_SIZE, MALLOC_CAP_DMA);
		app_cpu_stack_ptr += APP_CPU_STACK_SIZE - sizeof(size_t);
	}

#if BAREMETAL_APP_CPU_DEBUG
	printf("APP CPU STACK PTR: %08X\n", (uint32_t)app_cpu_stack_ptr);
#endif

	DPORT_SET_PERI_REG_MASK(DPORT_APPCPU_CTRL_B_REG, DPORT_APPCPU_CLKGATE_EN);
	return true;
}


/*
 * Initializes the app cpu. This needs to be called before the heap is initialized (heap_caps_init()).
 * Insert a call to this into the cpu_start.c file found in the SDK.
 */
void init_app_cpu_baremetal()
{
	app_cpu_initial_start = 0;

	for (int i = ETS_WIFI_MAC_INTR_SOURCE; i <= ETS_CACHE_IA_INTR_SOURCE; i++) {
        intr_matrix_set(1, i, ETS_INVALID_INUM);
    }

	DPORT_REG_SET_BIT(DPORT_APPCPU_CTRL_A_REG, DPORT_APPCPU_RESETTING);
	DPORT_REG_CLR_BIT(DPORT_APPCPU_CTRL_A_REG, DPORT_APPCPU_RESETTING);

	DPORT_WRITE_PERI_REG(DPORT_APPCPU_CTRL_D_REG, ((uint32_t)&app_cpu_init));
	DPORT_SET_PERI_REG_MASK(DPORT_APPCPU_CTRL_B_REG, DPORT_APPCPU_CLKGATE_EN);

	while(!app_cpu_initial_start){}
}

