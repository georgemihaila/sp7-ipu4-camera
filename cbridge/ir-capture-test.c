#include "ir-v4l2.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *message)
{
	fprintf(stderr, "test-ir: %s\n", message);
	return EXIT_FAILURE;
}

static void pack_group(uint8_t *packed, const uint16_t *pixels)
{
	packed[4] = 0U;
	for (unsigned index = 0U; index < 4U; index++) {
		packed[index] = (uint8_t)(pixels[index] >> 2U);
		packed[4] |= (uint8_t)((pixels[index] & 0x03U) << (index * 2U));
	}
}

int main(void)
{
	const unsigned width = 8U;
	const unsigned height = 2U;
	const unsigned stride = 32U;
	const unsigned offset = 4U;
	const size_t required = (size_t)offset + (height - 1U) * stride +
		IR_CAPTURE_HEADER_BYTES + width * 10U / 8U;
	const size_t frame_size = (size_t)offset + height * stride;
	uint8_t *frame = calloc(1U, frame_size);
	uint8_t output[width * height * 2U];
	uint16_t pixels[2][8] = {
		{ 0U, 1U, 2U, 3U, 252U, 511U, 768U, 1023U },
		{ 12U, 34U, 56U, 78U, 123U, 456U, 789U, 999U },
	};
	char error[256];

	if (frame == NULL)
		return fail("frame allocation failed");
	for (unsigned row = 0U; row < height; row++) {
		uint8_t *line = frame + offset + (size_t)row * stride;

		line[0] = 0x40U;
		for (unsigned group = 0U; group < width / 4U; group++)
			pack_group(line + IR_CAPTURE_HEADER_BYTES + group * 5U,
				pixels[row] + group * 4U);
	}
	if (ir_decode_raw10_to_yuyv(frame, frame_size, width, height, stride,
		offset, output, sizeof(output), error, sizeof(error)) != 0)
		return fail(error);
	for (unsigned row = 0U; row < height; row++) {
		for (unsigned pixel = 0U; pixel < width; pixel++) {
			size_t position = ((size_t)row * width + pixel) * 2U;

			if (output[position] != (uint8_t)(pixels[row][pixel] >> 2U) ||
				output[position + 1U] != 0x80U)
				return fail("decoded YUYV pixels or neutral chroma are wrong");
		}
	}
	if (ir_decode_raw10_to_yuyv(frame, required - 1U, width, height, stride,
		offset, output, sizeof(output), error, sizeof(error)) == 0)
		return fail("truncated frame was accepted");
	frame[offset] = 0U;
	if (ir_decode_raw10_to_yuyv(frame, frame_size, width, height, stride, offset,
		output, sizeof(output), error, sizeof(error)) == 0)
		return fail("bad CSI line header was accepted");
	free(frame);
	puts("test-ir: PASS");
	return EXIT_SUCCESS;
}
