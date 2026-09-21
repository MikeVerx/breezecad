// svg2ico.cpp — компактный конвертер SVG → ICO на lunasvg.
// Использование: svg2ico <input.svg> <output.ico>
//
// Формат ICO: PNG-независимый, BMP-in-ICO (BGRA + AND-маска нулями).
// Размеры: 16, 24, 32, 48, 64, 128. Формат 256 в BMP-режиме не пишем.

#include <lunasvg.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using namespace lunasvg;

static void writeU16(FILE* f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    fwrite(b, 1, 2, f);
}
static void writeU32(FILE* f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                     (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
    fwrite(b, 1, 4, f);
}
static void putU32(uint8_t* d, uint32_t v) {
    d[0]=v&0xFF; d[1]=(v>>8)&0xFF; d[2]=(v>>16)&0xFF; d[3]=(v>>24)&0xFF;
}
static void putU16(uint8_t* d, uint16_t v) {
    d[0]=v&0xFF; d[1]=(v>>8)&0xFF;
}

struct Image {
    int size;
    std::vector<uint8_t> pixels; // BGRA, top-down
};

static Image renderSVG(const std::string& path, int size) {
    auto doc = Document::loadFromFile(path);
    if (!doc) {
        fprintf(stderr, "Failed to load SVG: %s\n", path.c_str());
        exit(1);
    }
    auto bmp = doc->renderToBitmap(size, size);
    if (bmp.isNull()) {
        fprintf(stderr, "Failed to render %d\n", size);
        exit(1);
    }
    bmp.convertToRGBA();

    Image img;
    img.size = size;
    img.pixels.resize((size_t)size * size * 4);
    const uint8_t* src = bmp.data();
    int stride = bmp.stride();
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const uint8_t* p = src + (size_t)y * stride + (size_t)x * 4;
            uint8_t* d = &img.pixels[((size_t)y * size + x) * 4];
            d[0] = p[2]; // B
            d[1] = p[1]; // G
            d[2] = p[0]; // R
            d[3] = p[3]; // A
        }
    }
    return img;
}

static std::vector<uint8_t> packBMP(const Image& img) {
    int w = img.size, h = img.size;
    int xorSize = w * h * 4;
    int andRowBytes = ((w + 31) / 32) * 4;
    int andSize = andRowBytes * h;
    int total = 40 + xorSize + andSize;
    std::vector<uint8_t> blob(total, 0);
    uint8_t* p = blob.data();

    // BITMAPINFOHEADER
    putU32(p + 0, 40);
    putU32(p + 4, (uint32_t)w);
    putU32(p + 8, (uint32_t)(h * 2));  // XOR + AND
    putU16(p + 12, 1);
    putU16(p + 14, 32);
    putU32(p + 16, 0);       // BI_RGB
    putU32(p + 20, xorSize);
    putU32(p + 24, 0);
    putU32(p + 28, 0);
    putU32(p + 32, 0);
    putU32(p + 36, 0);

    // XOR bitmap (bottom-up BGRA)
    for (int y = 0; y < h; ++y) {
        int srcY = h - 1 - y;
        memcpy(p + 40 + (size_t)y * w * 4,
               &img.pixels[(size_t)srcY * w * 4],
               (size_t)w * 4);
    }
    // AND bitmap — уже нули
    return blob;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.svg> <output.ico>\n", argv[0]);
        return 1;
    }
    std::string inPath  = argv[1];
    std::string outPath = argv[2];

    std::vector<int> sizes = { 16, 24, 32, 48, 64, 128 };
    std::vector<Image> images;
    for (int s : sizes) images.push_back(renderSVG(inPath, s));

    std::vector<std::vector<uint8_t>> blobs;
    for (auto& img : images) blobs.push_back(packBMP(img));

    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f) { perror("fopen"); return 1; }

    writeU16(f, 0);
    writeU16(f, 1);
    writeU16(f, (uint16_t)images.size());

    uint32_t dataOffset = 6 + 16 * (uint32_t)images.size();
    std::vector<uint32_t> offsets;
    for (auto& b : blobs) {
        offsets.push_back(dataOffset);
        dataOffset += (uint32_t)b.size();
    }

    for (size_t i = 0; i < images.size(); ++i) {
        int w = images[i].size;
        int h = images[i].size;
        uint8_t entry[16] = {0};
        entry[0] = (w >= 256) ? 0 : (uint8_t)w;
        entry[1] = (h >= 256) ? 0 : (uint8_t)h;
        entry[2] = 0;
        entry[3] = 0;
        entry[4] = 1;  // planes
        entry[5] = 0;
        entry[6] = 32; // bitcount
        entry[7] = 0;
        uint32_t sz = (uint32_t)blobs[i].size();
        putU32(entry + 8, sz);
        putU32(entry + 12, offsets[i]);
        fwrite(entry, 1, 16, f);
    }
    for (auto& b : blobs) fwrite(b.data(), 1, b.size(), f);
    fclose(f);
    printf("OK: %s (%zu images: 16,24,32,48,64,128)\n", outPath.c_str(), images.size());
    return 0;
}
