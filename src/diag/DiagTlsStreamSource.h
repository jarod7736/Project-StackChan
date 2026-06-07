#pragma once
// Diagnostic TLS streaming source — rung 2e (HTTPS overlap).
//
// Reconstructs the production TtsStreamSource that ecf953b removed: owns a
// WiFiClientSecure + HTTPClient, opens the connection in the ctor, and feeds
// MP3 bytes to the decoder AS THEY ARRIVE — so the HTTPS/TLS connection stays
// open and WiFi-RX + TLS-decrypt run concurrently with the amp + decoder for
// the whole track. That is the exact concurrency ecf953b's buffer-then-play fix
// removed, now isolated with TLS as the only addition over the (cleared) plain-
// HTTP rung 2d. setInsecure() trusts lobsterboy's self-signed diag cert — cert
// validation is skipped, but the TLS handshake + per-record decrypt current
// (the thing we're testing) is unchanged. AudioPlayer::playStream() takes
// ownership and deletes this on teardown, which closes the connection.

#include <AudioFileSource.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>

namespace stkchan {

class DiagTlsStreamSource : public AudioFileSource {
public:
    explicit DiagTlsStreamSource(const char* url) {
        tls_ = new WiFiClientSecure();
        tls_->setInsecure();  // self-signed diag cert; TLS crypto load is the point
        http_ = new HTTPClient();
        if (http_->begin(*tls_, url) && http_->GET() == HTTP_CODE_OK) {
            stream_ = http_->getStreamPtr();
            size_   = http_->getSize();  // -1 = unknown (chunked / close-delimited)
        }
    }
    ~DiagTlsStreamSource() override { close(); }

    bool     open(const char*) override  { return stream_ != nullptr; }
    bool     isOpen() override           { return stream_ != nullptr; }
    uint32_t getSize() override          { return size_ > 0 ? (uint32_t)size_ : 0x7FFFFFFFu; }
    uint32_t getPos() override           { return pos_; }
    bool     seek(int32_t, int) override { return false; }  // no seek on a live stream

    uint32_t read(void* dst, uint32_t len) override {
        if (!stream_ || len == 0) return 0;
        uint8_t* out = static_cast<uint8_t*>(dst);
        uint32_t got = 0;
        uint32_t lastByte = millis();
        while (got < len) {
            int avail = stream_->available();
            if (avail > 0) {
                int n = stream_->readBytes(
                    out + got, std::min((uint32_t)avail, len - got));
                if (n > 0) { got += n; lastByte = millis(); }
            } else {
                if (http_ && !http_->connected() && stream_->available() == 0) break;
                if (millis() - lastByte > 3000) break;  // stall guard
                if (got > 0) break;  // hand back what we have; decoder asks again
                delay(2);
            }
        }
        pos_ += got;
        return got;
    }

    bool close() override {
        if (http_) { http_->end(); delete http_; http_ = nullptr; }
        if (tls_)  { delete tls_;  tls_  = nullptr; }
        stream_ = nullptr;
        return true;
    }

private:
    WiFiClientSecure* tls_    = nullptr;
    HTTPClient*       http_   = nullptr;
    Stream*           stream_ = nullptr;
    int               size_   = -1;
    uint32_t          pos_    = 0;
};

}  // namespace stkchan
