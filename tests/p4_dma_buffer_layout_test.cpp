#include "p4_dma_buffer_layout.h"
#include <cassert>
#include <initializer_list>
#include <limits>

int main() {
  P4DmaBufferLayout out;
  for (size_t bytes : {size_t(1536), size_t(4608), size_t(18432), size_t(30720)}) {
    assert(p4DmaBufferLayout(bytes, 64, 128, out));
    assert(out.alignment == 128 && out.bytes == bytes);
  }
  assert(p4DmaBufferLayout(1601, 64, 128, out) && out.bytes == 1664);
  assert(p4DmaBufferLayout(1601, 256, 128, out) && out.alignment == 256 && out.bytes == 1792);
  assert(!p4DmaBufferLayout(std::numeric_limits<size_t>::max(), 64, 128, out));
  assert(!p4DmaBufferLayout(0, 64, 128, out));
  assert(!p4DmaBufferLayout(1536, 192, 128, out));
  assert(out.bytes == 0 && out.alignment == 0);
}
