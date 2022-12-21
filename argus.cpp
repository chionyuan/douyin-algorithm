//
// Created by jpacg on 2022/12/20.
//

#include "argus.h"
#include "base64.h"
#include "hexdump.hpp"
#include <string>
#include <iostream>
extern "C" {
#include "md5.h"
#include "WjCryptLib_AesCbc.h"
#include "sm3.h"
#include "simon.h"
}

#include "bytearray.hpp"
#include "bytearray_processor.hpp"
#include "bytearray_reader.hpp"
#include "bytearray_view.hpp"
#include "ByteBuf.hpp"
#include "ByteBuf.h"

int md5(uint8_t *data, uint32_t size, uint8_t result[16]) {
    MD5Context ctx;
    md5Init(&ctx);
    md5Update(&ctx, data, size);
    md5Finalize(&ctx);
    memcpy(result, ctx.digest, 16);
    return 0;
}

int encrypt_enc_pb(const uint8_t *encode_pb, uint32_t size, const uint8_t xor_array[8]) {
    auto *buffer = new uint8_t[size + 8];
    memset(buffer, 0, size + 8);
    memcpy(&buffer[8], encode_pb, size);
    std::reverse(buffer, buffer + size + 8);
    for (int i = 0; i < (size + 8); ++i) {
        buffer[i] = buffer[i] ^ xor_array[i % 4];
    }
//    std::cout << "加密后的pb:\n" << Hexdump(buffer, size+8) << std::endl;
    return 0;
}

uint8_t *memdup(uint8_t *data, uint32_t size) {
    auto buf = new uint8_t[size];
    memcpy(buf, data, size);
    return buf;
}


// 有问题
int decrypt_enc_pb(uint8_t *data, uint32_t len) {
    // 后8位
    ByteBuf ba(&data[len-8], 8);

    for (int i = 0; i < len - 8; ++i) {
        uint8_t ch = data[i];
        data[i] = data[i] ^ ba.data()[i % 4];
//        printf("idx:%d mod:%02x     %02x ^ %02x = %02x\n", i, i % 8, ch, xor_array[i % 8], data[i]);
    }
    std::reverse(data, data+len);
    encrypt_enc_pb(&data[8], len-8, ba.data());
    return 0;
}


int decrypt_argus(const char *x_argus) {
    auto argus = base64_decode(std::string(x_argus));
    uint16_t rand_right = *(uint16_t *)argus.data();

    auto sign_key = base64_decode(std::string("jr36OAbsxc7nlCPmAp7YJUC8Ihi7fq73HLaR96qKovU="));
    uint8_t aes_key[16] = {0};
    uint8_t aes_iv[16] = {0};
    md5(reinterpret_cast<uint8_t *>(sign_key.data()), 16, aes_key);
    md5(reinterpret_cast<uint8_t *>(sign_key.data() + 16), 16, aes_iv);

    std::string output;
    output.resize(argus.size()-2);

    AesCbcDecryptWithKey(aes_key, 16, aes_iv, argus.data() + 2, output.data(), output.size());

    ByteBuf ba((const uint8_t *)output.data(), output.size());
    ba.remove_padding();
    std::cout << "aes result remove padding: \n" << Hexdump(ba.data(), ba.size()) << std::endl;
    // 第一个字节是0x35(真机)或者0xa6(unidbg), 不确定怎么来的, 步骤很多
    // 再后面四个字节是 random 的数据, 不知道干嘛用的
    // 再后面四个字节是 (01 02 0c 18) (01 d0 06 18) (01 d0 0f 18), 不确定怎么来的
    // 数据
    // 倒数2个字节是随机数高位

    uint32_t len = ba.size();
    uint16_t rand_left = *(uint16_t *)&ba.data()[len - 2];

    uint32_t rand = rand_left;
    rand = rand << 16 | rand_right;
    printf("%x %x random=%x\n", rand_right, rand_left, rand);

    uint32_t bsize = len-9-2;
    auto *b_buffer = new uint8_t[bsize];
    memcpy(b_buffer, &ba.data()[9], bsize);

    std::cout << "enc_pb 解密前:\n" << Hexdump(b_buffer, bsize) << std::endl;
    decrypt_enc_pb(b_buffer, bsize);
    std::cout << "enc_pb 解密后(前8个字节是异或):\n" << Hexdump(b_buffer, bsize) << std::endl;

    // sm3(sign_key + random + sign_key)
    auto size = sign_key.size() + 4 + sign_key.size();
    sh::ByteBuf sm3Buf;
    sm3Buf.writeBytes(sign_key.data(), sign_key.size());
    sm3Buf.writeBytes(reinterpret_cast<const char *>(&rand), 4);
    sm3Buf.writeBytes(sign_key.data(), sign_key.size());
    unsigned char sm3_output[32] = {0};
    sm3((unsigned char *) sm3Buf.data(), size, sm3_output);

    uint64_t key[] = {0, 0, 0, 0};
    memcpy(key, sm3_output, 32);

    uint64_t ct[2] = {0, 0};
    uint64_t pt[2] = {0, 0};

    uint8_t *p = &b_buffer[8];
    uint32_t new_len = bsize - 8;

    auto *protobuf = new uint8_t[new_len];
    for (int i = 0; i < new_len / 16; ++i) {
        memcpy(&ct, &p[i * 16], 16);
        simon_dec(pt, ct, key);
        // 定位到行，写一行(16字节)
        memcpy(&protobuf[i * 16], &pt[0], 8);
        memcpy(&protobuf[i * 16 + 8], &pt[1], 8);
    }

    ByteBuf pb_ba(protobuf, new_len);
//    pb_ba.remove_padding();

    std::cout << "protobuf:\n" << Hexdump(pb_ba.data(), pb_ba.size()) << std::endl;
    return 0;
}

int encrypt_argus() {
    return 0;
}