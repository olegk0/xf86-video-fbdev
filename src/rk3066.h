#define FBIOGET_OVERLAY_STATE   0X4619
#define FBIOSET_OVERLAY_STATE   0x5018
#define FBIOSET_ENABLE          0x5019
#define FBIOGET_ENABLE          0x5020

#define FBIOSET_YUV_ADDR 0x5002

enum {
    RGBA_8888          = 1,
    RGBX_8888          = 2,
    RGB_888            = 3,
    RGB_565            = 4,
    /* Legacy formats (deprecated), used by ImageFormat.java */
    YCbCr_422_SP       = 0x10, // NV16	16
    YCrCb_NV12         = 0x20, // YUY2	32
    YCrCb_444          = 0x22, //yuv444 34
};
