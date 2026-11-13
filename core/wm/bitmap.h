// Coloured watermark composited into full-page images (internal to the wm module).
//
// Model: the pattern in the image is black / grey / white (achromatic) and a watermark of ONE colour W was blended over it:
//     pixel = (1 - a) * c * (1,1,1) + a * W          c = original grey level, a = watermark coverage (0..1)
// The chroma of a pixel (its distance from the grey axis) is then a * (W - mean(W)), i.e. it always points in one direction u,
// and its length tells a.  With |W - mean(W)| = K and mean(W) = mW (both fitted from the image itself: watermark over white
// paper and watermark over black lines), every pixel can be solved for c, so lines under the watermark come back intact.
// Pixels without chroma (the pattern itself) are never changed.
#pragma once
#include <qpdf/QPDFObjectHandle.hh>

#include <cstdint>
#include <string>
#include <vector>

namespace wm::bmp {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;   // w*h*3
    bool lossy = false;         // came from JPEG (DCT) / JPX
};
// Decodes an 8-bit DeviceRGB / ICCBased(3) / DeviceGray image XObject to RGB.  False (with a reason) for anything else.
bool decode(QPDFObjectHandle img, Image& out, std::string* why = nullptr);

struct Model {
    bool ok = false;
    double u[3] = {0, 0, 0};      // unit chroma direction of the watermark colour
    double K = 0, mW = 0;         // |W - mean(W)| and mean(W), 0..255 scale
    double hueShare = 0;          // share of coloured pixels that have the watermark's hue
    double colourShare = 0;       // share of all pixels that carry the watermark colour
    int lightSamples = 0, darkSamples = 0;
    std::string why;              // when !ok: why the colours cannot be separated (UTF-8, for the report)
    std::vector<double> pack() const { return {u[0], u[1], u[2], K, mW}; }
    static Model unpack(const std::vector<double>& v);
};

// Fits the model on (sampled) images.  `stride` subsamples pixels for speed.
Model fit(const std::vector<const Image*>& imgs, int stride = 2);

struct CleanStats { long changed = 0, protectedDark = 0; };
// Removes the watermark colour in place.  Achromatic dark pixels are left exactly as they were.
CleanStats clean(Image& img, const Model& m);

// Every large image XObject a page uses (directly or through forms, depth <= 3).
std::vector<QPDFObjectHandle> pageImages(QPDFObjectHandle page, long minPixels = 250000);

}  // namespace wm::bmp
