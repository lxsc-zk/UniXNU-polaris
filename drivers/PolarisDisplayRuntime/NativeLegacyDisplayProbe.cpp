// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// native_legacy_display_probe / native_legacy_display_late_probe /
// native_mobile_graphics_client_probe
//
// build-xnu-4k-baseline.sh calls these three symbols from bsd_init.o and
// IOUserClient.cpo when XNU_NATIVE_LEGACY_DISPLAY=1. polaris has no upstream
// legacy display package, and its own PolarisDisplayRuntime needs a live
// IOService GOP nub that does not exist yet at bsd_init time. These probes
// therefore paint straight into the framebuffer DarwinBoot handed over
// (PE_state.video), so a 4 KiB polaris kernel can report that it reached
// bsd_init and bsd_autoconf instead of handing off invisibly.

#include <pexpert/pexpert.h>

extern "C" {

int native_legacy_display_probe(void);
int native_legacy_display_late_probe(void);
int native_mobile_graphics_client_probe(void);

}

namespace {

struct Frame {
	uint8_t *base;
	uint32_t width;
	uint32_t height;
	uint32_t row_bytes;
	uint32_t depth;
};

// 5x7 font, column-major bitmaps, only the glyphs this probe prints.
struct Glyph {
	char c;
	uint8_t col[5];
};

constexpr Glyph kGlyphs[] = {
	{ ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
	{ '.', { 0x00, 0x00, 0x40, 0x00, 0x00 } },
	{ ':', { 0x00, 0x24, 0x00, 0x00, 0x00 } },
	{ '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
	{ '=', { 0x14, 0x14, 0x14, 0x14, 0x14 } },
	{ '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
	{ '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
	{ '2', { 0x62, 0x51, 0x49, 0x49, 0x46 } },
	{ '3', { 0x22, 0x41, 0x49, 0x49, 0x36 } },
	{ '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
	{ '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
	{ '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } },
	{ '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
	{ '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
	{ '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
	{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
	{ 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
	{ 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
	{ 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
	{ 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
	{ 'F', { 0x7F, 0x09, 0x09, 0x01, 0x01 } },
	{ 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } },
	{ 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
	{ 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
	{ 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
	{ 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
	{ 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
	{ 'M', { 0x7F, 0x02, 0x04, 0x02, 0x7F } },
	{ 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
	{ 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
	{ 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
	{ 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } },
	{ 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
	{ 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
	{ 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
	{ 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
	{ 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
	{ 'W', { 0x7F, 0x20, 0x18, 0x20, 0x7F } },
	{ 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
	{ 'Y', { 0x03, 0x04, 0x78, 0x04, 0x03 } },
	{ 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
};

Frame gFrame;
uint32_t gLine;

void putPixel(uint32_t x, uint32_t y, uint32_t value)
{
	if (!gFrame.base || x >= gFrame.width || y >= gFrame.height) {
		return;
	}
	uint8_t *row = gFrame.base + (uint64_t)y * gFrame.row_bytes;
	if (gFrame.depth == 32) {
		*reinterpret_cast<uint32_t *>(row + x * 4u) = value;
	} else if (gFrame.depth == 16) {
		*reinterpret_cast<uint16_t *>(row + x * 2u) = static_cast<uint16_t>(value);
	}
}

const uint8_t *glyphFor(char c)
{
	for (const Glyph &glyph : kGlyphs) {
		if (glyph.c == c) {
			return glyph.col;
		}
	}
	return kGlyphs[0].col;
}

void drawText(uint32_t x, uint32_t y, const char *text, uint32_t scale, uint32_t value)
{
	uint32_t cursor = x;
	for (const char *p = text; *p; ++p) {
		char c = *p;
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
		const uint8_t *columns = glyphFor(c);
		for (uint32_t cx = 0; cx < 5; ++cx) {
			uint8_t bits = columns[cx];
			for (uint32_t cy = 0; cy < 7; ++cy) {
				if (!(bits & (1u << cy))) {
					continue;
				}
				for (uint32_t sy = 0; sy < scale; ++sy) {
					for (uint32_t sx = 0; sx < scale; ++sx) {
						putPixel(cursor + cx * scale + sx, y + cy * scale + sy, value);
					}
				}
			}
		}
		cursor += 6 * scale;
	}
}

void appendHex(char *buffer, uint32_t &at, uint64_t value)
{
	static const char kDigits[] = "0123456789ABCDEF";
	char scratch[17];
	uint32_t count = 0;
	do {
		scratch[count++] = kDigits[value & 0xf];
		value >>= 4;
	} while (value && count < sizeof(scratch));
	buffer[at++] = '0';
	buffer[at++] = 'X';
	while (count && at < 63) {
		buffer[at++] = scratch[--count];
	}
}

void appendDecimal(char *buffer, uint32_t &at, uint64_t value)
{
	char scratch[21];
	uint32_t count = 0;
	do {
		scratch[count++] = static_cast<char>('0' + (value % 10));
		value /= 10;
	} while (value && count < sizeof(scratch));
	while (count && at < 63) {
		buffer[at++] = scratch[--count];
	}
}

void drawLine(const char *text)
{
	if (!gFrame.base) {
		return;
	}
	uint32_t scale = (gFrame.height >= 1000) ? 4u : 2u;
	uint32_t lineHeight = 7u * scale + 10u * scale;
	uint32_t y = gFrame.height / 3u + gLine * lineHeight;
	drawText(gFrame.width / 12u, y, text, scale, 0x0000FF00u);
	gLine++;
}

void prepare()
{
	if (gFrame.base) {
		return;
	}
	gFrame.base = reinterpret_cast<uint8_t *>(PE_state.video.v_baseAddr);
	gFrame.width = PE_state.video.v_width;
	gFrame.height = PE_state.video.v_height;
	gFrame.row_bytes = PE_state.video.v_rowBytes;
	gFrame.depth = PE_state.video.v_depth;
}

} // namespace

extern "C" int
native_legacy_display_probe(void)
{
	prepare();
	drawLine("UNIXNU 4K KERNEL");
	drawLine("DISPLAY PROBE OK");

	char buffer[64];
	uint32_t at = 0;
	buffer[at++] = 'F';
	buffer[at++] = 'B';
	buffer[at++] = ' ';
	appendHex(buffer, at, reinterpret_cast<uint64_t>(gFrame.base));
	buffer[at] = 0;
	drawLine(buffer);

	at = 0;
	appendDecimal(buffer, at, gFrame.width);
	buffer[at++] = 'X';
	appendDecimal(buffer, at, gFrame.height);
	buffer[at++] = ' ';
	buffer[at++] = 'R';
	appendDecimal(buffer, at, gFrame.row_bytes);
	buffer[at++] = ' ';
	buffer[at++] = 'D';
	appendDecimal(buffer, at, gFrame.depth);
	buffer[at] = 0;
	drawLine(buffer);
	return 0;
}

extern "C" int
native_legacy_display_late_probe(void)
{
	prepare();
	drawLine("LATE PROBE OK");
	drawLine("BSD INIT COMPLETE");
	return 0;
}

extern "C" int
native_mobile_graphics_client_probe(void)
{
	return 0;
}
