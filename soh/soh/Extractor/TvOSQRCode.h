#pragma once
#ifdef __TVOS__
#include <cstdint>
#include <string>
#include <vector>
#include <qrcodegen.h>

class TvOSQRCode {
public:
    bool Encode(const std::string& text) {
        uint8_t tempBuf[qrcodegen_BUFFER_LEN_MAX];
        mQR.resize(qrcodegen_BUFFER_LEN_MAX, 0);
        bool ok = qrcodegen_encodeText(text.c_str(), tempBuf, mQR.data(),
                                       qrcodegen_Ecc_MEDIUM,
                                       qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                       qrcodegen_Mask_AUTO, true);
        if (!ok) mQR.clear();
        return ok;
    }

    int Size() const {
        if (mQR.empty()) return 0;
        return qrcodegen_getSize(mQR.data());
    }

    bool Module(int x, int y) const {
        if (mQR.empty()) return false;
        return qrcodegen_getModule(mQR.data(), x, y);
    }

private:
    std::vector<uint8_t> mQR;
};

#endif
