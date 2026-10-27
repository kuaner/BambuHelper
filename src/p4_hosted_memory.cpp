#include "p4_hosted_memory.h"
#if defined(BOARD_IS_WS_P4_7B)
#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_private/esp_cache_private.h>
#include "p4_dma_buffer_layout.h"
extern "C" {
// Arduino exports Hosted's include/ directory; the OS adapter lives alongside
// it. Keep the actual pinned SDK declarations instead of duplicating their ABI.
#include "../host/port/esp/freertos/include/port_esp_hosted_host_config.h"
#include "../host/esp_hosted_os_abstraction.h"
#include "../host/esp_hosted_host_fw_ver.h"
}

// The adapter relies on this version's RX task, allocation and free contract.
// Updating Arduino/Hosted requires revisiting it, rather than silently routing
// an unknown driver's TX pool to incorrectly aligned PSRAM blocks.
static_assert(ESP_HOSTED_VERSION_MAJOR_1 == 2 && ESP_HOSTED_VERSION_MINOR_1 == 12 &&
              ESP_HOSTED_VERSION_PATCH_1 == 11, "Revalidate P4 Hosted RX allocation adapter");
namespace {
using Allocator = void* (*)(size_t, size_t);
Allocator originalAllocate = nullptr;
size_t psramAlignment = 0;
std::atomic<uint32_t> psramAllocations{0}, psramFailures{0}, largestRxRequest{0};

void* allocate(size_t bytes, size_t alignment) {
  if (bytes < 1536 || std::strcmp(pcTaskGetName(nullptr), "sdio_read") != 0)
    return originalAllocate(bytes, alignment);
  // Only streaming RX: changing the 1600-byte-stride TX pool would violate
  // the P4's 128-byte PSRAM cache-line alignment for every second block.
  P4DmaBufferLayout layout;
  if (!p4DmaBufferLayout(bytes, alignment, psramAlignment, layout)) return nullptr;
  void* memory = heap_caps_aligned_alloc(layout.alignment, layout.bytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (memory) {
    psramAllocations.fetch_add(1, std::memory_order_relaxed);
    uint32_t previous = largestRxRequest.load(std::memory_order_relaxed);
    while (bytes > previous && !largestRxRequest.compare_exchange_weak(previous, bytes,
           std::memory_order_relaxed)) {}
  } else {
    psramFailures.fetch_add(1, std::memory_order_relaxed);
    return originalAllocate(bytes, alignment);
  }
  return memory;
}
}

void p4HostedMemoryStart() {
  if (originalAllocate) return;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &psramAlignment) != ESP_OK ||
      !psramAlignment || !g_hosted_osi_funcs._h_malloc_align) {
    Serial.println("[hosted-memory] RX PSRAM initialization failed");
    return;
  }
  originalAllocate = g_hosted_osi_funcs._h_malloc_align;
  g_hosted_osi_funcs._h_malloc_align = allocate;
  Serial.printf("[hosted-memory] RX PSRAM allocator installed alignment=%u\n", (unsigned)psramAlignment);
}

void p4HostedMemoryLog() {
  Serial.printf("[hosted-memory] rx_psram=%lu largest_request=%lu failures=%lu dma_free=%u dma_largest=%u\n",
                (unsigned long)psramAllocations.load(std::memory_order_relaxed),
                (unsigned long)largestRxRequest.load(std::memory_order_relaxed),
                (unsigned long)psramFailures.load(std::memory_order_relaxed),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
}
#endif
