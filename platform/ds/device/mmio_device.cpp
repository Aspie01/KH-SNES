// The device half of the MMIO seam: a volatile store, and nothing else.
//
// THIS FILE IS NOT BUILT HERE.  devkitPro is absent (tools/check_device.py), so
// platform/ds/host/Makefile.host deliberately excludes it and links
// mmio_host.cpp in its place.  It is committed anyway, because the alternative
// is that the device tier's other half gets written for the first time by
// somebody who also has to discover what shape it should be -- and because
// keeping it beside the recorder is what makes the two obviously the same
// three functions.
//
// It is kept this small ON PURPOSE.  Everything interesting about the
// initialisation -- which bank, which base, which byte, in which order -- is in
// init.cpp, which the host suite compiles and tests. What is untestable here is
// only "does a store to this address reach the register", and the way to keep
// that from hiding a defect is to make sure there is nothing else in the file
// for a defect to be in.  Three casts and three stores.
//
// It includes no libnds header.  There is nothing here libnds provides:
// `volatile uint8_t*` is the language, and the addresses come from
// platform/ds/include/vram_map.h, which is frozen and asserted.

#include "mmio.h"

namespace kh::device {

void Mmio::write8(uint32_t addr, uint8_t v) {
    *reinterpret_cast<volatile uint8_t*>(addr) = v;
}

void Mmio::write16(uint32_t addr, uint16_t v) {
    *reinterpret_cast<volatile uint16_t*>(addr) = v;
}

void Mmio::write32(uint32_t addr, uint32_t v) {
    *reinterpret_cast<volatile uint32_t*>(addr) = v;
}

}  // namespace kh::device
