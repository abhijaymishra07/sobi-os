#include <sobi/types.h>
#include <sobi/compiler.h>
#include <limine.h>

__used __section(".limine_requests_start")
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__used __section(".limine_requests")
volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__used __section(".limine_requests")
volatile struct limine_bootloader_info_request limine_bootloader_info_req = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__used __section(".limine_requests")
volatile struct limine_executable_cmdline_request limine_cmdline_req = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__used __section(".limine_requests")
volatile struct limine_hhdm_request limine_hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__used __section(".limine_requests")
volatile struct limine_memmap_request limine_memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__used __section(".limine_requests")
volatile struct limine_executable_address_request limine_exec_addr_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = NULL,
};

__used __section(".limine_requests")
volatile struct limine_stack_size_request limine_stack_size_req = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .stack_size = 64 * 1024,
};

__used __section(".limine_requests_end")
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;
