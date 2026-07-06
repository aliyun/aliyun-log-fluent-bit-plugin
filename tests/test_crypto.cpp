#include "sls_crypto.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint8_t> abc = {'a', 'b', 'c'};
    assert(aliyun_sls::crypto::md5Base64(abc) == "kAFQmDzST7DWlj99KOF/cg==");
    assert(aliyun_sls::crypto::md5HexUpper(abc) == "900150983CD24FB0D6963F7D28E17F72");

    const auto hmac =
        aliyun_sls::crypto::hmacSha1Base64("key", "The quick brown fox jumps over the lazy dog");
    assert(hmac == "3nybhbi3iqa8ino29wqQcBydtNk=");

    std::cout << "crypto test passed\n";
    return 0;
}
