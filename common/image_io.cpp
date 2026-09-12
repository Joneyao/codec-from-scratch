// image_io.cpp — 最小 PPM (P6) 读写实现
#include "image_io.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace cfs {

namespace {
// 跳过 PPM 头里的空白和 # 注释。
void SkipWhitespaceAndComments(std::istream& is) {
    int c;
    while ((c = is.peek()) != EOF) {
        if (std::isspace(c)) {
            is.get();
        } else if (c == '#') {
            std::string line;
            std::getline(is, line);
        } else {
            break;
        }
    }
}
}  // namespace

bool LoadPpm(const std::string& path, RgbImage& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    std::string magic;
    is >> magic;
    if (magic != "P6") return false;

    SkipWhitespaceAndComments(is);
    int width = 0, height = 0, maxval = 0;
    is >> width;
    SkipWhitespaceAndComments(is);
    is >> height;
    SkipWhitespaceAndComments(is);
    is >> maxval;
    if (width <= 0 || height <= 0 || maxval != 255) return false;

    is.get();  // 头之后恰好一个空白分隔符

    out.width = width;
    out.height = height;
    out.data.resize(static_cast<size_t>(width) * height * 3);
    is.read(reinterpret_cast<char*>(out.data.data()),
            static_cast<std::streamsize>(out.data.size()));
    return static_cast<size_t>(is.gcount()) == out.data.size();
}

bool SavePpm(const std::string& path, const RgbImage& img) {
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    os << "P6\n" << img.width << " " << img.height << "\n255\n";
    os.write(reinterpret_cast<const char*>(img.data.data()),
             static_cast<std::streamsize>(img.data.size()));
    return static_cast<bool>(os);
}

}  // namespace cfs
