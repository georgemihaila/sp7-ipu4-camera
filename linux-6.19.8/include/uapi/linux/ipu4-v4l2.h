/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Intel IPU4 vector pixel formats retained by the external IPU4 driver. */
#ifndef _UAPI_LINUX_IPU4_V4L2_H
#define _UAPI_LINUX_IPU4_V4L2_H

#include <linux/videodev2.h>

#define V4L2_PIX_FMT_SBGGR8_16V32 v4l2_fourcc('b', 'V', '0', 'A')
#define V4L2_PIX_FMT_SGBRG8_16V32 v4l2_fourcc('b', 'V', '0', 'B')
#define V4L2_PIX_FMT_SGRBG8_16V32 v4l2_fourcc('b', 'V', '0', 'C')
#define V4L2_PIX_FMT_SRGGB8_16V32 v4l2_fourcc('b', 'V', '0', 'D')

#define V4L2_PIX_FMT_SBGGR10V32 v4l2_fourcc('b', 'V', '0', 'E')
#define V4L2_PIX_FMT_SGBRG10V32 v4l2_fourcc('b', 'V', '0', 'F')
#define V4L2_PIX_FMT_SGRBG10V32 v4l2_fourcc('b', 'V', '0', 'G')
#define V4L2_PIX_FMT_SRGGB10V32 v4l2_fourcc('b', 'V', '0', 'H')

#define V4L2_PIX_FMT_SBGGR12V32 v4l2_fourcc('b', 'V', '0', 'I')
#define V4L2_PIX_FMT_SGBRG12V32 v4l2_fourcc('b', 'V', '0', 'J')
#define V4L2_PIX_FMT_SGRBG12V32 v4l2_fourcc('b', 'V', '0', 'K')
#define V4L2_PIX_FMT_SRGGB12V32 v4l2_fourcc('b', 'V', '0', 'L')

#define V4L2_PIX_FMT_SBGGR14V32 v4l2_fourcc('b', 'V', '0', 'M')
#define V4L2_PIX_FMT_SGBRG14V32 v4l2_fourcc('b', 'V', '0', 'N')
#define V4L2_PIX_FMT_SGRBG14V32 v4l2_fourcc('b', 'V', '0', 'O')
#define V4L2_PIX_FMT_SRGGB14V32 v4l2_fourcc('b', 'V', '0', 'P')

#define V4L2_PIX_FMT_YUYV420_V32 v4l2_fourcc('y', '0', '3', '2')

#endif /* _UAPI_LINUX_IPU4_V4L2_H */
